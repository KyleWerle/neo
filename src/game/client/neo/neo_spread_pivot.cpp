#include "cbase.h"
#include "neo_spread_pivot.h"
#include "neo_ironsight_lens.h"
#include "weapon_neobasecombatweapon.h"
#include "c_neo_player.h"
#include "checksum_md5.h"
#include "usercmd.h"
#include "prediction.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "engine/ivdebugoverlay.h"
#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_spread_pivot("cl_neo_spread_pivot", "0", FCVAR_ARCHIVE,
	"Prototype: the gun turns toward where its bullets go. 0 = off; 1 = lead: while the trigger is held, it"
	" points at where the next shot will go; 2 = follow: each shot kicks it toward where that shot went.",
	true, 0, true, 2);
ConVar cl_neo_spread_pivot_scale("cl_neo_spread_pivot_scale", "1", FCVAR_ARCHIVE,
	"How far the gun turns: 1 = the barrel points exactly where the bullet goes, more exaggerates.",
	true, 0, true, 4);
ConVar cl_neo_spread_pivot_time("cl_neo_spread_pivot_time", "0.03", FCVAR_ARCHIVE,
	"Seconds the gun takes to settle on a new direction (the time constant of its easing).", true, 0.001f, true, 1);
ConVar cl_neo_spread_pivot_debug("cl_neo_spread_pivot_debug", "0", FCVAR_CHEAT,
	"Debug: mark in the world where the gun is turning to (lead: where the next shot will land, to check against"
	" its bullet hole).");

static struct
{
	const C_NEOBaseCombatWeapon *pWeapon = nullptr;
	bool bKnowsCommands = false;
	int commandMinusTick = 0;	// a command's number minus the tick it runs on (they advance together)
	Vector2D lastShot;			// the last shot's spread offset, in tangents along the eye's right and up
	float lastShotTime = -1.0f;
	Vector2D current;			// this frame's eased offset
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

void NeoSpreadPivotShot(C_NEOBaseCombatWeapon *pWeapon, const CUserCmd &cmd, const Vector &spread, int shots)
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
	s_pivot.bKnowsCommands = true;
	s_pivot.commandMinusTick = cmd.command_number - TIME_TO_TICKS(gpGlobals->curtime);
	s_pivot.lastShot = SpreadOffset(cmd.random_seed, shots - 1, spread);
	s_pivot.lastShotTime = gpGlobals->curtime;
}

// Where the gun should point now, as a spread offset.
static Vector2D PivotTarget(C_NEOBaseCombatWeapon *pWeapon, C_BasePlayer *pOwner)
{
	if (pWeapon != s_pivot.pWeapon)
	{
		return Vector2D(0.0f, 0.0f);
	}
	if (cl_neo_spread_pivot.GetInt() == 2)
	{
		// Follow: toward the last shot, held for about a shot's time, then back.
		const float hold = Max(pWeapon->GetFireRate() * 1.5f, 0.1f);
		return (gpGlobals->curtime - s_pivot.lastShotTime < hold) ? s_pivot.lastShot : Vector2D(0.0f, 0.0f);
	}

	// Lead: while an automatic's trigger is held, the next shot fires on the first tick at or after its next
	// attack time; that tick's command number gives its seed.
	const bool bFiring = s_pivot.bKnowsCommands && (pOwner->m_nButtons & IN_ATTACK) && !pWeapon->IsSemiAuto()
		&& pWeapon->Clip1() > 0 && !pWeapon->m_bInReload;
	if (!bFiring)
	{
		return Vector2D(0.0f, 0.0f);
	}
	const int tick = static_cast<int>(ceilf(pWeapon->m_flNextPrimaryAttack / TICK_INTERVAL - 0.01f));
	const int command = tick + s_pivot.commandMinusTick;
	const int seed = MD5_PseudoRandom(command) & 0x7fffffff;
	return SpreadOffset(seed, 0, pWeapon->GetBulletSpread());
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
	if (!pWeapon || !pOwner || !pOwner->IsLocalPlayer())
	{
		return;
	}
	if (s_pivot.updatedFrame != gpGlobals->framecount)
	{
		s_pivot.updatedFrame = gpGlobals->framecount;
		const Vector2D target = cl_neo_spread_pivot.GetBool() ? PivotTarget(pWeapon, pOwner) : Vector2D(0.0f, 0.0f);
		const float ease = 1.0f - expf(-gpGlobals->frametime / cl_neo_spread_pivot_time.GetFloat());
		s_pivot.current += (target - s_pivot.current) * ease;
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
