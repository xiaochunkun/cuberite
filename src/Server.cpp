// ReDucTor is an awesome guy who helped me a lot

#include "Globals.h"  // NOTE: MSVC stupidness requires this to be the same across all modules

#include "Server.h"
#include "ClientHandle.h"
#include "Mobs/Monster.h"
#include "Root.h"
#include "World.h"
#include "Bindings/PluginManager.h"
#include "ChatColor.h"
#include "Entities/Player.h"
#include "Inventory.h"
#include "Item.h"
#include "FurnaceRecipe.h"
#include "WebAdmin.h"
#include "Protocol/ProtocolRecognizer.h"
#include "CommandOutput.h"
#include "FastRandom.h"

#include "IniFile.h"

#include <fstream>
#include <sstream>
#include <iostream>





////////////////////////////////////////////////////////////////////////////////
// cServerListenCallbacks:

class cServerListenCallbacks:
	public cNetwork::cListenCallbacks
{
	cServer & m_Server;
	UInt16 m_Port;

	virtual cTCPLink::cCallbacksPtr OnIncomingConnection(const AString & a_RemoteIPAddress, UInt16 a_RemotePort) override
	{
		return m_Server.OnConnectionAccepted(a_RemoteIPAddress);
	}

	virtual void OnAccepted(cTCPLink & a_Link) override {}

	virtual void OnError(int a_ErrorCode, const AString & a_ErrorMsg) override
	{
		LOGWARNING("无法侦听端口 %d: %d (%s).", m_Port, a_ErrorCode, a_ErrorMsg.c_str());
	}

public:
	cServerListenCallbacks(cServer & a_Server, UInt16 a_Port):
		m_Server(a_Server),
		m_Port(a_Port)
	{
	}
};





////////////////////////////////////////////////////////////////////////////////
// cServer::cTickThread:

cServer::cTickThread::cTickThread(cServer & a_Server) :
	Super("服务器代码"),
	m_Server(a_Server)
{
}





void cServer::cTickThread::Execute(void)
{
	auto LastTime = std::chrono::steady_clock::now();
	static const auto msPerTick = std::chrono::milliseconds(50);

	while (!m_ShouldTerminate)
	{
		auto NowTime = std::chrono::steady_clock::now();
		auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(NowTime - LastTime).count();
		m_Server.Tick(static_cast<float>(msec));
		auto TickTime = std::chrono::steady_clock::now() - NowTime;

		if (TickTime < msPerTick)
		{
			// Stretch tick time until it's at least msPerTick
			std::this_thread::sleep_for(msPerTick - TickTime);
		}

		LastTime = NowTime;
	}
}





////////////////////////////////////////////////////////////////////////////////
// cServer:

cServer::cServer(void) :
	m_PlayerCount(0),
	m_ClientViewDistance(0),
	m_bIsConnected(false),
	m_RCONServer(*this),
	m_MaxPlayers(0),
	m_bIsHardcore(false),
	m_TickThread(*this),
	m_ShouldAuthenticate(false),
	m_UpTime(0)
{
	// Initialize the LuaStateTracker singleton before the app goes multithreaded:
	cLuaStateTracker::GetStats();
}





void cServer::ClientMovedToWorld(const cClientHandle * a_Client)
{
	cCSLock Lock(m_CSClients);
	m_ClientsToRemove.push_back(const_cast<cClientHandle *>(a_Client));
}





void cServer::PlayerCreated()
{
	m_PlayerCount++;
}





void cServer::PlayerDestroyed()
{
	m_PlayerCount--;
}





