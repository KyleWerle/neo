#include "cbase.h"
#include "neo_spread_pivot.h"
#include "neo_ironsight_lens.h"
#include "neo_viewmodel_recoil.h"
#include "neo_gunplay_marks.h"
#include "neo_gunplay_shots.h"
#include "neo_ironsights.h"
#include "weapon_neobasecombatweapon.h"
#include "c_neo_player.h"
#include "usercmd.h"
#include "prediction.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "engine/ivdebugoverlay.h"
#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Follow only: each shot turns the gun toward where that shot went. The lead mode (pointing at where the next shot
// would go, from its seed) is gone: it revealed the next shot, and could be abused (Kyle, 2026-09-29).
ConVar cl_neo_spread_pivot("cl_neo_spread_pivot", "1", FCVAR_ARCHIVE,
	"With Enable Gunplay: each shot turns the gun toward where that shot went. 0 = off.", true, 0, true, 1);
ConVar cl_neo_spread_pivot_scale("cl_neo_spread_pivot_scale", "1", FCVAR_ARCHIVE,
	"How far the gun turns: 1 = the barrel points exactly where the bullet goes, more exaggerates.",
	true, 0, true, 4);
ConVar cl_neo_spread_pivot_time("cl_neo_spread_pivot_time", "0.03", FCVAR_ARCHIVE,
	"How quickly the gun moves to each shot's direction, in seconds (its spring's time constant).", true, 0.005f, true, 1);
ConVar cl_neo_spread_pivot_return("cl_neo_spread_pivot_return", "0.2", FCVAR_ARCHIVE,
	"How slowly the gun eases back to centre once the shooting stops (or the mag runs dry), in seconds.",
	true, 0.005f, true, 2);
ConVar cl_neo_spread_pivot_turn_hold("cl_neo_spread_pivot_turn_hold", "0", FCVAR_ARCHIVE,
	"How far, in degrees, turning away after a shot can leave the gun behind on where it went, eased in"
	" and never past this (0 = the gun turns with the view at once).", true, 0, true, 45);
ConVar cl_neo_spread_pivot_debug("cl_neo_spread_pivot_debug", "0", FCVAR_CHEAT,
	"Debug: mark in the world where the gun is turning to (where the last shot went, to check against its bullet"
	" hole).");

static struct
{
	const C_NEOBaseCombatWeapon *pWeapon = nullptr;
	Vector lastShot;			// the last shot's direction in the world (aim plus spread)
	QAngle lastShotEyes;		// the eye angles it was fired from (without the recoil's punch)
	float lastShotTime = -1.0f;
	Vector2D current;			// this frame's offset, on a critically damped spring toward the target
	Vector2D velocity;
	int updatedFrame = -1;
} s_pivot;

// The spread offset of a command's shot (the shot'th bullet it fires), in tangents along the eye's right and
// up: as FireBullets seeds it and CShotManipulator::ApplySpread draws it (bias 1, so the shot bias is
// ai_shot_bias_max), from a private stream so the game's own random numbers are untouched.
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

void NeoSpreadPivotShot(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &aim, const Vector &spread,
	int shots)
{
	if (cl_neo_spread_pivot_debug.GetBool())
	{
		const Vector2D offset = SpreadOffset(cmd.random_seed, Max(shots - 1, 0), spread);
		Msg("[pivot] %s: command %d, first %d, shots %d, cone %.4f %.4f, offset %.4f %.4f\n",
			pWeapon ? pWeapon->GetClassname() : "?", cmd.command_number, prediction->IsFirstTimePredicted(), shots,
			spread.x, spread.y, offset.x, offset.y);
	}
	if (!prediction->IsFirstTimePredicted() || shots <= 0)
	{
		return;
	}
	s_pivot.pWeapon = pWeapon;
	// Kept as a world direction, as ApplySpread builds it: the recoil that follows the shot moves the view, and the
	// gun should stay on where the bullet went, not on the same offset from the raised view.
	const Vector2D offset = SpreadOffset(cmd.random_seed, shots - 1, spread);
	Vector right, up;
	VectorVectors(aim, right, up);
	s_pivot.lastShot = aim + right * offset.x + up * offset.y;
	// The crosshair's impact mark for it, at the same place.
	NeoGunplayMarkShot(pWeapon, s_pivot.lastShot);
	s_pivot.lastShotTime = gpGlobals->curtime;
	C_BasePlayer *pOwner = pWeapon ? ToBasePlayer(pWeapon->GetOwner()) : nullptr;
	s_pivot.lastShotEyes = pOwner ? pOwner->EyeAngles() : QAngle(0.0f, 0.0f, 0.0f);
	// The same shot knocks the arms (cl_neo_viewmodel_recoil), toward where it went within its cone.
	NeoViewmodelRecoilShot(pWeapon, Vector2D(offset.x / Max(spread.x, 0.0001f), offset.y / Max(spread.y, 0.0001f)));
}

