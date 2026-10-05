#pragma once

#include "ehandle.h"

// Who the view is through and, when that is a player watched in first person, their shots. Their clip is sent to
// them alone, so their shots are counted by their muzzle flash counter instead.

class C_NEO_Player;
class C_NEOBaseCombatWeapon;

struct NeoViewShots
{
	CHandle<C_NEO_Player> hPlayer;
	CHandle<C_NEOBaseCombatWeapon> hWeapon;
	bool bSpectating = false;
	int count = 0; // only the difference between two frames means anything
	float viewChanged = -100.0f; // realtime the view player last changed
};

const NeoViewShots &NeoGetViewShots();

// The local player, or the player they watch in first person.
C_NEO_Player *NeoViewPlayer();