bool cServer::InitServer(cSettingsRepositoryInterface & a_Settings, bool a_ShouldAuth)
{
	m_Description = a_Settings.GetValueSet("Server", "Description", "Cuberite_dfgg - in C++!");
	m_ShutdownMessage = a_Settings.GetValueSet("Server", "ShutdownMessage", "服务器已关闭");
	m_MaxPlayers = static_cast<size_t>(a_Settings.GetValueSetI("Server", "MaxPlayers", 100));
	m_bIsHardcore = a_Settings.GetValueSetB("Server", "HardcoreEnabled", false);
	m_bAllowMultiLogin = a_Settings.GetValueSetB("Server", "AllowMultiLogin", false);
	m_RequireResourcePack = a_Settings.GetValueSetB("Server", "RequireResourcePack", false);
	m_ResourcePackUrl = a_Settings.GetValueSet("Server", "ResourcePackUrl", "");
	m_CustomRedirectUrl = a_Settings.GetValueSet("Server", "CustomRedirectUrl", "https://youtu.be/dQw4w9WgXcQ");


	m_FaviconData = Base64Encode(cFile::ReadWholeFile(AString("favicon.png")));  // Will return empty string if file nonexistant; client doesn't mind

	if (m_bIsConnected)
	{
		LOGERROR("错误：在服务器已在运行时尝试初始化服务器！");
		return false;
	}

	LOGINFO("兼容客户端: %s", MCS_CLIENT_VERSIONS);
	LOGD("兼容的协议版本 %s", MCS_PROTOCOL_VERSIONS);

	m_Ports = ReadUpgradeIniPorts(a_Settings, "Server", "Ports", "Port", "PortsIPv6", "25565");

	m_RCONServer.Initialize(a_Settings);

	m_bIsConnected = true;

	m_ServerID = "-";
	m_ShouldAuthenticate = a_ShouldAuth;
	if (m_ShouldAuthenticate)
	{
		auto & rand = GetRandomProvider();
		unsigned int r1 = rand.RandInt<unsigned int>(1000000000U, 0x7fffffffU);
		unsigned int r2 = rand.RandInt<unsigned int>(1000000000U, 0x7fffffffU);
		std::ostringstream sid;
		sid << std::hex << r1;
		sid << std::hex << r2;
		m_ServerID = sid.str();
		m_ServerID.resize(16, '0');
	}

	// Check if both BungeeCord and online mode are on, if so, warn the admin:
	m_ShouldAllowBungeeCord = a_Settings.GetValueSetB("Authentication", "AllowBungeeCord", false);
	m_OnlyAllowBungeeCord = a_Settings.GetValueSetB("Authentication", "OnlyAllowBungeeCord", false);
	m_ProxySharedSecret = a_Settings.GetValueSet("Authentication", "ProxySharedSecret", "");


	if (m_ShouldAllowBungeeCord && m_ShouldAuthenticate)
	{
		LOGWARNING("警告：允许使用BungeeCord，服务器设置为在线模式。这是不安全的，无法正常工作。在settings.ini中禁用身份验证或BungeeCord。");
	}

	if (m_ShouldAllowBungeeCord && m_ProxySharedSecret.empty())
	{
		LOGWARNING("警告：没有设置代理转发密钥，任何代理服务器都可以将玩家转发到此服务器，除非从 Internet 关闭。");
	}

	m_ShouldAllowMultiWorldTabCompletion = a_Settings.GetValueSetB("Server", "AllowMultiWorldTabCompletion", true);
	m_ShouldLimitPlayerBlockChanges = a_Settings.GetValueSetB("AntiCheat", "LimitPlayerBlockChanges", true);

	const auto ClientViewDistance = a_Settings.GetValueSetI("Server", "DefaultViewDistance", cClientHandle::DEFAULT_VIEW_DISTANCE);
	if (ClientViewDistance < cClientHandle::MIN_VIEW_DISTANCE)
	{
		m_ClientViewDistance = cClientHandle::MIN_VIEW_DISTANCE;
		LOGINFO("将默认视图距离设置为最小值 %d", m_ClientViewDistance);
	}
	else if (ClientViewDistance > cClientHandle::MAX_VIEW_DISTANCE)
	{
		m_ClientViewDistance = cClientHandle::MAX_VIEW_DISTANCE;
		LOGINFO("将默认视图距离设置为最大值 %d", m_ClientViewDistance);
	}
	else
	{
		m_ClientViewDistance = ClientViewDistance;
	}

	PrepareKeys();

	return true;
}





