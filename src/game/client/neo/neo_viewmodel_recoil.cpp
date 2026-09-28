#include "cbase.h"
#include "neo_viewmodel_recoil.h"
#include "weapon_neobasecombatweapon.h"
#include "c_neo_player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_viewmodel_recoil("cl_neo_viewmodel_recoil", "0", FCVAR_ARCHIVE,
	"Prototype: each shot knocks the gun like the arms taking it (cosmetic). Strength; 0 = off.", true, 0, true, 4);
ConVar cl_neo_viewmodel_recoil_freq("cl_neo_viewmodel_recoil_freq", "11", FCVAR_ARCHIVE,
	"How fast the gun's nose springs back, in Hz (the whole gun follows at 60% of this).", true, 2, true, 30);
ConVar cl_neo_viewmodel_recoil_damping("cl_neo_viewmodel_recoil_damping", "0.27", FCVAR_ARCHIVE,
	"How much the nose sways before settling: 1 = no overshoot, lower sways more.", true, 0.1f, true, 1.5f);
ConVar cl_neo_viewmodel_recoil_lateral("cl_neo_viewmodel_recoil_lateral", "0.44", FCVAR_ARCHIVE,
	"How much each shot pushes the gun sideways, toward where it went, against the straight kick up and back.",
	true, 0, true, 4);

// Peak movement for a shot of strength 1: degrees of nose rise and sideways turn, and units back and aside.
static constexpr float KICK_PITCH = 1.6f;
static constexpr float KICK_YAW = 1.0f;
static constexpr float KICK_ROLL = 1.2f;
static constexpr float KICK_BACK = 0.9f;
static constexpr float KICK_SIDE = 0.35f;
static constexpr float KICK_UP = 0.15f;
static constexpr float LINEAR_FREQ_SCALE = 0.6f;	// the whole gun lags the nose
static constexpr float LINEAR_DAMPING = 0.65f;
static constexpr float BRACED_ROTATION = 0.4f;		// on the sights, of the rotation

// One damped spring per axis: x'' = -w^2 x - 2 z w x'.
struct Spring3
{
	Vector x = vec3_origin;
	Vector v = vec3_origin;

	void Step(float dt, float omega, float zeta)
	{
		const Vector a = x * (-omega * omega) - v * (2.0f * zeta * omega);
		v += a * dt;
		x += v * dt;
	}
};

static struct
{
	Spring3 rotation;	// pitch, yaw, roll (degrees)
	Spring3 linear;		// forward, right, up (units)
	int updatedFrame = -1;
} s_recoil;

// A shot's strength from the gun's own recoil numbers (its camera kick or recoil range, whichever it has),
// compressed so a sniper doesn't throw the gun off screen.
static float ShotStrength(C_NEOBaseCombatWeapon *pWeapon, bool bAimed)
{
	const WeaponHandlingInfo_t &handling = pWeapon->GetWeaponHandling();
	const WeaponKickInfo_t &kick = handling.kickInfo;
	const WeaponRecoilInfo_t &recoil = handling.recoilInfo;
	const float kickSize = Max(fabsf(kick.minX), fabsf(kick.maxX)) + 0.5f * Max(fabsf(kick.minY), fabsf(kick.maxY));
	const float recoilSize = (Max(fabsf(recoil.minX), fabsf(recoil.maxX)) + 0.5f * Max(fabsf(recoil.minY), fabsf(recoil.maxY)))
		* (bAimed ? recoil.adsFactor : recoil.hipFactor);
	return clamp(sqrtf(Max(kickSize, recoilSize)), 0.4f, 2.0f);
}

void NeoViewmodelRecoilShot(C_NEOBaseCombatWeapon *pWeapon, const Vector2D &conePosition)
{
	const float scale = cl_neo_viewmodel_recoil.GetFloat();
	auto *pOwner = pWeapon ? ToNEOPlayer(pWeapon->GetOwner()) : nullptr;
	if (scale <= 0.0f || !pOwner)
	{
		return;
	}
	const float strength = ShotStrength(pWeapon, pOwner->IsInAim()) * scale;
	const float lateral = cl_neo_viewmodel_recoil_lateral.GetFloat();
	const float side = clamp(conePosition.x, -1.0f, 1.0f) * lateral;
	const float lift = clamp(conePosition.y, -1.0f, 1.0f) * lateral;

	// An impulse v0 on a spring peaks near v0 / w, so each kick is its peak times w.
	const float omega = 2.0f * M_PI_F * cl_neo_viewmodel_recoil_freq.GetFloat();
	const float omegaLinear = omega * LINEAR_FREQ_SCALE;
	// Nose up (negative pitch) and toward the shot (right = negative yaw), rolled with it.
	s_recoil.rotation.v += Vector(-(KICK_PITCH + 0.5f * KICK_PITCH * lift), -KICK_YAW * side, KICK_ROLL * side) * (strength * omega);
	// The whole gun back toward the eye and aside with the shot.
	s_recoil.linear.v += Vector(-KICK_BACK, KICK_SIDE * side, KICK_UP * (1.0f + lift)) * (strength * omegaLinear);
}

void NeoViewmodelRecoilApply(C_BasePlayer *pOwner, const QAngle &eyeAngles, float ironsightBlend, Vector &origin,
	QAngle &angles)
{
	if (!pOwner || !pOwner->IsLocalPlayer())
	{
		return;
	}
	if (s_recoil.updatedFrame != gpGlobals->framecount)
	{
		s_recoil.updatedFrame = gpGlobals->framecount;
		const float omega = 2.0f * M_PI_F * cl_neo_viewmodel_recoil_freq.GetFloat();
		const float zeta = cl_neo_viewmodel_recoil_damping.GetFloat();
		// Small steps: the springs are stiff next to a slow frame.
		constexpr float STEP = 1.0f / 240.0f;
		for (float left = Min(gpGlobals->frametime, 0.1f); left > 0.0f; left -= STEP)
		{
			const float dt = Min(left, STEP);
			s_recoil.rotation.Step(dt, omega, zeta);
			s_recoil.linear.Step(dt, omega * LINEAR_FREQ_SCALE, LINEAR_DAMPING);
		}
	}

	const Vector &rot = s_recoil.rotation.x;
	const Vector &lin = s_recoil.linear.x;
	if (rot.LengthSqr() + lin.LengthSqr() < 1e-8f)
	{
		return;
	}
	Vector forward, right, up;
	AngleVectors(eyeAngles, &forward, &right, &up);
	origin += forward * lin.x + right * lin.y + up * lin.z;

	const float braced = 1.0f - (1.0f - BRACED_ROTATION) * ironsightBlend;
	matrix3x4_t base, local, result;
	AngleMatrix(angles, base);
	AngleMatrix(QAngle(rot.x * braced, rot.y * braced, rot.z * braced), local);
	ConcatTransforms(base, local, result);
	MatrixAngles(result, angles);
}
