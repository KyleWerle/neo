#pragma once

// Spread pivot, part of Gun motion Full: each shot turns the gun toward where that bullet went. A shot's spread comes
// from its command's random seed, which the server uses too, so the client works out each shot's direction exactly.

class C_BasePlayer;
class C_NEOBaseCombatWeapon;
class CUserCmd;
class QAngle;
class Vector;

constexpr int NEO_SPREAD_PIVOT_MAX_PELLETS = 16;

// A shot by the local player's weapon, from its primary attack (only a command's first prediction counts): the
// direction it was aimed before spread, the spread cone, and how many bullets the command fired.
void NeoSpreadPivotShot(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &aim, const Vector &spread,
	int shots);

// A shotgun's shot, the same way: the gun turns and knocks toward the pellets' centre.
void NeoSpreadPivotPellets(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &aim, const Vector &spread,
	int pellets);

// A shot by a player watched in first person, as the direction it went (see neo_spectator_hits.h).
void NeoSpreadPivotWatchedShot(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner, const Vector &direction);

// Turns the viewmodel toward this frame's pivot; bFlipped for a viewmodel drawn mirrored (left-handed).
void NeoSpreadPivotApply(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner, bool bFlipped, QAngle &angles);