bool cServer::RegisterForgeMod(const AString & a_ModName, const AString & a_ModVersion, UInt32 a_ProtocolVersionNumber)
{
	auto & Mods = RegisteredForgeMods(a_ProtocolVersionNumber);

	return Mods.insert({a_ModName, a_ModVersion}).second;
}





void cServer::UnregisterForgeMod(const AString & a_ModName, UInt32 a_ProtocolVersionNumber)
{
	auto & Mods = RegisteredForgeMods(a_ProtocolVersionNumber);

	auto it = Mods.find(a_ModName);
	if (it != Mods.end())
	{
		Mods.erase(it);
	}
}





AStringMap & cServer::RegisteredForgeMods(const UInt32 a_Protocol)
{
	auto it = m_ForgeModsByVersion.find(a_Protocol);

	if (it == m_ForgeModsByVersion.end())
	{
		AStringMap mods;
		m_ForgeModsByVersion.insert({a_Protocol, mods});
		return m_ForgeModsByVersion.find(a_Protocol)->second;
	}

	return it->second;
}





const AStringMap & cServer::GetRegisteredForgeMods(const UInt32 a_Protocol)
{
	return RegisteredForgeMods(a_Protocol);
}





bool cServer::IsPlayerInQueue(const AString & a_Username)
{
	cCSLock Lock(m_CSClients);
	for (const auto & client : m_Clients)
	{
		if ((client->GetUsername()).compare(a_Username) == 0)
		{
			return true;
		}
	}
	return false;
}





void cServer::PrepareKeys(void)
{
	LOGD("正在生成协议加密密钥对...");
	VERIFY(m_PrivateKey.Generate(1024));
	m_PublicKeyDER = m_PrivateKey.GetPubKeyDER();
}





cTCPLink::cCallbacksPtr cServer::OnConnectionAccepted(const AString & a_RemoteIPAddress)
{
	LOGD("Client \"%s\" connected!", a_RemoteIPAddress.c_str());
	cClientHandlePtr NewHandle = std::make_shared<cClientHandle>(a_RemoteIPAddress, m_ClientViewDistance);
	cCSLock Lock(m_CSClients);
	m_Clients.push_back(NewHandle);
	return NewHandle;
}





void cServer::Tick(float a_Dt)
{
	// Update server uptime
	m_UpTime++;

	// Send the tick to the plugins, as well as let the plugin manager reload, if asked to (issue #102):
	cPluginManager::Get()->Tick(a_Dt);

	// Process all the queued commands:
	TickCommands();

	// Tick all clients not yet assigned to a world:
	TickClients(a_Dt);

	// Process all queued tasks
	TickQueuedTasks();
}





void cServer::TickClients(float a_Dt)
{
	cClientHandlePtrs RemoveClients;
	{
		cCSLock Lock(m_CSClients);

		// Remove clients that have moved to a world (the world will be ticking them from now on)
		for (auto itr = m_ClientsToRemove.begin(), end = m_ClientsToRemove.end(); itr != end; ++itr)
		{
			for (auto itrC = m_Clients.begin(), endC = m_Clients.end(); itrC != endC; ++itrC)
			{
				if (itrC->get() == *itr)
				{
					m_Clients.erase(itrC);
					break;
				}
			}
		}  // for itr - m_ClientsToRemove[]
		m_ClientsToRemove.clear();

		// Tick the remaining clients, take out those that have been destroyed into RemoveClients
		for (auto itr = m_Clients.begin(); itr != m_Clients.end();)
		{
			auto & Client = *itr;

			Client->ServerTick(a_Dt);
			if (Client->IsDestroyed())
			{
				// Delete the client later, when CS is not held, to avoid deadlock: https://forum.cuberite.org/thread-374.html
				RemoveClients.push_back(std::move(Client));
				itr = m_Clients.erase(itr);
				continue;
			}

			++itr;
		}  // for itr - m_Clients[]
	}

	// Delete the clients that have been destroyed
	RemoveClients.clear();
}





