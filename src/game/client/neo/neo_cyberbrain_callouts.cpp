#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "c_neo_player.h"
#include "view.h"
#include "igameevents.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The ghost's enemy callouts, for the surround ring. The stock compass keeps them (neo_hud_compass.cpp) but hides in the
// cyberbrain styles, so the ring keeps its own copy from the same game event: one per spotted player, restarted each
// time the ghost calls them out again, for as long as the compass would show it (cl_neo_ghost_callout_compass_time).

namespace NeoCyberbrain
{

static struct Spotted { Vector pos; float time = -100.0f; } s_spotted[MAX_PLAYERS_ARRAY_SAFE];

static void ClearCallouts()
{
	for (Spotted &spotted : s_spotted)
		spotted.time = -100.0f;
}

void CalloutEvent(IGameEvent *pEvent)
{
	const char *pName = pEvent->GetName();
	if (!V_stricmp(pName, "ghost_enemy_callout"))
	{
		// Only your own team's callouts, and none while spectating (as the compass).
		const int team = GetLocalPlayerTeam();
		if (team == TEAM_SPECTATOR || pEvent->GetInt("team") != team)
			return;
		const int target = pEvent->GetInt("targetid");
		if (target > 0 && target < V_ARRAYSIZE(s_spotted))
		{
			s_spotted[target].pos.Init(pEvent->GetInt("targetx"), pEvent->GetInt("targety"), pEvent->GetInt("targetz"));
			s_spotted[target].time = gpGlobals->realtime;
		}
	}
	else if (!V_stricmp(pName, "round_start"))
	{
		ClearCallouts();
	}
	else if (!V_stricmp(pName, "player_team"))
	{
		C_BasePlayer *pPlayer = UTIL_PlayerByUserId(pEvent->GetInt("userid"));
		if (pPlayer && pPlayer->IsLocalPlayer())
			ClearCallouts();
	}
}

void SenseCallouts(float now, Senses &out)
{
	static ConVarRef cl_neo_ghost_callout_compass_time("cl_neo_ghost_callout_compass_time");
	const float lasts = cl_neo_ghost_callout_compass_time.IsValid() ? cl_neo_ghost_callout_compass_time.GetFloat() : 10.0f;
	out.calloutCount = 0;
	out.calloutNewest = -1;
	for (const Spotted &spotted : s_spotted)
	{
		const float age = now - spotted.time;
		if (age < 0.0f || age >= lasts || out.calloutCount >= MAX_CALLOUTS)
			continue;
		const Vector d = spotted.pos - MainViewOrigin();
		Callout &c = out.callout[out.calloutCount];
		c.yaw = RAD2DEG(atan2f(d.y, d.x));
		c.metres = d.Length() * METERS_PER_INCH;
		c.age = age;
		c.life = lasts > 0.0f ? 1.0f - age / lasts : 0.0f;
		if (out.calloutNewest < 0 || age < out.callout[out.calloutNewest].age)
			out.calloutNewest = out.calloutCount;
		++out.calloutCount;
	}
}

void ResetCallouts()
{
	ClearCallouts();
}
} // namespace NeoCyberbrain
