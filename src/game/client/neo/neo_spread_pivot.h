#pragma once

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

// Turns the viewmodel's angles (eye angles plus its own offsets) toward this frame's pivot, in eye space.
void NeoSpreadPivotApply(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner, QAngle &angles);