bool cServer::Start(void)
{
	for (const auto & port: m_Ports)
	{
		UInt16 PortNum;
		if (!StringToInteger(port, PortNum))
		{
			LOGWARNING("为服务器指定的端口无效： \"%s\". 忽略。", port.c_str());
			continue;
		}
		auto Handle = cNetwork::Listen(PortNum, std::make_shared<cServerListenCallbacks>(*this, PortNum));
		if (Handle->IsListening())
		{
			m_ServerHandles.push_back(Handle);
		}
	}  // for port - Ports[]
	if (m_ServerHandles.empty())
	{
		LOGERROR("无法打开任何端口。中止服务器");
		return false;
	}
	m_TickThread.Start();
	return true;
}





bool cServer::Command(cClientHandle & a_Client, AString & a_Cmd)
{
	bool Res = cRoot::Get()->DoWithPlayerByUUID(
		a_Client.GetUUID(),
		[&](cPlayer & a_Player)
		{
			return cRoot::Get()->GetPluginManager()->CallHookChat(a_Player, a_Cmd);
		}
	);
	return Res;
}





void cServer::QueueExecuteConsoleCommand(const AString & a_Cmd, cCommandOutputCallback & a_Output)
{
	// Put the command into a queue (Alleviates FS #363):
	cCSLock Lock(m_CSPendingCommands);
	m_PendingCommands.emplace_back(a_Cmd, &a_Output);
}





void cServer::ScheduleTask(cTickTime a_DelayTicks, std::function<void(cServer &)> a_Task)
{
	const auto TargetTick = a_DelayTicks + m_UpTime;
	// Insert the task into the list of scheduled tasks
	{
		cCSLock Lock(m_CSTasks);
		m_Tasks.emplace_back(TargetTick, std::move(a_Task));
	}
}





void cServer::ExecuteConsoleCommand(const AString & a_Cmd, cCommandOutputCallback & a_Output)
{
	AStringVector split = StringSplit(a_Cmd, " ");
	if (split.empty())
	{
		return;
	}

	// "stop" and "restart" are handled in cRoot::ExecuteConsoleCommand, our caller, due to its access to controlling variables

	// "help" and "reload" are to be handled by dfgg, so that they work no matter what
	if (split[0] == "help")
	{
		PrintHelp(split, a_Output);
		a_Output.Finished();
		return;
	}
	else if (split[0] == "reload")
	{
		if (split.size() > 1)
		{
			cPluginManager::Get()->ReloadPlugin(split[1]);
			a_Output.OutLn("插件重新加载计划");
		}
		else
		{
			cPluginManager::Get()->ReloadPlugins();
		}
		a_Output.Finished();
		return;
	}
	else if (split[0] == "reloadplugins")
	{
		cPluginManager::Get()->ReloadPlugins();
		a_Output.OutLn("插件重新加载");
		a_Output.Finished();
		return;
	}
	else if (split[0] == "reloadweb")
	{
		cRoot::Get()->GetWebAdmin()->Reload();
		a_Output.OutLn("重新加载 WebAdmin 配置");
		a_Output.Finished();
		return;
	}
	else if (split[0] == "load")
	{
		if (split.size() > 1)
		{
			cPluginManager::Get()->RefreshPluginList();  // Refresh the plugin list, so that if the plugin was added just now, it is loadable
			a_Output.OutLn(cPluginManager::Get()->LoadPlugin(split[1]) ? "插件已加载" : "加载插件时出错");
		}
		else
		{
			a_Output.OutLn("用法: load <插件文件夹>");
		}
		a_Output.Finished();
		return;
	}
	else if (split[0] == "unload")
	{
		if (split.size() > 1)
		{
			cPluginManager::Get()->UnloadPlugin(split[1]);
			a_Output.OutLn("插件已卸载");
		}
		else
		{
			a_Output.OutLn("用法: unload <插件文件夹>");
		}
		a_Output.Finished();
		return;
	}
	if (split[0] == "destroyentities")
	{
		cRoot::Get()->ForEachWorld([](cWorld & a_World)
			{
				a_World.ForEachEntity([](cEntity & a_Entity)
					{
						if (!a_Entity.IsPlayer())
						{
							a_Entity.Destroy();
						}
						return false;
					}
				);
				return false;
			}
		);
		a_Output.OutLn("销毁所有实体");
		a_Output.Finished();
		return;
	}

	// There is currently no way a plugin can do these (and probably won't ever be):
	else if (split[0].compare("chunkstats") == 0)
	{
		cRoot::Get()->LogChunkStats(a_Output);
		a_Output.Finished();
		return;
	}

	else if (split[0].compare("luastats") == 0)
	{
		a_Output.OutLn(cLuaStateTracker::GetStats());
		a_Output.Finished();
		return;
	}
	else if (cPluginManager::Get()->ExecuteConsoleCommand(split, a_Output, a_Cmd))
	{
		a_Output.Finished();
		return;
	}

	a_Output.OutLn("未知命令，请为所有命令键入“help”。");
	a_Output.Finished();
}





