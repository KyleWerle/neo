#include "cbase.h"
#include "neo_viewmodel_recoil.h"
#include "weapon_neobasecombatweapon.h"
#include "c_neo_player.h"
#include "prediction.h"
#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_viewmodel_recoil("cl_neo_viewmodel_recoil", "0", FCVAR_ARCHIVE,
	"Prototype: each shot knocks the gun like the arms taking it (cosmetic). Strength; 0 = off.", true, 0, true, 4);
ConVar cl_neo_viewmodel_recoil_freq("cl_neo_viewmodel_recoil_freq", "7", FCVAR_ARCHIVE,
	"How fast the gun's nose springs back, in Hz (the whole gun follows at 60% of this).", true, 2, true, 30);
ConVar cl_neo_viewmodel_recoil_damping("cl_neo_viewmodel_recoil_damping", "0.27", FCVAR_ARCHIVE,
	"How much the nose sways before settling: 1 = no overshoot, lower sways more.", true, 0.1f, true, 1.5f);
ConVar cl_neo_viewmodel_recoil_lateral("cl_neo_viewmodel_recoil_lateral", "0.2", FCVAR_ARCHIVE,
	"How much each shot pushes the gun sideways, toward where it went, against the straight kick up and back.",
	true, 0, true, 4);

// The sideways part of each shot (where it went within its cone) turns and tilts the gun toward that side
// rather than sliding it along a flat plane (FCC: the sideways shifts and their resets looked odd).
ConVar cl_neo_viewmodel_recoil_turn("cl_neo_viewmodel_recoil_turn", "2", FCVAR_ARCHIVE,
	"Degrees the gun's nose turns toward the side a shot went (strength 1, a shot at the cone's edge).", true, 0, true, 10);
ConVar cl_neo_viewmodel_recoil_tilt("cl_neo_viewmodel_recoil_tilt", "10", FCVAR_ARCHIVE,
	"Degrees the gun tilts (rolls) toward the side a shot went, at its peak, for a shot at the cone's edge"
	" (one near the centre tilts less, as the square root of how far out it went).", true, 0, true, 45);
ConVar cl_neo_viewmodel_recoil_tilt_freq("cl_neo_viewmodel_recoil_tilt_freq", "4", FCVAR_ARCHIVE,
	"How fast the tilt eases back, in Hz: slower than the nose, so the gun rolls into the shot's side and the arms"
	" bring it level again (on the nose's own spring it flicked back too fast to see).", true, 1, true, 30);
ConVar cl_neo_viewmodel_recoil_tilt_hip("cl_neo_viewmodel_recoil_tilt_hip", "2", FCVAR_ARCHIVE,
	"How many times stronger the tilt is at the hip than on the sights (blended as the gun comes up; the classic"
	" aim with ironsights off counts as aimed).", true, 0, true, 5);
ConVar cl_neo_viewmodel_recoil_rof_ref("cl_neo_viewmodel_recoil_rof_ref", "0.15", FCVAR_ARCHIVE,
	"Guns whose cycle time (their fastest rate of fire) is below this, in seconds, get each shot's sideways turn and"
	" tilt shrunk in proportion (down to 30%), so fast automatics wobble rather than shake: the random left and right"
	" kicks otherwise pile up.", true, 0.01f, true, 1);
static constexpr float SIDEWAYS_MIN_SCALE = 0.3f;
static constexpr float TILT_DAMPING = 0.6f;
// An impulse on a spring damped this much peaks at about half of v0 / w, not all of it (the knock's other
// springs, less damped, reach near it); the tilt's kick is divided by this so cl_neo_viewmodel_recoil_tilt is
// the peak in degrees for a shot at the cone's edge.
static constexpr float TILT_PEAK = 0.5f;
ConVar cl_neo_viewmodel_recoil_tilt_pivot("cl_neo_viewmodel_recoil_tilt_pivot", "4", FCVAR_ARCHIVE,
	"How far below the gun's origin (near the eye's line through the sights) the tilt turns it, in units: through"
	" the gun's body it twists on itself; 0 swings it under the sight line like a pendulum, hard to see.",
	true, 0, true, 20);
ConVar cl_neo_viewmodel_recoil_debug("cl_neo_viewmodel_recoil_debug", "0", FCVAR_NONE,
	"Debug: print each shot's place in its cone and the knock's tilt, and the tilt's peak after it.");
static float s_flTiltPeak = 0.0f;
ConVar cl_neo_viewmodel_recoil_side_shift("cl_neo_viewmodel_recoil_side_shift", "0", FCVAR_ARCHIVE,
	"Units the whole gun slides toward the side a shot went (the old sideways shift; 0.35 before the turn and tilt).",
	true, 0, true, 2);

