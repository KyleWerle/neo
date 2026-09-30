#include "cbase.h"
#include "neo_hud_model_callouts.h"
#include "c_neo_player.h"
#include "view.h"
#include "igameevents.h"
#include "igamesystem.h"
#include "GameEventListener.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace NeoHud
{
static struct Spotted { Vector pos; float time = -100.0f; } s_spotted[MAX_PLAYERS_ARRAY_SAFE];

void ResetCallouts()
{
	for (Spotted &spotted : s_spotted)
		spotted.time = -100.0f;
}

static void CalloutEvent(IGameEvent *pEvent)
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
		ResetCallouts();
	}
	else if (!V_stricmp(pName, "player_team"))
	{
		C_BasePlayer *pPlayer = UTIL_PlayerByUserId(pEvent->GetInt("userid"));
		if (pPlayer && pPlayer->IsLocalPlayer())
			ResetCallouts();
	}
}

int ReadCallouts(float now, Callout out[MAX_CALLOUTS], int &newest)
{
	static ConVarRef cl_neo_ghost_callout_compass_time("cl_neo_ghost_callout_compass_time");
	const float lasts = cl_neo_ghost_callout_compass_time.IsValid() ? cl_neo_ghost_callout_compass_time.GetFloat() : 10.0f;
	int count = 0;
	newest = -1;
	for (const Spotted &spotted : s_spotted)
	{
		const float age = now - spotted.time;
		if (age < 0.0f || age >= lasts || count >= MAX_CALLOUTS)
			continue;
		const Vector d = spotted.pos - MainViewOrigin();
		Callout &c = out[count];
		c.yaw = RAD2DEG(atan2f(d.y, d.x));
		c.metres = d.Length() * METERS_PER_INCH;
		c.age = age;
		c.life = lasts > 0.0f ? 1.0f - age / lasts : 0.0f;
		if (newest < 0 || age < out[newest].age)
			newest = count;
		++count;
	}
	return count;
}

// Listens for the callouts in every style (Competitive's compass shows them too).
class CNeoHudCalloutModel : public CAutoGameSystem, public CGameEventListener
{
public:
	CNeoHudCalloutModel() : CAutoGameSystem("CNeoHudCalloutModel") {}

	bool Init() override
	{
		ListenForGameEvent("ghost_enemy_callout");
		ListenForGameEvent("round_start");
		ListenForGameEvent("player_team");
		return true;
	}
	void FireGameEvent(IGameEvent *pEvent) override { CalloutEvent(pEvent); }
};
static CNeoHudCalloutModel s_calloutModel;
} // namespace NeoHud
