#include "cbase.h"
#include "neo_hud_model_feed.h"
#include "neo_hud_model_team.h"
#include "c_neo_player.h"
#include "c_playerresource.h"
#include "c_team.h"
#include "igameevents.h"
#include "igamesystem.h"
#include "GameEventListener.h"
#include "ui/neo_scoreboard.h"
#include "ui/neo_hud_deathnotice.h"
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace NeoHud
{
static const Color FEED_DOWN(255, 90, 74, 255);	// a rank lost (the cyberbrain's critical red)

using Entry = FeedEntry;

static Entry s_feed[FEED_MAX];
static int s_feedCount = 0;

void ResetFeed()
{
	s_feedCount = 0;
}

static void Add(Entry &e, const wchar_t *pText, FeedKind kind, const Color &c)
{
	if (e.count >= FEED_SEGMENTS || !pText || !pText[0])
		return;
	FeedSegment &seg = e.seg[e.count++];
	V_wcsncpy(seg.text, pText, sizeof(seg.text));
	seg.kind = kind;
	seg.color = c;
}

static void AddName(Entry &e, const char *pName, int team)
{
	wchar_t name[64];
	g_pVGuiLocalize->ConvertANSIToUnicode(pName ? pName : "", name, sizeof(name));
	Add(e, name, FEED_NAME, TeamColour(team));
}

static void AddIcon(Entry &e, wchar_t glyph, const Color &c)
{
	const wchar_t text[2] = { glyph, L'\0' };
	Add(e, text, FEED_ICON, c);
}

static int TeamOf(int player)
{
	C_Team *pTeam = player > 0 ? GetPlayersTeam(player) : nullptr;
	return pTeam ? pTeam->GetTeamNumber() : TEAM_UNASSIGNED;
}

// A player's name with a takeover's context, as the stock feed shows it.
static const char *NameOf(int player)
{
	C_NEO_Player *pPlayer = ToNEOPlayer(UTIL_PlayerByIndex(player));
	if (pPlayer)
		return pPlayer->GetPlayerNameWithTakeoverContext(player);
	const char *pName = g_PR->GetPlayerName(player);
	return pName ? pName : "";
}

static void AddDeath(IGameEvent *pEvent, Entry &e)
{
	const int killer = engine->GetPlayerForUserID(pEvent->GetInt("attacker"));
	const int victim = engine->GetPlayerForUserID(pEvent->GetInt("userid"));
	const int assist = engine->GetPlayerForUserID(pEvent->GetInt("assists"));
	const bool bSuicide = pEvent->GetBool("suicide");
	const char *pKiller = killer > 0 ? NameOf(killer) : "", *pAssist = assist > 0 ? NameOf(assist) : "";
	// A spectator assisting their own takeover's kill: "bot + player".
	C_NEO_Player *pKillerPlayer = ToNEOPlayer(UTIL_PlayerByIndex(killer));
	if (pKillerPlayer && killer == assist && killer > 0 && pKillerPlayer->GetSpectatorTakeoverPlayerTarget())
	{
		pKiller = pKillerPlayer->GetSpectatorTakeoverPlayerTarget()->GetNeoPlayerName();
		pAssist = pKillerPlayer->GetNeoPlayerName();
	}
	const Color white = COLOR_NEO_WHITE;
	if (assist > 0)
	{
		if (bSuicide)
			AddName(e, NameOf(victim), TeamOf(victim));
		else if (killer > 0)
			AddName(e, pKiller, TeamOf(killer));
		Add(e, L" + ", FEED_GAP, white);
		AddName(e, pAssist, TeamOf(assist));
	}
	else if (!bSuicide && killer > 0)
	{
		AddName(e, pKiller, TeamOf(killer));
	}
	if (!bSuicide)
	{
		wchar_t icon[4];
		g_pVGuiLocalize->ConvertANSIToUnicode(pEvent->GetString("deathIcon"), icon, sizeof(icon));
		Add(e, L" ", FEED_GAP, white);
		Add(e, icon, FEED_ICON, white);
		if (pEvent->GetBool("explosive"))
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_EXPLODE, white);
		if (pEvent->GetBool("headshot"))
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_HEADSHOT, white);
	}
	else
	{
		Add(e, L" ", FEED_GAP, white);
		AddIcon(e, NEO_HUD_DEATHNOTICEICON_SHORTBUS, COLOR_NEO_ORANGE);
	}
	Add(e, L" ", FEED_GAP, white);
	AddName(e, NameOf(victim), TeamOf(victim));
	if (pEvent->GetBool("ghoster"))
		AddIcon(e, NEO_HUD_DEATHNOTICEICON_GHOST, white);
	const int self = GetLocalPlayerIndex();
	e.bInvolved = killer == self || victim == self || assist == self;
}

