#pragma once

#include "mathlib/vector2d.h"

// Spread pivot (prototype): the gun turns toward where its bullets actually go. A shot's spread comes from
// its command's random seed (MD5_PseudoRandom of the command number, see FireBullets), which the server
// uses too, so the client can work out any shot's direction exactly, including the next one while the
// trigger is held. cl_neo_spread_pivot picks lead (point at the next shot) or follow (kick toward each shot
// as it fires); see neo_spread_pivot.cpp.

class C_BasePlayer;
class C_NEOBaseCombatWeapon;
class CUserCmd;
class QAngle;
class Vector;

// A shot the local player's weapon fired: its command, the direction it was aimed (before spread), the spread
// cone it used, and how many bullets the command fired. Called from the weapon's primary attack; only the
// first prediction of a command counts.
void NeoSpreadPivotShot(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &aim, const Vector &spread,
	int shots);

// A shotgun's shot (their own fire code; first prediction only): each pellet's direction, worked out the same
// way. The gun turns toward the pattern's centre and knocks that way (scaled up to the cone's range: the centre of
// many pellets sits near the middle), and each pellet gets its impact mark. One pellet is the Supa 7's slug.
void NeoSpreadPivotPellets(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &aim, const Vector &spread,
	int pellets);

// The last shotgun shot's pattern, for the crosshair's ring: each pellet's place in the cone (about -1..1 each way).
struct NeoSpreadPattern
{
	static constexpr int MAX_PELLETS = 16;
	float time = -100.0f;	// realtime it was fired
	float fired = -100.0f;	// the gun's predicted time it was fired (its readiness is measured on that clock)
	const C_NEOBaseCombatWeapon *pWeapon = nullptr;
	int count = 0;
	Vector2D cone[MAX_PELLETS];
};
const NeoSpreadPattern &NeoSpreadPivotLastPattern();

// Turns the viewmodel's angles (eye angles plus its own offsets) toward this frame's pivot, in eye space.
void NeoSpreadPivotApply(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner, QAngle &angles);
