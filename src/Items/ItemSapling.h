
#pragma once

#include "ItemHandler.h"





class cItemSaplingHandler final :
	public cItemHandler
{
	using Super = cItemHandler;

public:

	using Super::Super;


	virtual bool CommitPlacement(cPlayer & a_Player, const cItem & a_HeldItem, const Vector3i a_PlacePosition, const eBlockFace a_ClickedBlockFace, const Vector3i a_CursorPosition) const override
	{
		return a_Player.PlaceBlock(
			a_PlacePosition,
			static_cast<BLOCKTYPE>(m_ItemType),
			static_cast<NIBBLETYPE>(a_HeldItem.m_ItemDamage & 0x07)  // Allow only the lowest 3 bits (top bit is for growth).
		);
	}
} ;
