#include "cbase.h"
#include "neo_viewmodel_recoil.h"
#include "neo_view_shots.h"
#include "neo_spectator_hits.h"
#include "neo_spread_pivot.h"
#include "weapon_neobasecombatweapon.h"
#include "c_neo_player.h"
#include "prediction.h"
#include "mathlib/vector2d.h"
#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gun_motion("cl_neo_gun_motion", "0", FCVAR_ARCHIVE,
	"The gun moves with each shot (cosmetic). 0 = off, 1 = subtle (a lighter knock), 2 = full (the knock, and the gun"
	" turns toward where each shot went).", true, 0, true, 2);

ConVar cl_neo_viewmodel_recoil("cl_neo_viewmodel_recoil", "1", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Strength of each shot's knock at Gun motion Full (Subtle is half of it).", true, 0, true, 4);
ConVar cl_neo_viewmodel_recoil_freq("cl_neo_viewmodel_recoil_freq", "7", FCVAR_CHEAT | FCVAR_HIDDEN,
	"How fast the gun's nose springs back, in Hz (the whole gun follows at 60% of this).", true, 2, true, 30);
ConVar cl_neo_viewmodel_recoil_damping("cl_neo_viewmodel_recoil_damping", "0.27", FCVAR_CHEAT | FCVAR_HIDDEN,
	"How much the nose sways before settling: 1 = no overshoot, lower sways more.", true, 0.1f, true, 1.5f);
ConVar cl_neo_viewmodel_recoil_lift("cl_neo_viewmodel_recoil_lift", "0.2", FCVAR_CHEAT | FCVAR_HIDDEN,
	"How much a shot that went high or low in its cone adds to or takes from the kick up.", true, 0, true, 4);
ConVar cl_neo_viewmodel_recoil_turn("cl_neo_viewmodel_recoil_turn", "2", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Degrees the nose turns toward the side a shot went, for a shot at the cone's edge.", true, 0, true, 10);
ConVar cl_neo_viewmodel_recoil_tilt("cl_neo_viewmodel_recoil_tilt", "10", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Degrees the gun rolls toward the side a shot went, at its peak, for a shot at the cone's edge.", true, 0, true, 45);
ConVar cl_neo_viewmodel_recoil_tilt_freq("cl_neo_viewmodel_recoil_tilt_freq", "4", FCVAR_CHEAT | FCVAR_HIDDEN,
	"How fast the roll eases back, in Hz.", true, 1, true, 30);
ConVar cl_neo_viewmodel_recoil_tilt_hip("cl_neo_viewmodel_recoil_tilt_hip", "2", FCVAR_CHEAT | FCVAR_HIDDEN,
	"How many times stronger the roll is at the hip than when aiming.", true, 0, true, 5);
ConVar cl_neo_viewmodel_recoil_tilt_pivot("cl_neo_viewmodel_recoil_tilt_pivot", "4", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Units below the gun's origin that it rolls around, so it twists on its body.", true, 0, true, 20);
ConVar cl_neo_viewmodel_recoil_rof_ref("cl_neo_viewmodel_recoil_rof_ref", "0.15", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Guns cycling faster than this, in seconds, get each shot's turn and roll shrunk in proportion (down to 30%), so"
	" fast automatics wobble rather than shake.", true, 0.01f, true, 1);
ConVar cl_neo_viewmodel_recoil_heavy_cycle("cl_neo_viewmodel_recoil_heavy_cycle", "0.8", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Guns cycling this slowly or slower, in seconds, and the shotguns knock with the heavy settings; faster ones blend"
	" toward the automatics'.", true, 0.15f, true, 3);
ConVar cl_neo_viewmodel_recoil_heavy_strength("cl_neo_viewmodel_recoil_heavy_strength", "2.5", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Heavy guns: how many times harder they knock.", true, 0, true, 6);
ConVar cl_neo_viewmodel_recoil_heavy_freq("cl_neo_viewmodel_recoil_heavy_freq", "2", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Heavy guns: how fast the nose springs back, in Hz.", true, 0.5f, true, 30);
ConVar cl_neo_viewmodel_recoil_heavy_damping("cl_neo_viewmodel_recoil_heavy_damping", "0.9", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Heavy guns: how damped the nose is (1 = no overshoot).", true, 0.1f, true, 1);
ConVar cl_neo_viewmodel_recoil_pellet_side("cl_neo_viewmodel_recoil_pellet_side", "0.5", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Shotguns: the share of the sideways turn and roll they get.", true, 0, true, 1);

NeoGunMotionLevel NeoGunMotion()
{
	return static_cast<NeoGunMotionLevel>(cl_neo_gun_motion.GetInt());
}

static constexpr float SUBTLE_SCALE = 0.5f;
static constexpr float SIDEWAYS_MIN_SCALE = 0.3f;
static constexpr float LIGHT_CYCLE = 0.1f;
static constexpr float MAX_STRENGTH = 3.0f;
static constexpr float MAX_SIDEWAYS_STRENGTH = 1.0f;
static constexpr float TILT_DAMPING = 0.6f;
static constexpr float TILT_PEAK = 0.5f; // of v0 / w, that an impulse on the tilt's spring reaches

// Peak movement for a shot of strength 1: degrees of nose rise, and units back and up.
static constexpr float KICK_PITCH = 1.6f;
static constexpr float KICK_BACK = 0.9f;
static constexpr float KICK_UP = 0.15f;
static constexpr float LINEAR_FREQ_SCALE = 0.6f; // the whole gun lags the nose
static constexpr float LINEAR_DAMPING = 0.65f;

// The fraction of v0 / w that an impulse v0 on a spring of damping zeta reaches at its peak.
static float SpringPeakFraction(float zeta)
{
	zeta = clamp(zeta, 0.01f, 0.999f);
	const float s = sqrtf(1.0f - zeta * zeta);
	return expf(-zeta / s * atan2f(s, zeta));
}

// One damped spring per axis: x'' = -w^2 x - 2 z w x'.
struct NeoRecoilSpring
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
	NeoRecoilSpring rotation; // pitch, yaw (degrees)
	NeoRecoilSpring tilt; // roll (degrees), in x
	NeoRecoilSpring linear; // forward, right, up (units)
	float heaviness = 0.0f; // the last shot's gun, 0 an automatic to 1 heavy
	int updatedFrame = -1;
} s_recoil;

// A shot's strength from the gun's own recoil numbers, compressed so a sniper doesn't throw the gun off screen.
static float ShotStrength(C_NEOBaseCombatWeapon *pWeapon, bool bAimed)
{
	const WeaponHandlingInfo_t &handling = pWeapon->GetWeaponHandling();
	const WeaponKickInfo_t &kick = handling.kickInfo;
	const WeaponRecoilInfo_t &recoil = handling.recoilInfo;
	const float kickSize = Max(fabsf(kick.minX), fabsf(kick.maxX)) + 0.5f * Max(fabsf(kick.minY), fabsf(kick.maxY));
	const float recoilSize = (Max(fabsf(recoil.minX), fabsf(recoil.maxX)) + 0.5f * Max(fabsf(recoil.minY), fabsf(recoil.maxY)))
		* (bAimed ? recoil.adsFactor : recoil.hipFactor);
	return clamp(sqrtf(Max(kickSize, recoilSize)), 0.7f, 2.0f);
}

// The weapons' recoil numbers are much alike, so on one spring every gun knocked the same. The slower a gun cycles,
// the heavier its knock, 0 (an automatic) to 1, on a log scale; the shotguns are heavy whatever their cycle.
static float Heaviness(C_NEOBaseCombatWeapon *pWeapon, bool bPellets)
{
	if (bPellets)
	{
		return 1.0f;
	}
	const float heavyCycle = Max(cl_neo_viewmodel_recoil_heavy_cycle.GetFloat(), LIGHT_CYCLE * 1.01f);
	return clamp(logf(Max(pWeapon->GetFireRate(), 0.001f) / LIGHT_CYCLE) / logf(heavyCycle / LIGHT_CYCLE), 0.0f, 1.0f);
}

static void NoseSpring(float heaviness, float &freq, float &zeta)
{
	const float light = cl_neo_viewmodel_recoil_freq.GetFloat();
	freq = light * powf(cl_neo_viewmodel_recoil_heavy_freq.GetFloat() / light, heaviness);
	zeta = Lerp(heaviness, cl_neo_viewmodel_recoil_damping.GetFloat(), cl_neo_viewmodel_recoil_heavy_damping.GetFloat());
}

static float KnockScale()
{
	switch (NeoGunMotion())
	{
	case NEO_GUN_MOTION_SUBTLE: return SUBTLE_SCALE * cl_neo_viewmodel_recoil.GetFloat();
	case NEO_GUN_MOTION_FULL: return cl_neo_viewmodel_recoil.GetFloat();
	default: return 0.0f;
	}
}

static void KnockShot(C_NEOBaseCombatWeapon *pWeapon, const Vector2D &conePosition, bool bPellets)
{
	const float scale = KnockScale();
	auto *pOwner = pWeapon ? ToNEOPlayer(pWeapon->GetOwner()) : nullptr;
	if (scale <= 0.0f || !pOwner)
	{
		return;
	}
	const float heaviness = Heaviness(pWeapon, bPellets);
	s_recoil.heaviness = heaviness;
	const float baseStrength = ShotStrength(pWeapon, pOwner->IsInAim());
	const float strength = Min(baseStrength * Lerp(heaviness, 1.0f, cl_neo_viewmodel_recoil_heavy_strength.GetFloat()),
		MAX_STRENGTH) * scale;
	const float sideStrength = Min(baseStrength, MAX_SIDEWAYS_STRENGTH) * scale;
	// Fast automatics and shotguns turn and roll less, or the random left and right kicks pile up.
	const float rateScale = clamp(pWeapon->GetFireRate() / cl_neo_viewmodel_recoil_rof_ref.GetFloat(), SIDEWAYS_MIN_SCALE, 1.0f)
		* (bPellets ? cl_neo_viewmodel_recoil_pellet_side.GetFloat() : 1.0f);
	const float rawSide = clamp(conePosition.x, -1.0f, 1.0f);
	const float coneSide = rawSide * rateScale;
	const float lift = clamp(conePosition.y, -1.0f, 1.0f) * cl_neo_viewmodel_recoil_lift.GetFloat();

	// An impulse v0 on a spring peaks near v0 / w, so each kick is its peak times w. The more damped heavy spring
	// would peak lower, so its kick is sized up to keep the peak.
	float freq, zeta;
	NoseSpring(heaviness, freq, zeta);
	const float omega = 2.0f * M_PI_F * freq;
	const float omegaLinear = omega * LINEAR_FREQ_SCALE;
	const float lightPeak = SpringPeakFraction(cl_neo_viewmodel_recoil_damping.GetFloat());
	const float peakSize = Lerp(heaviness, lightPeak, 1.0f) / SpringPeakFraction(zeta);
	// Nose up (negative pitch) and turned toward the shot (right is negative yaw).
	s_recoil.rotation.v += Vector(-(KICK_PITCH + 0.5f * KICK_PITCH * lift) * strength,
		-cl_neo_viewmodel_recoil_turn.GetFloat() * coneSide * sideStrength, 0.0f) * (omega * peakSize);
	// Most shots land near the cone's centre, where a proportional roll is too small to see: the square root keeps the
	// edge shots biggest but rolls the common ones clearly.
	const float omegaTilt = 2.0f * M_PI_F * cl_neo_viewmodel_recoil_tilt_freq.GetFloat();
	const float tiltSide = ((rawSide < 0.0f) ? -sqrtf(-rawSide) : sqrtf(rawSide)) * rateScale;
	s_recoil.tilt.v.x += cl_neo_viewmodel_recoil_tilt.GetFloat() * tiltSide * sideStrength * omegaTilt / TILT_PEAK;
	s_recoil.linear.v += Vector(-KICK_BACK, 0.0f, KICK_UP * (1.0f + lift)) * (strength * omegaLinear);
}

void NeoViewmodelRecoilShot(C_NEOBaseCombatWeapon *pWeapon, const Vector2D &conePosition)
{
	KnockShot(pWeapon, conePosition, false);
}

void NeoViewmodelRecoilPelletShot(C_NEOBaseCombatWeapon *pWeapon, const Vector2D &conePosition)
{
	KnockShot(pWeapon, conePosition, true);
}

// A knock toward a random point of the cone, from a private random stream so the game's own numbers are untouched.
static void RandomKnock(C_NEOBaseCombatWeapon *pWeapon, bool bPellets)
{
	static CUniformRandomStream s_random;
	static bool s_bSeeded = false;
	if (!s_bSeeded)
	{
		s_random.SetSeed(static_cast<int>(Plat_FloatTime() * 1000.0) & 0x7fffffff);
		s_bSeeded = true;
	}
	const float angle = s_random.RandomFloat(0.0f, 2.0f * M_PI_F);
	const float distance = sqrtf(s_random.RandomFloat(0.0f, 1.0f));
	KnockShot(pWeapon, Vector2D(cosf(angle) * distance, sinf(angle) * distance), bPellets);
}

// A watched shot's knock and turn toward the directions its hits went (one, or a shotgun's pellets), from the watched
// player's aim. The pellets' centre is scaled up as the local player's is (NeoSpreadPivotPellets).
static void KnockToward(C_NEO_Player *pOwner, C_NEOBaseCombatWeapon *pWeapon, const Vector *pDirections, int count,
	bool bPellets)
{
	Vector forward, right, up;
	AngleVectors(pOwner->EyeAngles(), &forward, &right, &up);
	const Vector &spread = pWeapon->GetBulletSpread();
	Vector2D centre(0.0f, 0.0f);
	Vector mean(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < count; ++i)
	{
		const float ahead = Max(pDirections[i].Dot(forward), 0.1f);
		centre += Vector2D(pDirections[i].Dot(right) / ahead, pDirections[i].Dot(up) / ahead) / count;
		mean += pDirections[i] / count;
	}
	Vector2D knock(centre.x / Max(spread.x, 0.0001f), centre.y / Max(spread.y, 0.0001f));
	if (bPellets)
	{
		knock *= sqrtf(static_cast<float>(count));
	}
	const float length = knock.Length();
	if (length > 1.0f)
	{
		knock /= length;
	}
	KnockShot(pWeapon, knock, bPellets);
	NeoSpreadPivotWatchedShot(pWeapon, pOwner, mean.Normalized());
}

// A player watched in first person: their shots aren't predicted here, but the server's impacts show where they hit.
// Each shot waits a moment for its hits and knocks toward them; one that hit nothing knocks toward a random point of
// the cone. A new view player starts still.
static void WatchSpectatedShots()
{
	static constexpr float HIT_WAIT = 0.08f;
	static constexpr int MAX_PENDING = 3;
	static int s_iCount = 0;
	static int s_iPending = 0;
	static float s_flPendingSince = 0.0f;
	static float s_flViewChanged = -1.0f;
	const NeoViewShots &shots = NeoGetViewShots();
	if (shots.viewChanged != s_flViewChanged)
	{
		s_flViewChanged = shots.viewChanged;
		s_iCount = shots.count;
		s_iPending = 0;
		s_recoil.rotation = s_recoil.tilt = s_recoil.linear = NeoRecoilSpring();
		return;
	}
	C_NEO_Player *pPlayer = shots.hPlayer.Get();
	C_NEOBaseCombatWeapon *pWeapon = shots.hWeapon.Get();
	if (!shots.bSpectating || !pWeapon || !pPlayer)
	{
		s_iCount = shots.count;
		s_iPending = 0;
		return;
	}
	if (shots.count != s_iCount)
	{
		if (s_iPending == 0)
		{
			s_flPendingSince = gpGlobals->realtime;
		}
		s_iPending = Min(s_iPending + (shots.count - s_iCount), MAX_PENDING);
		s_iCount = shots.count;
	}
	if (s_iPending <= 0)
	{
		return;
	}
	const bool bPellets = (pWeapon->GetNeoWepBits() & (NEO_WEP_SUPA7 | NEO_WEP_AA13)) != 0;
	Vector directions[NEO_SPREAD_PIVOT_MAX_PELLETS];
	const int hits = NeoTakeSpectatorHits(directions, ARRAYSIZE(directions));
	if (hits > 0)
	{
		if (bPellets)
		{
			KnockToward(pPlayer, pWeapon, directions, hits, true);
		}
		else
		{
			// A hit a shot, oldest first.
			for (int i = 0; i < Min(hits, s_iPending); ++i)
			{
				KnockToward(pPlayer, pWeapon, &directions[i], 1, false);
			}
		}
		s_iPending = 0;
	}
	else if (gpGlobals->realtime - s_flPendingSince > HIT_WAIT)
	{
		for (int i = 0; i < s_iPending; ++i)
		{
			RandomKnock(pWeapon, bPellets);
		}
		s_iPending = 0;
	}
}

void NeoViewmodelRecoilApply(C_BasePlayer *pOwner, const QAngle &eyeAngles, bool bFlipped, Vector &origin,
	QAngle &angles)
{
	if (!pOwner || pOwner != NeoViewPlayer() || prediction->InPrediction())
	{
		return;
	}
	if (s_recoil.updatedFrame != gpGlobals->framecount)
	{
		s_recoil.updatedFrame = gpGlobals->framecount;
		WatchSpectatedShots();
		float freq, zeta;
		NoseSpring(s_recoil.heaviness, freq, zeta);
		const float omega = 2.0f * M_PI_F * freq;
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
	auto *pNeoOwner = ToNEOPlayer(pOwner);
	const bool bAimed = pNeoOwner && pNeoOwner->IsInAim();
	const float side = bFlipped ? -1.0f : 1.0f;
	const float roll = side * s_recoil.tilt.x.x * (bAimed ? 1.0f : cl_neo_viewmodel_recoil_tilt_hip.GetFloat());
	if (rot.LengthSqr() + lin.LengthSqr() + roll * roll < 1e-8f)
	{
		return;
	}
	Vector forward, right, up;
	AngleVectors(eyeAngles, &forward, &right, &up);
	origin += forward * lin.x + right * lin.y + up * lin.z;

	matrix3x4_t base, local, turned;
	AngleMatrix(angles, base);
	AngleMatrix(QAngle(rot.x, side * rot.y, 0.0f), local);
	ConcatTransforms(base, local, turned);

	// The roll turns the gun around its own length through a point below its origin (which sits near the eye's line),
	// so it twists on its body instead of swinging under the sight line.
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
