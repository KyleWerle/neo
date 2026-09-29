#include "cbase.h"
#include "neo_gunplay_spectator_hits.h"
#include "neo_gunplay_marks.h"
#include "neo_gunplay_shots.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_spectator_hits_debug("cl_neo_gunplay_spectator_hits_debug", "0", FCVAR_NONE,
	"Debug: print each impact received while spectating in first person, and whether it was the watched player's.");

namespace NeoGunplaySpectatorHits
{
static constexpr int MAX_HITS = 32;
static constexpr float START_REACH = 48.0f;	// units: the view is interpolated behind the server, a moving player drifts
static constexpr float AIM_DOT = 0.8f;			// the shot's line against their aim (the view lags their turns too)
static constexpr float MAX_AGE = 0.25f;		// seconds a hit waits for its shot to be taken

struct Hit
{
	Vector direction;
	float time;
};
static Hit s_hits[MAX_HITS];
static int s_iCount = 0;
static float s_viewChanged = -100.0f;
} // namespace NeoGunplaySpectatorHits

void NeoGunplaySpectatorImpact(const Vector &start, const Vector &hit)
{
	using namespace NeoGunplaySpectatorHits;
	if (!NeoGunplayEnabled())
	{
		return;
	}
	const NeoGunplayShots &shots = NeoGunplayWatchShots();
	if (shots.viewChanged != s_viewChanged)
	{
		s_viewChanged = shots.viewChanged;
		s_iCount = 0;
	}
	if (!shots.bSpectating || !shots.pPlayer || !shots.pWeapon)
	{
		return;
	}
	// Theirs: it left from their eyes, along their aim. A penetration's second impact starts where the bullet left
	// the wall, so it isn't taken.
	Vector direction = hit - start;
	const float length = direction.NormalizeInPlace();
	Vector forward;
	AngleVectors(shots.pPlayer->EyeAngles(), &forward);
	const float reach = (start - shots.pPlayer->EyePosition()).Length();
	const bool bTheirs = length > 1.0f && reach <= START_REACH && direction.Dot(forward) >= AIM_DOT;
	if (cl_neo_gunplay_spectator_hits_debug.GetBool())
	{
		Msg("[spectate] impact %.0f units from their eyes, %.2f along their aim: %s\n", reach, direction.Dot(forward),
			bTheirs ? "theirs" : "not theirs");
	}
	if (!bTheirs)
	{
		return;
	}
	NeoGunplayMarkHit(shots.pWeapon, hit);
	if (s_iCount == MAX_HITS)
	{
		V_memmove(s_hits, s_hits + 1, sizeof(Hit) * (MAX_HITS - 1));
		--s_iCount;
	}
	s_hits[s_iCount++] = { direction, gpGlobals->realtime };
}

int NeoGunplayTakeSpectatorHits(Vector *pDirections, int max)
{
	using namespace NeoGunplaySpectatorHits;
	const float now = gpGlobals->realtime;
	int taken = 0;
	for (int i = 0; i < s_iCount && taken < max; ++i)
	{
		if (now - s_hits[i].time <= MAX_AGE)
		{
			pDirections[taken++] = s_hits[i].direction;
		}
	}
	s_iCount = 0;
	return taken;
}
