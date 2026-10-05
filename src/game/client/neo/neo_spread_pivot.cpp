#include "cbase.h"
#include "neo_spread_pivot.h"
#include "neo_view_shots.h"
#include "neo_viewmodel_recoil.h"
#include "weapon_neobasecombatweapon.h"
#include "c_neo_player.h"
#include "usercmd.h"
#include "prediction.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "mathlib/vector2d.h"
#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_spread_pivot_scale("cl_neo_spread_pivot_scale", "1", FCVAR_CHEAT | FCVAR_HIDDEN,
	"How far the gun turns toward each shot: 1 = the barrel points where the bullet went.", true, 0, true, 4);
ConVar cl_neo_spread_pivot_time("cl_neo_spread_pivot_time", "0.03", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Seconds the gun takes to turn toward each shot.", true, 0.005f, true, 1);
ConVar cl_neo_spread_pivot_return("cl_neo_spread_pivot_return", "0.2", FCVAR_CHEAT | FCVAR_HIDDEN,
	"Seconds the gun takes to ease back to centre once the shooting stops.", true, 0.005f, true, 2);

static struct
{
	const C_NEOBaseCombatWeapon *pWeapon = nullptr;
	Vector lastShot; // the last shot's direction in the world
	QAngle lastShotEyes; // the eye angles it was fired from, without the punch
	float lastShotTime = -100.0f; // realtime: shots are recorded during prediction, when curtime is the tickbase's
	Vector2D current; // on a critically damped spring toward the target
	Vector2D velocity;
	int updatedFrame = -1;
} s_pivot;

// The spread offset of a command's shot'th bullet, in tangents along the eye's right and up: as FireBullets seeds it
// and CShotManipulator::ApplySpread draws it, from a private stream so the game's own random numbers are untouched.
static Vector2D SpreadOffset(int randomSeed, int shot, const Vector &spread)
{
	static ConVarRef ai_shot_bias_max("ai_shot_bias_max");
	const float shotBias = ai_shot_bias_max.GetFloat();
	const float flatness = fabsf(shotBias) * 0.5f;
	CUniformRandomStream stream;
	stream.SetSeed((randomSeed & 255) + shot);
	float x, y;
	do
	{
		x = stream.RandomFloat(-1, 1) * flatness + stream.RandomFloat(-1, 1) * (1 - flatness);
		y = stream.RandomFloat(-1, 1) * flatness + stream.RandomFloat(-1, 1) * (1 - flatness);
		if (shotBias < 0)
		{
			x = (x >= 0) ? 1.0f - x : -1.0f - x;
			y = (y >= 0) ? 1.0f - y : -1.0f - y;
		}
	} while (x * x + y * y > 1);
	return Vector2D(x * spread.x, y * spread.y);
}

// Kept as a world direction: the view kick after the shot raises the view, and the gun stays on where the bullet went.
static void SetLastShot(C_NEOBaseCombatWeapon *pWeapon, const Vector &direction)
{
	s_pivot.pWeapon = pWeapon;
	s_pivot.lastShot = direction;
	s_pivot.lastShotTime = gpGlobals->realtime;
	C_BasePlayer *pOwner = pWeapon ? ToBasePlayer(pWeapon->GetOwner()) : nullptr;
	s_pivot.lastShotEyes = pOwner ? pOwner->EyeAngles() : vec3_angle;
}

void NeoSpreadPivotShot(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &aim, const Vector &spread,
	int shots)
{
	if (!prediction->IsFirstTimePredicted() || shots <= 0)
	{
		return;
	}
	const Vector2D offset = SpreadOffset(cmd.random_seed, shots - 1, spread);
	Vector right, up;
	VectorVectors(aim, right, up);
	SetLastShot(pWeapon, aim + right * offset.x + up * offset.y);
	NeoViewmodelRecoilShot(pWeapon, Vector2D(offset.x / Max(spread.x, 0.0001f), offset.y / Max(spread.y, 0.0001f)));
}

void NeoSpreadPivotWatchedShot(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner, const Vector &direction)
{
	s_pivot.pWeapon = pWeapon;
	s_pivot.lastShot = direction;
	s_pivot.lastShotTime = gpGlobals->realtime;
	s_pivot.lastShotEyes = pOwner->EyeAngles();
}

void NeoSpreadPivotPellets(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &aim, const Vector &spread,
	int pellets)
{
	if (!prediction->IsFirstTimePredicted() || pellets <= 0)
	{
		return;
	}
	const int count = Min(pellets, NEO_SPREAD_PIVOT_MAX_PELLETS);
	Vector right, up;
	VectorVectors(aim, right, up);
	Vector2D centre(0.0f, 0.0f);
	for (int i = 0; i < count; ++i)
	{
		// FireBullets reseeds each pellet with the command's seed plus its index.
		centre += SpreadOffset(cmd.random_seed, i, spread) / count;
	}
	SetLastShot(pWeapon, aim + right * centre.x + up * centre.y);

	// The centre of n pellets strays about 1/sqrt(n) as far as one does: scaled back up, the knock has a single
	// shot's range.
	Vector2D knock(centre.x / Max(spread.x, 0.0001f), centre.y / Max(spread.y, 0.0001f));
	knock *= sqrtf(static_cast<float>(count));
	const float length = knock.Length();
	if (length > 1.0f)
	{
		knock /= length;
	}
	NeoViewmodelRecoilPelletShot(pWeapon, knock);
}

// Where the gun should point now, as a spread offset: toward the last shot for about a shot's time, then back.
static Vector2D PivotTarget(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner)
{
	const float hold = Max(pWeapon->GetFireRate() * 1.5f, 0.1f);
	if (pWeapon != s_pivot.pWeapon || gpGlobals->realtime - s_pivot.lastShotTime >= hold)
	{
		return Vector2D(0.0f, 0.0f);
	}
	// From the eyes it was fired from plus the punch as it is now, so the gun stays on the shot while the punch lifts
	// the view.
	Vector forward, right, up;
	AngleVectors(s_pivot.lastShotEyes + pOwner->GetPunchAngle(), &forward, &right, &up);
	const float ahead = s_pivot.lastShot.Dot(forward);
	if (ahead <= 0.1f)
	{
		return Vector2D(0.0f, 0.0f);
	}
	return Vector2D(s_pivot.lastShot.Dot(right) / ahead, s_pivot.lastShot.Dot(up) / ahead);
}

void NeoSpreadPivotApply(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner, bool bFlipped, QAngle &angles)
{
	// Not while predicting commands: frametime is a tick then, and the frame's own pass follows.
	if (!pWeapon || !pOwner || pOwner != NeoViewPlayer() || prediction->InPrediction())
	{
		return;
	}
	if (s_pivot.updatedFrame != gpGlobals->framecount)
	{
		s_pivot.updatedFrame = gpGlobals->framecount;
		const Vector2D target = (NeoGunMotion() == NEO_GUN_MOTION_FULL) ? PivotTarget(pWeapon, pOwner)
			: Vector2D(0.0f, 0.0f);
		// A critically damped spring, quick to each shot and slower home.
		const bool bReturning = target.LengthSqr() < 1e-12f;
		const float omega = 1.0f / (bReturning ? cl_neo_spread_pivot_return : cl_neo_spread_pivot_time).GetFloat();
		constexpr float STEP = 1.0f / 240.0f;
		for (float left = Min(gpGlobals->frametime, 0.1f); left > 0.0f; left -= STEP)
		{
			const float dt = Min(left, STEP);
			const Vector2D accel = (target - s_pivot.current) * (omega * omega) - s_pivot.velocity * (2.0f * omega);
			s_pivot.velocity += accel * dt;
			s_pivot.current += s_pivot.velocity * dt;
		}
	}

	const Vector2D offset = s_pivot.current * cl_neo_spread_pivot_scale.GetFloat();
	if (offset.LengthSqr() < 1e-10f)
	{
		return;
	}
	// A direction at tangent t in the main view shows where one at t / fovScale does in the viewmodel's, so the
	// barrel turned by that points where the bullet goes on screen.
	const CViewSetup *pView = view->GetPlayerViewSetup();
	const float fovScale = pView ? tanf(DEG2RAD(pView->fov * 0.5f)) / tanf(DEG2RAD(pView->fovViewmodel * 0.5f)) : 1.0f;
	const float yaw = -RAD2DEG(atanf(offset.x / fovScale));
	const QAngle turn(-RAD2DEG(atanf(offset.y / fovScale)), bFlipped ? -yaw : yaw, 0.0f);
	matrix3x4_t base, local, result;
	AngleMatrix(angles, base);
	AngleMatrix(turn, local);
	ConcatTransforms(base, local, result);
	MatrixAngles(result, angles);
}