void NeoSpreadPivotWatchedShot(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner, const Vector &direction)
{
	s_pivot.pWeapon = pWeapon;
	s_pivot.lastShot = direction;
	s_pivot.lastShotTime = gpGlobals->curtime;
	s_pivot.lastShotEyes = pOwner->EyeAngles();
}

static NeoSpreadPattern s_pattern;

const NeoSpreadPattern &NeoSpreadPivotLastPattern()
{
	return s_pattern;
}

void NeoSpreadPivotPellets(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &aim, const Vector &spread,
	int pellets)
{
	if (!prediction->IsFirstTimePredicted() || pellets <= 0)
	{
		return;
	}
	Vector directions[NeoSpreadPattern::MAX_PELLETS];
	const int count = Min(pellets, NeoSpreadPattern::MAX_PELLETS);
	Vector right, up;
	VectorVectors(aim, right, up);
	Vector2D centre(0.0f, 0.0f);
	s_pattern.time = gpGlobals->realtime;
	s_pattern.fired = gpGlobals->curtime;
	s_pattern.pWeapon = pWeapon;
	s_pattern.count = count;
	for (int i = 0; i < count; ++i)
	{
		// FireBullets reseeds each pellet with the command's seed plus its index.
		const Vector2D offset = SpreadOffset(cmd.random_seed, i, spread);
		directions[i] = aim + right * offset.x + up * offset.y;
		centre += offset / count;
		s_pattern.cone[i].Init(offset.x / Max(spread.x, 0.0001f), offset.y / Max(spread.y, 0.0001f));
	}
	NeoGunplayMarkPellets(pWeapon, directions, count);

	// The gun turns toward the pattern's centre, as it does to a single shot.
	s_pivot.pWeapon = pWeapon;
	s_pivot.lastShot = aim + right * centre.x + up * centre.y;
	s_pivot.lastShotTime = gpGlobals->curtime;
	C_BasePlayer *pOwner = pWeapon ? ToBasePlayer(pWeapon->GetOwner()) : nullptr;
	s_pivot.lastShotEyes = pOwner ? pOwner->EyeAngles() : QAngle(0.0f, 0.0f, 0.0f);

	// And knocks that way. The centre of n pellets strays about 1/sqrt(n) as far as one does: scaled back up, the
	// knock has a single shot's range, its direction still the pellets'.
	Vector2D knock(centre.x / Max(spread.x, 0.0001f), centre.y / Max(spread.y, 0.0001f));
	knock *= sqrtf(static_cast<float>(count));
	const float length = knock.Length();
	if (length > 1.0f)
	{
		knock /= length;
	}
	NeoViewmodelRecoilPelletShot(pWeapon, knock);
}

