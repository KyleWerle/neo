#pragma once

// Viewmodel recoil (prototype): each shot knocks the gun like the arms taking it, cosmetic only (the camera
// and the bullets are untouched). Two springs: the nose kicks up and toward where the shot went and sways
// back (rotation, fast, a little under-damped); the whole gun shifts back and the same way and is pulled
// back into line a moment later (translation, slower), so the rear moves too as the arms compensate.

class C_BasePlayer;
class C_NEOBaseCombatWeapon;
class QAngle;
class Vector;
class Vector2D;

// A shot fired by the local player's weapon (first prediction only): where it went within its cone, each
// axis in about -1..1 (0 when the cone is tiny).
void NeoViewmodelRecoilShot(C_NEOBaseCombatWeapon *pWeapon, const Vector2D &conePosition);

// A shotgun's shot (first prediction only): the knock toward where its pellets went, the pattern's centre within
// the cone scaled up to the cone's range (NeoSpreadPivotPellets), with the shotguns' heavy knock.
void NeoViewmodelRecoilPelletShot(C_NEOBaseCombatWeapon *pWeapon, const Vector2D &conePosition);

// Adds this frame's knock to the viewmodel's origin and angles. On the sights (ironsightBlend) the rotation
// is braced down so the sight picture stays usable; the push back stays.
void NeoViewmodelRecoilApply(C_BasePlayer *pOwner, const QAngle &eyeAngles, float ironsightBlend, Vector &origin,
	QAngle &angles);
