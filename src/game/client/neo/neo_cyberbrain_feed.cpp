#include "cbase.h"
#include "neo_cyberbrain_team.h"
#include "c_neo_player.h"
#include "c_playerresource.h"
#include "c_team.h"
#include "igameevents.h"
#include "ui/neo_scoreboard.h"
#include "ui/neo_hud_deathnotice.h"
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The kill feed, top right: its own copy of the death notice's entries (the stock one keeps its side effects: the
// round's killers for the spectator overlay, your kills for the scoreboard, the console lines stats plugins read), in
// NT's killfeed icon glyphs between team-coloured names. The same kinds, filters and lifetime as the stock feed
// (hud_deathnotice_time, cl_neo_hud_extended_killfeed; eight at most, the oldest on top): a kill ("killer [+ assist]
// weapon [explosive] [headshot] victim [ghost]"), a suicide or world death (the shortbus), a rank change, the ghost
// captured, the VIP extracted or dead. Entries you're in are bracketed.

namespace NeoCyberbrain
{
constexpr float FEED_Y = 16.0f, FEED_ROW = 30.0f, FEED_RIGHT = 16.0f;

using Entry = FeedEntry;

static Entry s_feed[FEED_MAX];
static int s_feedCount = 0;

void ResetFeed()
{
	s_feedCount = 0;
}

static void Add(Entry &e, const wchar_t *pText, Font font, const Color &c)
{
	if (e.count >= FEED_SEGMENTS || !pText || !pText[0])
		return;
	FeedSegment &seg = e.seg[e.count++];
	V_wcsncpy(seg.text, pText, sizeof(seg.text));
	seg.font = font;
	seg.color = c;
}

static void AddName(Entry &e, const char *pName, int team)
{
	wchar_t name[64];
	g_pVGuiLocalize->ConvertANSIToUnicode(pName ? pName : "", name, sizeof(name));
	Add(e, name, FONT_NAME, TeamColour(team));
}

static void AddIcon(Entry &e, wchar_t glyph, const Color &c)
{
	const wchar_t text[2] = { glyph, L'\0' };
	Add(e, text, FONT_ICONS, c);
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
		Add(e, L" + ", FONT_VALUE, white);
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
		Add(e, L" ", FONT_VALUE, white);
		Add(e, icon, FONT_ICONS, white);
		if (pEvent->GetBool("explosive"))
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_EXPLODE, white);
		if (pEvent->GetBool("headshot"))
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_HEADSHOT, white);
	}
	else
	{
		Add(e, L" ", FONT_VALUE, white);
		AddIcon(e, NEO_HUD_DEATHNOTICEICON_SHORTBUS, COLOR_NEO_ORANGE);
	}
	Add(e, L" ", FONT_VALUE, white);
	AddName(e, NameOf(victim), TeamOf(victim));
	if (pEvent->GetBool("ghoster"))
		AddIcon(e, NEO_HUD_DEATHNOTICEICON_GHOST, white);
	const int self = GetLocalPlayerIndex();
	e.bInvolved = killer == self || victim == self || assist == self;
}

void FeedEvent(IGameEvent *pEvent)
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
		Add(e, L" ", FONT_VALUE, white);
		AddIcon(e, static_cast<wchar_t>(NEO_HUD_DEATHNOTICEICON_RANKLESS_DOG + oldRank), white);
		if (newRank > oldRank)
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_RANKUP, TeamColour(team));
		else
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_RANKDOWN, CRIT);
		AddIcon(e, static_cast<wchar_t>(NEO_HUD_DEATHNOTICEICON_RANKLESS_DOG + newRank), white);
		e.bInvolved = player == self;
	}
	else if (bGhost)
	{
		if (player > 0)
		{
			AddName(e, NameOf(player), TeamOf(player));
			Add(e, L" HAS CAPTURED THE ", FONT_LABEL, white);
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_GHOST, white);
		}
		else
		{
			AddIcon(e, NEO_HUD_DEATHNOTICEICON_GHOST, white);
			Add(e, L" HAS BEEN CAPTURED", FONT_LABEL, white);
		}
		e.bInvolved = player == self;
	}
	else
	{
		Add(e, L"THE VIP ", FONT_LABEL, white);
		if (player > 0)
		{
			AddName(e, NameOf(player), TeamOf(player));
			Add(e, L" ", FONT_VALUE, white);
		}
		Add(e, bExtract ? L"HAS EXTRACTED" : L"HAS DIED", FONT_LABEL, white);
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
	int kept = 0;
	for (int i = 0; i < s_feedCount; ++i)
	{
		if (s_feed[i].hide > gpGlobals->curtime)
			s_feed[kept++] = s_feed[i];
	}
	s_feedCount = kept;
	*ppEntries = s_feed;
	// The scoreboard hides the feed as it does the stock one.
	static ConVarRef cl_neo_hud_scoreboard_hide_others("cl_neo_hud_scoreboard_hide_others");
	if (cl_neo_hud_scoreboard_hide_others.GetBool() && g_pNeoScoreBoard && g_pNeoScoreBoard->IsVisible())
		return 0;
	return s_feedCount;
}

void PaintFeed(const Frame &f)
{
	const FeedEntry *pFeed;
	const int count = FeedEntries(&pFeed);
	const float s = f.s, right = f.wide - FEED_RIGHT * s;
	for (int i = 0; i < count; ++i)
	{
		const Entry &e = pFeed[i];
		float width = 0.0f;
		for (int k = 0; k < e.count; ++k)
			width += TextWidth(e.seg[k].text, e.seg[k].font);
		const float y = (FEED_Y + FEED_ROW * 0.5f + i * FEED_ROW) * s, x0 = right - width;
		// A faint dark strip behind (low: it shouldn't read as a panel), and your own entries bracketed.
		Rect(f, Vector2D(x0 - 10.0f * s, y - 12.0f * s), Vector2D(right + 6.0f * s, y + 12.0f * s), Color(0, 0, 0, 255), 0.35f);
		if (e.bInvolved)
		{
			const float bx0 = x0 - 12.0f * s, bx1 = right + 8.0f * s, by0 = y - 13.0f * s, by1 = y + 13.0f * s, arm = 6.0f * s;
			Line(f, Vector2D(bx0, by0), Vector2D(bx0 + arm, by0), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx0, by0), Vector2D(bx0, by1), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx0, by1), Vector2D(bx0 + arm, by1), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx1, by0), Vector2D(bx1 - arm, by0), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx1, by0), Vector2D(bx1, by1), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx1, by1), Vector2D(bx1 - arm, by1), NEO_GHOST_MEDIUM, f.color, 0.8f);
		}
		float x = x0;
		for (int k = 0; k < e.count; ++k)
			x += Text(f, e.seg[k].text, x, y, 1, e.seg[k].font, e.seg[k].color, 0.95f);
	}
}
} // namespace NeoCyberbrain