// Peak movement for a shot of strength 1: degrees of nose rise, and units back and up.
static constexpr float KICK_PITCH = 1.6f;
static constexpr float KICK_BACK = 0.9f;
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
	Spring3 rotation;	// pitch, yaw (degrees; its roll unused)
	Spring3 tilt;		// roll (degrees), in x
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
	// Where the shot went within its cone, -1..1 each way; the turn and tilt use it as it is (less on a gun that
	// cycles fast, by its cycle time: see cl_neo_viewmodel_recoil_rof_ref), the rest scaled.
	const float rateScale = clamp(pWeapon->GetFireRate() / cl_neo_viewmodel_recoil_rof_ref.GetFloat(), SIDEWAYS_MIN_SCALE, 1.0f);
	const float rawSide = clamp(conePosition.x, -1.0f, 1.0f);
	const float coneSide = rawSide * rateScale;
	const float side = coneSide * lateral;
	const float lift = clamp(conePosition.y, -1.0f, 1.0f) * lateral;

	// An impulse v0 on a spring peaks near v0 / w, so each kick is its peak times w.
	const float omega = 2.0f * M_PI_F * cl_neo_viewmodel_recoil_freq.GetFloat();
	const float omegaLinear = omega * LINEAR_FREQ_SCALE;
	// Nose up (negative pitch), turned toward the shot (right = negative yaw) and tilted with it.
	s_recoil.rotation.v += Vector(-(KICK_PITCH + 0.5f * KICK_PITCH * lift), -cl_neo_viewmodel_recoil_turn.GetFloat() * coneSide,
		0.0f) * (strength * omega);
	const float omegaTilt = 2.0f * M_PI_F * cl_neo_viewmodel_recoil_tilt_freq.GetFloat();
	// Most shots land near the cone's centre (about 0.3 out), where a proportional tilt was about a degree and
	// didn't read: the square root keeps the side and the edge shots biggest, but tilts the common ones clearly.
	const float tiltSide = ((rawSide < 0.0f) ? -sqrtf(-rawSide) : sqrtf(rawSide)) * rateScale;
	s_recoil.tilt.v.x += cl_neo_viewmodel_recoil_tilt.GetFloat() * tiltSide * strength * omegaTilt / TILT_PEAK;
	if (cl_neo_viewmodel_recoil_debug.GetBool())
	{
		Msg("[knock] cone %.2f %.2f  strength %.2f  rate scale %.2f  tilt now %.2f deg (peak since last shot %.2f)\n",
			conePosition.x, conePosition.y, strength, rateScale, s_recoil.tilt.x.x, s_flTiltPeak);
		s_flTiltPeak = 0.0f;
	}
	// The whole gun back toward the eye (and aside with the shot, if asked).
	s_recoil.linear.v += Vector(-KICK_BACK, cl_neo_viewmodel_recoil_side_shift.GetFloat() * side, KICK_UP * (1.0f + lift))
		* (strength * omegaLinear);
}

void NeoViewmodelRecoilRandomShot(C_NEOBaseCombatWeapon *pWeapon)
{
	if (!prediction->IsFirstTimePredicted())
	{
		return;
	}
	static CUniformRandomStream s_random;
	static bool s_bSeeded = false;
	if (!s_bSeeded)
	{
		s_random.SetSeed(static_cast<int>(Plat_FloatTime() * 1000.0) & 0x7fffffff);
		s_bSeeded = true;
	}
	const float angle = s_random.RandomFloat(0.0f, 2.0f * M_PI_F);
	const float distance = sqrtf(s_random.RandomFloat(0.0f, 1.0f));	// even over the disc
	NeoViewmodelRecoilShot(pWeapon, Vector2D(cosf(angle) * distance, sinf(angle) * distance));
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
			s_recoil.tilt.Step(dt, 2.0f * M_PI_F * cl_neo_viewmodel_recoil_tilt_freq.GetFloat(), TILT_DAMPING);
			s_recoil.linear.Step(dt, omega * LINEAR_FREQ_SCALE, LINEAR_DAMPING);
		}
	}

	const Vector &rot = s_recoil.rotation.x;
	const Vector &lin = s_recoil.linear.x;
	// Stronger at the hip, easing to its own size as the gun comes onto the sights (or the classic aim).
	auto *pNeoOwner = ToNEOPlayer(pOwner);
	const float aimed = (pNeoOwner && pNeoOwner->IsInAim() && ironsightBlend <= 0.0f) ? 1.0f : ironsightBlend;
	const float roll = s_recoil.tilt.x.x * Lerp(aimed, cl_neo_viewmodel_recoil_tilt_hip.GetFloat(), 1.0f);
	s_flTiltPeak = Max(s_flTiltPeak, fabsf(roll));
	if (rot.LengthSqr() + lin.LengthSqr() + roll * roll < 1e-8f)
	{
		return;
	}
	Vector forward, right, up;
	AngleVectors(eyeAngles, &forward, &right, &up);
	origin += forward * lin.x + right * lin.y + up * lin.z;

	// Braced on the sights: pitch and yaw only.
	const float braced = 1.0f - (1.0f - BRACED_ROTATION) * ironsightBlend;
	matrix3x4_t base, local, turned;
	AngleMatrix(angles, base);
	AngleMatrix(QAngle(rot.x * braced, rot.y * braced, 0.0f), local);
	ConcatTransforms(base, local, turned);

	// The tilt: a roll around the gun's own length, through a point pivot units below its origin (the origin sits
	// near the eye's line through the sights, the gun below it), so the gun twists on its body: its top goes one
	// way and its bottom the other. Around the origin itself it only swung under the sight line.
	const float pivot = cl_neo_viewmodel_recoil_tilt_pivot.GetFloat();
	matrix3x4_t tilt, result;
	AngleMatrix(QAngle(0.0f, 0.0f, roll), tilt);
	ConcatTransforms(turned, tilt, result);
	Vector gunUp, originFromPivot;
	MatrixGetColumn(turned, 2, gunUp);
	VectorRotate(Vector(0.0f, 0.0f, pivot), result, originFromPivot);
	origin = origin - gunUp * pivot + originFromPivot;
	MatrixAngles(result, angles);
}
