#include "cbase.h"
#include "neo_spectator_hits.h"
#include "neo_view_shots.h"
#include "neo_viewmodel_recoil.h"
#include "c_neo_player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace NeoSpectatorHits
{
static constexpr int MAX_HITS = 32;
static constexpr float START_REACH = 48.0f; // the view is interpolated behind the server, so a moving player drifts
static constexpr float AIM_DOT = 0.8f; // the view lags their turns too
static constexpr float MAX_AGE = 0.25f;

struct Hit
{
	Vector direction;
	float time;
};
static Hit s_hits[MAX_HITS];
static int s_iCount = 0;
static float s_viewChanged = -100.0f;

static void DropOnViewChange(const NeoViewShots &shots)
{
	if (shots.viewChanged != s_viewChanged)
	{
		s_viewChanged = shots.viewChanged;
		s_iCount = 0;
	}
}
} // namespace NeoSpectatorHits

void NeoSpectatorImpact(const Vector &start, const Vector &hit)
{
	using namespace NeoSpectatorHits;
	if (NeoGunMotion() == NEO_GUN_MOTION_OFF)
	{
		return;
	}
	const NeoViewShots &shots = NeoGetViewShots();
	DropOnViewChange(shots);
	C_NEO_Player *pPlayer = shots.hPlayer.Get();
	if (!shots.bSpectating || !pPlayer || !shots.hWeapon.Get())
	{
		return;
	}
	// A penetration's second impact starts where the bullet left the wall, so it is out of reach.
	Vector direction = hit - start;
	const float length = direction.NormalizeInPlace();
	Vector forward;
	AngleVectors(pPlayer->EyeAngles(), &forward);
	const float reach = (start - pPlayer->EyePosition()).Length();
	if (length <= 1.0f || reach > START_REACH || direction.Dot(forward) < AIM_DOT)
	{
		return;
	}
	if (s_iCount == MAX_HITS)
	{
		V_memmove(s_hits, s_hits + 1, sizeof(Hit) * (MAX_HITS - 1));
		--s_iCount;
	}
	s_hits[s_iCount++] = { direction, gpGlobals->realtime };
}

int NeoTakeSpectatorHits(Vector *pDirections, int max)
{
	using namespace NeoSpectatorHits;
	DropOnViewChange(NeoGetViewShots());
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