static void FeedEvent(IGameEvent *pEvent)
{
	static ConVarRef hud_deathnotice_time("hud_deathnotice_time"), cl_neo_hud_extended_killfeed("cl_neo_hud_extended_killfeed");
	if (!g_PR || hud_deathnotice_time.GetFloat() <= 0.0f)
		return;
	const char *pName = pEvent->GetName();
	const int extended = cl_neo_hud_extended_killfeed.GetInt();
	const bool bDeath = !V_stricmp(pName, "player_death");
	const bool bRank = !V_stricmp(pName, "player_rankchange");
	const bool bGhost = !V_stricmp(pName, "ghost_capture");
	const bool bExtract = !V_stricmp(pName, "vip_extract"), bVipDeath = !V_stricmp(pName, "vip_death");
	if (!(bDeath || (bRank && extended >= 2) || ((bGhost || bExtract || bVipDeath) && extended >= 1)))
		return;

	Entry e = {};
	const Color white = COLOR_NEO_WHITE;
	const int player = engine->GetPlayerForUserID(pEvent->GetInt("userid"));
	const int self = GetLocalPlayerIndex();
	if (bDeath)
	{
		AddDeath(pEvent, e);
	}
	else if (bRank)
	{
		const int oldRank = pEvent->GetInt("oldRank"), newRank = pEvent->GetInt("newRank");
		const int team = TeamOf(player);
		AddName(e, NameOf(player), team);
		Add(e, L" ", FEED_GAP, white);
		AddIcon(e, static_cast<wchar_t>(NEO_HUD_DEATHNOTICEICON_RANKLESS_DOG + oldRank), white);
		if (newRank > oldRank)
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_RANKUP, TeamColour(team));
		else
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_RANKDOWN, FEED_DOWN);
		AddIcon(e, static_cast<wchar_t>(NEO_HUD_DEATHNOTICEICON_RANKLESS_DOG + newRank), white);
		e.bInvolved = player == self;
	}
	else if (bGhost)
	{
		if (player > 0)
		{
			AddName(e, NameOf(player), TeamOf(player));
			Add(e, L" HAS CAPTURED THE ", FEED_WORDS, white);
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_GHOST, white);
		}
		else
		{
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_GHOST, white);
			Add(e, L" HAS BEEN CAPTURED", FEED_WORDS, white);
		}
		e.bInvolved = player == self;
	}
	else
	{
		Add(e, L"THE VIP ", FEED_WORDS, white);
		if (player > 0)
		{
			AddName(e, NameOf(player), TeamOf(player));
			Add(e, L" ", FEED_GAP, white);
		}
		Add(e, bExtract ? L"HAS EXTRACTED" : L"HAS DIED", FEED_WORDS, white);
		e.bInvolved = bExtract && player == self;
	}
	e.hide = gpGlobals->curtime + hud_deathnotice_time.GetFloat();
	if (s_feedCount == FEED_MAX)
	{
		for (int i = 1; i < FEED_MAX; ++i)
			s_feed[i - 1] = s_feed[i];
		--s_feedCount;
	}
	s_feed[s_feedCount++] = e;
}

int FeedEntries(const FeedEntry **ppEntries)
{
	// Expired entries go, and any that would outlive their lifetime (left over from a map whose game time ran higher).
	static ConVarRef hud_deathnotice_time("hud_deathnotice_time");
	const float longest = hud_deathnotice_time.GetFloat() + 1.0f;
	int kept = 0;
	for (int i = 0; i < s_feedCount; ++i)
	{
		const float left = s_feed[i].hide - gpGlobals->curtime;
		if (left > 0.0f && left <= longest)
		{
			if (kept != i)
				s_feed[kept] = s_feed[i];
			++kept;
		}
	}
	s_feedCount = kept;
	*ppEntries = s_feed;
	// The scoreboard hides the feed as it does the stock one.
	static ConVarRef cl_neo_hud_scoreboard_hide_others("cl_neo_hud_scoreboard_hide_others");
	if (cl_neo_hud_scoreboard_hide_others.GetBool() && g_pNeoScoreBoard && g_pNeoScoreBoard->IsVisible())
		return 0;
	return s_feedCount;
}

// Listens for the death notice's events in every style, and starts each map's feed empty (the entries' lifetimes
// are on game time, which restarts with the map).
class CNeoHudFeedModel : public CAutoGameSystem, public CGameEventListener
{
public:
	CNeoHudFeedModel() : CAutoGameSystem("CNeoHudFeedModel") {}

	bool Init() override
	{
		ListenForGameEvent("player_death");
		ListenForGameEvent("player_rankchange");
		ListenForGameEvent("ghost_capture");
		ListenForGameEvent("vip_extract");
		ListenForGameEvent("vip_death");
		return true;
	}
	void LevelInitPreEntity() override { ResetFeed(); }
	void FireGameEvent(IGameEvent *pEvent) override { FeedEvent(pEvent); }
};
static CNeoHudFeedModel s_feedModel;
} // namespace NeoHud