void cServer::PrintHelp(const AStringVector & a_Split, cCommandOutputCallback & a_Output)
{
	UNUSED(a_Split);
	typedef std::pair<AString, AString> AStringPair;
	typedef std::vector<AStringPair> AStringPairs;

	class cCallback :
		public cPluginManager::cCommandEnumCallback
	{
	public:
		cCallback(void) : m_MaxLen(0) {}

		virtual bool Command(const AString & a_Command, const cPlugin * a_Plugin, const AString & a_Permission, const AString & a_HelpString) override
		{
		UNUSED(a_Plugin);
		UNUSED(a_Permission);
			if (!a_HelpString.empty())
			{
				m_Commands.push_back(AStringPair(a_Command, a_HelpString));
				if (m_MaxLen < a_Command.length())
				{
					m_MaxLen = a_Command.length();
				}
			}
			return false;
		}

		AStringPairs m_Commands;
		size_t m_MaxLen;
	} Callback;
	cPluginManager::Get()->ForEachConsoleCommand(Callback);
	std::sort(Callback.m_Commands.begin(), Callback.m_Commands.end());
	for (AStringPairs::const_iterator itr = Callback.m_Commands.begin(), end = Callback.m_Commands.end(); itr != end; ++itr)
	{
		const AStringPair & cmd = *itr;
		// Output the commands and their help strings, with all the commands aligned to the same width
		a_Output.OutLn(fmt::format(FMT_STRING("{1:{0}s} - {2}"), Callback.m_MaxLen, cmd.first, cmd.second));
	}  // for itr - Callback.m_Commands[]
}





void cServer::BindBuiltInConsoleCommands(void)
{
	// Create an empty handler - the actual handling for the commands is performed before they are handed off to cPluginManager
	class cEmptyHandler:
		public cPluginManager::cCommandHandler
	{
		virtual bool ExecuteCommand(
			const AStringVector & a_Split,
			cPlayer * a_Player,
			const AString & a_Command,
			cCommandOutputCallback * a_Output = nullptr
		) override
		{
			return false;
		}
	};
	auto handler = std::make_shared<cEmptyHandler>();

	// Register internal commands:
	cPluginManager * PlgMgr = cPluginManager::Get();
	PlgMgr->BindConsoleCommand("help",            nullptr, handler, "显示可用命令");
	PlgMgr->BindConsoleCommand("reload",          nullptr, handler, "重新加载所有插件");
	PlgMgr->BindConsoleCommand("reloadweb",       nullptr, handler, "重新加载 webadmin 配置");
	PlgMgr->BindConsoleCommand("restart",         nullptr, handler, "干净地重新启动服务器");
	PlgMgr->BindConsoleCommand("stop",            nullptr, handler, "干净地停止服务器");
	PlgMgr->BindConsoleCommand("chunkstats",      nullptr, handler, "显示详细的块内存统计信息");
	PlgMgr->BindConsoleCommand("load",            nullptr, handler, "添加并启用指定的插件");
	PlgMgr->BindConsoleCommand("unload",          nullptr, handler, "禁用指定的插件");
	PlgMgr->BindConsoleCommand("destroyentities", nullptr, handler, "摧毁所有世界中的所有实体");
}





