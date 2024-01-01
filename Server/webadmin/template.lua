-- Use a table for fast concatenation of strings
local SiteContent = {}
function Output(String)
	table.insert(SiteContent, String)
end





local function GetDefaultPage()
	local PM = cRoot:Get():GetPluginManager()

	local SubTitle = "概览"
	local Content = ""

	Content = Content .. "<h4>插件:</h4><ul>"
	PM:ForEachPlugin(
		function (a_CBPlugin)
			if (a_CBPlugin:IsLoaded()) then
				Content = Content ..  "<li>" .. a_CBPlugin:GetName() .. " (版本 " .. a_CBPlugin:GetVersion() .. ")</li>"
			end
		end
	)

	Content = Content .. "</ul>"
	Content = Content .. "<h4>玩家:</h4><ul>"

	cRoot:Get():ForEachPlayer(
		function(a_CBPlayer)
			Content = Content .. "<li>" .. a_CBPlayer:GetName() .. "</li>"
		end
	)

	Content = Content .. "</ul>";

	return Content, SubTitle
end





function ShowPage(WebAdmin, TemplateRequest)
	SiteContent = {}
	local BaseURL = cWebAdmin:GetBaseURL(TemplateRequest.Request.Path)
	local Title = "Cuberite Web 管理系统"
	local NumPlayers = cRoot:Get():GetServer():GetNumPlayers()
	local MemoryUsageKiB = cRoot:GetPhysicalRAMUsage()
	local NumChunks = cRoot:Get():GetTotalChunkCount()
	local PluginPage = cWebAdmin:GetPage(TemplateRequest.Request)
	local PageContent = PluginPage.Content
	local SubTitle = PluginPage.PluginFolder
	if (PluginPage.UrlPath ~= "") then
		SubTitle = PluginPage.PluginFolder .. " - " .. PluginPage.TabTitle
	end
	if (PageContent == "") then
		PageContent, SubTitle = GetDefaultPage()
	end

	Output([[
<!-- Copyright Justin S and Cuberite Team, licensed under CC-BY-SA 3.0 -->
<!DOCTYPE html>
<html lang="en">
<head>
	<meta charset="utf-8">
	<meta name="viewport" content="width=device-width, initial-scale=1.0">
	<title>]] .. Title .. [[</title>
	<link rel="stylesheet" type="text/css" href="/style.css">
</head>
<body>
<div class="header color-background">
	<div class="wrapper">
		<a href="]] .. BaseURL .. [[" class="logo">Cuberite</a>
	</div>
</div>
<div class="panel">
	<div class="wrapper">
		<div class="welcome">
			<strong>欢迎您, ]] .. TemplateRequest.Request.Username .. [[</strong>
			<a href="/" class="link-logout">退出</a>
		</div>
		<ul class="stats">
			<li>在线玩家: <strong>]] .. NumPlayers .. [[</strong></li>
			<li>内存用量: <strong>]] .. string.format("%.2f", MemoryUsageKiB / 1024) .. [[MB</strong></li>
			<li>区块数: <strong>]] .. NumChunks .. [[</strong></li>
		</ul>
	</div>
</div>
<div class="columns">
	<div class="columns-wrapper">
		<div class="columns-spacing">
			<div class="box left">
				<h2 class="head color-background">Menu</h2>
				<ul class="sidebar">
					<li>
						<a href="]] .. BaseURL .. [[" class="link-home">主页</a>
					</li>
				</ul>
				<div class="category">服务器管理</div>
				<ul class="sidebar">
	]])

	-- Get all tabs:
	local perPluginTabs = {}
	for _, tab in ipairs(cWebAdmin:GetAllWebTabs()) do
		local pluginTabs = perPluginTabs[tab.PluginName] or {};
		perPluginTabs[tab.PluginName] = pluginTabs
		table.insert(pluginTabs, tab)
	end
	
	-- Sort by plugin:
	local pluginNames = {}
	for pluginName, pluginTabs in pairs(perPluginTabs) do
		table.insert(pluginNames, pluginName)
	end
	table.sort(pluginNames)
	
	-- Output by plugin, then alphabetically:
	for _, pluginName in ipairs(pluginNames) do
		local pluginTabs = perPluginTabs[pluginName]
		table.sort(pluginTabs,
			function(a_Tab1, a_Tab2)
				return ((a_Tab1.Title or "") < (a_Tab2.Title or ""))
			end
		)
		
		-- Translate the plugin name into the folder name (-> title)
		local pluginWebTitle = cPluginManager:Get():GetPluginFolderName(pluginName) or pluginName
		Output("<li><strong class=\"link-page\">" .. pluginWebTitle .. "</strong></li>\n");

		-- Output each tab:
		for _, tab in pairs(pluginTabs) do
			Output("<li><a href=\"" .. BaseURL .. pluginName .. "/" .. tab.UrlPath .. "\" class=\"sidebar-item link-subpage\">" .. tab.Title .. "</a></li>\n")
		end
		Output("\n");
	end


	Output([[
				</ul>
			</div>
			<div class="box right">
				<h1 class="head color-background">]] .. SubTitle .. [[</h1>
				<div class="main-content">]] .. PageContent .. [[</div>
			</div>
		</div>
	</div>
</div>
<div class="footer">
	<div class="footer-container">
		<div class="wrapper">
			<span class="copyright">Copyright © <a href="https://cuberite.org/" target="_blank">Cuberite 团队</a></span>
			<ul class="footer-links">
				<li><a href="https://cuberite.org/" target="_blank">Cuberite 主页</a></li>
				<li><a href="https://forum.cuberite.org/" target="_blank">论坛</a></li>
				<li><a href="https://api.cuberite.org/" target="_blank">API 文档</a></li>
				<li><a href="https://book.cuberite.org/" target="_blank">用户手册</a></li>
			</ul>
		</div>
	</div>
</div>
</body>
</html>
]])

	return table.concat(SiteContent)
end
