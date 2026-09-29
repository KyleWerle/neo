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

// A shot from a weapon with its own fire code and no one place its shot went (the shotguns' pellets): the knock
// toward a random point of the cone, from a private random stream (the game's own numbers are untouched). Call
// after the weapon fires; counts only on a command's first prediction.
void NeoViewmodelRecoilRandomShot(C_NEOBaseCombatWeapon *pWeapon);

// Adds this frame's knock to the viewmodel's origin and angles. On the sights (ironsightBlend) the rotation
// is braced down so the sight picture stays usable; the push back stays.
void NeoViewmodelRecoilApply(C_BasePlayer *pOwner, const QAngle &eyeAngles, float ironsightBlend, Vector &origin,
	QAngle &angles);
