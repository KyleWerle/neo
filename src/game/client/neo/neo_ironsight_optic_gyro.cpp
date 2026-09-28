#include "cbase.h"
#include "neo_ironsight_optic_gyro.h"
#include "weapon_neobasecombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static constexpr float GYRO_MAX_ROLL = 1.2f;	// radians; beyond this the reticle is simply held back
static constexpr float GYRO_MAX_STEP = 0.05f;	// seconds; longer frames (hitches) are cut short
static constexpr int GYRO_SUBSTEPS = 4;

static struct
{
	bool valid = false;
	int frame = -1;
	char weapon[MAX_WEAPON_STRING] = "";
	float lastLensRoll = 0.0f;
	float roll = 0.0f;	// the reticle's roll on screen
	float rate = 0.0f;
} s_gyro;

// The difference between two angles, wrapped to [-pi, pi].
static float AngleDelta(float to, float from)
{
	float delta = fmodf(to - from + M_PI_F, 2.0f * M_PI_F);
	if (delta < 0.0f)
	{
		delta += 2.0f * M_PI_F;
	}
	return delta - M_PI_F;
}

float NeoIronsightGyroRoll(const CNEOWeaponInfo &data, float lensRoll)
{
	const bool bSameWeapon = s_gyro.valid && V_strcmp(s_gyro.weapon, data.szClassName) == 0;
	if (bSameWeapon && s_gyro.frame == gpGlobals->framecount)
	{
		return s_gyro.roll;
	}
	// A new weapon, or none drawn last frame (switched away, died): start level.
	if (!bSameWeapon || s_gyro.frame != gpGlobals->framecount - 1)
	{
		s_gyro.valid = true;
		V_strncpy(s_gyro.weapon, data.szClassName, sizeof(s_gyro.weapon));
		s_gyro.lastLensRoll = lensRoll;
		s_gyro.roll = 0.0f;
		s_gyro.rate = 0.0f;
	}
	s_gyro.frame = gpGlobals->framecount;

	const Vector &spring = data.m_vecIronOpticGyroSpring;
	const float stiffness = Max(spring.x, 0.1f);
	const float damping = Max(spring.y, 0.0f);
	const float drag = clamp(spring.z, 0.0f, 1.0f);

	// The lens's turn since last frame drags the reticle along, then the spring pulls it back to level.
	s_gyro.roll += drag * AngleDelta(lensRoll, s_gyro.lastLensRoll);
	s_gyro.lastLensRoll = lensRoll;
	const float step = clamp(gpGlobals->frametime, 0.0f, GYRO_MAX_STEP) / GYRO_SUBSTEPS;
	for (int i = 0; i < GYRO_SUBSTEPS; ++i)
	{
		const float accel = -stiffness * stiffness * s_gyro.roll - 2.0f * damping * stiffness * s_gyro.rate;
		s_gyro.rate += accel * step;
		s_gyro.roll += s_gyro.rate * step;
	}
	s_gyro.roll = clamp(s_gyro.roll, -GYRO_MAX_ROLL, GYRO_MAX_ROLL);
	return s_gyro.roll;
}
