#pragma once

// The view player's shots, for the gunplay's presentation (the recoil knock, the crosshair layer, the dot trail,
// the sight ghost): whoever's eyes the view is through, the local player or one watched in first person. The
// local player's by their clip dropping; a watched player's clip goes to them alone, so theirs by their muzzle
// flash counter, which every client receives. Updated once a frame, on first use.

class C_NEO_Player;
class C_NEOBaseCombatWeapon;

struct NeoGunplayShots
{
	C_NEO_Player *pPlayer = nullptr;		// the view player
	C_NEOBaseCombatWeapon *pWeapon = nullptr;	// their active weapon
	bool bSpectating = false;	// watching someone else: their clip and next attack aren't known here
	int count = 0;				// shots so far (only differences mean anything)
	float lastShot = -100.0f;	// realtime of the last one
	float viewChanged = -100.0f;	// realtime the view player last changed (a new spectate target, a spawn)
};

const NeoGunplayShots &NeoGunplayWatchShots();
