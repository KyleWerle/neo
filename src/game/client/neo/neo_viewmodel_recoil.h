#pragma once

// Viewmodel recoil: each shot knocks the gun as if the arms took it, toward where the shot went. Cosmetic only, the
// camera and the bullets are untouched.

class C_BasePlayer;
class C_NEOBaseCombatWeapon;
class QAngle;
class Vector;
class Vector2D;

enum NeoGunMotionLevel
{
	NEO_GUN_MOTION_OFF = 0,
	NEO_GUN_MOTION_SUBTLE,
	NEO_GUN_MOTION_FULL,
};

// The Gun motion setting: Subtle is a lighter knock, Full adds the spread pivot, both crossfade animations.
NeoGunMotionLevel NeoGunMotion();

// A shot by the local player's weapon (first prediction only): where it went within its cone, about -1..1 each way.
void NeoViewmodelRecoilShot(C_NEOBaseCombatWeapon *pWeapon, const Vector2D &conePosition);

// A shotgun's shot: the pellets' centre within the cone, scaled up to the cone's range. Shotguns knock heavy.
void NeoViewmodelRecoilPelletShot(C_NEOBaseCombatWeapon *pWeapon, const Vector2D &conePosition);

// Adds this frame's knock to the viewmodel; bFlipped for a viewmodel drawn mirrored (left-handed).
void NeoViewmodelRecoilApply(C_BasePlayer *pOwner, const QAngle &eyeAngles, bool bFlipped, Vector &origin,
	QAngle &angles);