// Where the gun should point now, as a spread offset.
static Vector2D PivotTarget(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner)
{
	if (pWeapon != s_pivot.pWeapon)
	{
		return Vector2D(0.0f, 0.0f);
	}
	// Toward the last shot, held for about a shot's time, then back. Measured from the eyes it was
	// fired from with the recoil's punch as it is now, so the gun stays on the shot while the recoil lifts the
	// view (the punch). Turning away since (the eyes) leaves it behind on where the shot went only up to
	// cl_neo_spread_pivot_turn_hold, eased in: measured from the eyes as they are now, a fast turn left the gun,
	// and a collimated dot riding it, stuck on the shot.
	const float hold = Max(pWeapon->GetFireRate() * 1.5f, 0.1f);
	if (gpGlobals->curtime - s_pivot.lastShotTime >= hold)
	{
		return Vector2D(0.0f, 0.0f);
	}
	// The shot as a spread offset from the view at angles.
	const auto offsetFrom = [&](const QAngle &angles, Vector2D &offset) {
		Vector forward, right, up;
		AngleVectors(angles, &forward, &right, &up);
		const float ahead = s_pivot.lastShot.Dot(forward);
		offset.Init(s_pivot.lastShot.Dot(right) / Max(ahead, 0.1f), s_pivot.lastShot.Dot(up) / Max(ahead, 0.1f));
		return ahead > 0.1f;
	};
	Vector2D fromShot, fromNow;
	if (!offsetFrom(s_pivot.lastShotEyes + pOwner->GetPunchAngle(), fromShot))
	{
		return Vector2D(0.0f, 0.0f);
	}
	if (!offsetFrom(pOwner->EyeAngles() + pOwner->GetPunchAngle(), fromNow))
	{
		return fromShot;
	}
	// The turn's pull, saturating smoothly at the hold: tanh eases in and never reaches past it.
	const float turnHold = tanf(DEG2RAD(cl_neo_spread_pivot_turn_hold.GetFloat()));
	const Vector2D pull = fromNow - fromShot;
	const float length = pull.Length();
	if (turnHold <= 0.0f || length < 1e-6f)
	{
		return fromShot;
	}
	return fromShot + pull * (turnHold * tanhf(length / turnHold) / length);
}

static void DrawDebugMarker(C_BasePlayer *pOwner, const Vector2D &offset)
{
	Vector forward, right, up;
	AngleVectors(pOwner->EyeAngles() + pOwner->GetPunchAngle(), &forward, &right, &up);
	const Vector dir = forward + right * offset.x + up * offset.y;
	trace_t tr;
	UTIL_TraceLine(pOwner->EyePosition(), pOwner->EyePosition() + dir * MAX_TRACE_LENGTH, MASK_SHOT, pOwner,
		COLLISION_GROUP_NONE, &tr);
	constexpr float SIZE = 2.0f;
	debugoverlay->AddLineOverlay(tr.endpos - right * SIZE, tr.endpos + right * SIZE, 255, 0, 255, true, 0.0f);
	debugoverlay->AddLineOverlay(tr.endpos - up * SIZE, tr.endpos + up * SIZE, 255, 0, 255, true, 0.0f);
}

void NeoSpreadPivotApply(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner, QAngle &angles)
{
	// The local player's gun, or a watched player's in first person (their shots from their impacts).
	if (!pWeapon || !pOwner || pOwner != NeoGunplayWatchShots().pPlayer)
	{
		return;
	}
	if (s_pivot.updatedFrame != gpGlobals->framecount)
	{
		s_pivot.updatedFrame = gpGlobals->framecount;
		const Vector2D target = (NeoGunplayEnabled() && cl_neo_spread_pivot.GetBool()) ? PivotTarget(pWeapon, pOwner)
			: Vector2D(0.0f, 0.0f);
		// A critically damped spring starts from rest, so every move eases in; quick to each shot, slower home.
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
		if (cl_neo_spread_pivot_debug.GetBool() && cl_neo_spread_pivot.GetBool())
		{
			DrawDebugMarker(pOwner, target);
		}
	}

	const Vector2D offset = s_pivot.current * cl_neo_spread_pivot_scale.GetFloat();
	if (offset.LengthSqr() < 1e-10f)
	{
		return;
	}
	// A direction at tangent t in the main view shows where one at t / fovScale does in the viewmodel's, so
	// the barrel turned by that points where the bullet goes on screen.
	const CViewSetup *pView = view->GetPlayerViewSetup();
	const float fovScale = pView ? NeoIronsightFovScale(*pView) : 1.0f;
	const QAngle turn(-RAD2DEG(atanf(offset.y / fovScale)), -RAD2DEG(atanf(offset.x / fovScale)), 0.0f);
	matrix3x4_t base, local, result;
	AngleMatrix(angles, base);
	AngleMatrix(turn, local);
	ConcatTransforms(base, local, result);
	MatrixAngles(result, angles);
}