void cServer::Shutdown(void)
{
	// Stop listening on all sockets:
	for (const auto & srv: m_ServerHandles)
	{
		srv->Close();
	}
	m_ServerHandles.clear();

	// Notify the tick thread and wait for it to terminate:
	m_TickThread.Stop();

	// Save all chunks in all worlds, wait for chunks to be sent to the ChunkStorage queue for each world:
	cRoot::Get()->SaveAllChunksNow();

	// Remove all clients:
	cCSLock Lock(m_CSClients);
	for (auto itr = m_Clients.begin(); itr != m_Clients.end(); ++itr)
	{
		(*itr)->Destroy();
	}
	m_Clients.clear();
}





void cServer::KickUser(int a_ClientID, const AString & a_Reason)
{
	cCSLock Lock(m_CSClients);
	for (auto itr = m_Clients.begin(); itr != m_Clients.end(); ++itr)
	{
		if ((*itr)->GetUniqueID() == a_ClientID)
		{
			(*itr)->Kick(a_Reason);
		}
	}  // for itr - m_Clients[]
}





void cServer::AuthenticateUser(int a_ClientID, AString && a_Username, const cUUID & a_UUID, Json::Value && a_Properties)
{
	cCSLock Lock(m_CSClients);

	// Check max players condition within lock (expect server and authenticator thread to both call here)
	if (GetNumPlayers() >= GetMaxPlayers())
	{
		KickUser(a_ClientID, "服务器当前已满：(" "\n" "请稍后再试？");
		return;
	}

	for (const auto & Client : m_Clients)
	{
		if (Client->GetUniqueID() == a_ClientID)
		{
			Client->Authenticate(std::move(a_Username), a_UUID, std::move(a_Properties));
			return;
		}
	}
}





void cServer::TickCommands(void)
{
	decltype(m_PendingCommands) PendingCommands;
	{
		cCSLock Lock(m_CSPendingCommands);
		std::swap(PendingCommands, m_PendingCommands);
	}

	// Execute any pending commands:
	for (const auto & Command : PendingCommands)
	{
		ExecuteConsoleCommand(Command.first, *Command.second);
	}
}





void cServer::TickQueuedTasks(void)
{
	// Move the tasks to be executed to a seperate vector to avoid deadlocks on
	// accessing m_Tasks
	decltype(m_Tasks) Tasks;
	{
		cCSLock Lock(m_CSTasks);
		if (m_Tasks.empty())
		{
			return;
		}

		// Partition everything to be executed by returning false to move to end
		// of list if time reached
		auto MoveBeginIterator = std::partition(
			m_Tasks.begin(), m_Tasks.end(),
			[this](const decltype(m_Tasks)::value_type & a_Task)
			{
				return a_Task.first >= m_UpTime;
			});

		// Cut all the due tasks from m_Tasks into Tasks:
		Tasks.insert(
			Tasks.end(), std::make_move_iterator(MoveBeginIterator),
			std::make_move_iterator(m_Tasks.end()));
		m_Tasks.erase(MoveBeginIterator, m_Tasks.end());
	}

	// Execute each task:
	for (const auto & Task : Tasks)
	{
		Task.second(*this);
	}  // for itr - m_Tasks[]
}
