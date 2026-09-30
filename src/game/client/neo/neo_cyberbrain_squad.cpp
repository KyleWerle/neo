#include "cbase.h"
#include "neo_cyberbrain_team.h"
#include "c_neo_player.h"
#include "c_playerresource.h"
#include "neo_gamerules.h"
#include "ui/neo_scoreboard.h"
#include "ui/neo_hud_deathnotice.h"
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The squad list, top left, under your squad's star: NT's own art throughout (the star images, the class icons with
// their squad colours and the dead skull, the killfeed font's rank marks). Per mate: the class icon, the rank mark, the
// name (a takeover's context included), the class, health as the stock list shows it, and a hairline of health under
// the row; the dead as KIA with the name (or who's impersonating them). The order is the stock one: with the bot
// commander, the bots you command (in your team's colour), then the rest of your squad, then a gap and the rest of the
// team smaller; without it, your squad then the team. Hidden with the scoreboard as the stock list is.

namespace NeoCyberbrain
{
constexpr float STAR_X = 14.0f, STAR_Y = 8.0f, STAR_W = 150.0f, STAR_H = 37.5f;
constexpr float LIST_X = 18.0f, LIST_Y = 62.0f, LIST_W = 330.0f;
constexpr float ROW_BIG = 26.0f, ROW_SMALL = 21.0f, SQUAD_GAP = 12.0f;

static int Texture(const char *pFile)
{
	const int id = vgui::surface()->CreateNewTextureID();
	vgui::surface()->DrawSetTextureFile(id, pFile, true, false);
	return id;
}

static void PaintStar(const Frame &f, int star, int team)
{
	static int s_stars[STAR__TOTAL];
	static bool s_bLoaded = false;
	if (!s_bLoaded)
	{
		// In the stars' order (alpha first, none last), as the stock element loads them.
		static const char *s_files[STAR__TOTAL] = { "vgui/hud/star_alpha", "vgui/hud/star_bravo", "vgui/hud/star_charlie",
			"vgui/hud/star_delta", "vgui/hud/star_echo", "vgui/hud/star_foxtrot", "vgui/hud/star_none" };
		for (int i = 0; i < STAR__TOTAL; ++i)
			s_stars[i] = Texture(s_files[i]);
		s_bLoaded = true;
	}
	star = clamp(star, 0, STAR__TOTAL - 1);
	const Color c = star == STAR_NONE ? COLOR_NEO_WHITE : team == TEAM_NSF ? COLOR_NSF : COLOR_JINRAI;
	NeoGhostFlush();
	vgui::surface()->DrawSetTexture(s_stars[star]);
	vgui::surface()->DrawSetColor(c.r(), c.g(), c.b(), Alpha(f, 0.9f));
	const float s = f.s;
	vgui::surface()->DrawTexturedRect(RoundFloatToInt(STAR_X * s), RoundFloatToInt(STAR_Y * s), RoundFloatToInt((STAR_X + STAR_W) * s),
		RoundFloatToInt((STAR_Y + STAR_H) * s));
	Line(f, Vector2D((STAR_X + STAR_W + 6.0f) * s, (STAR_Y + STAR_H * 0.5f) * s), Vector2D((LIST_X + LIST_W) * s, (STAR_Y + STAR_H * 0.5f) * s),
		NEO_GHOST_LIGHT, f.color, 0.25f);
}

// The class icon from NT's atlas (vgui/classIcons: eight columns, four rows): squad-coloured for your own squad,
// generic for the rest, the skull for the dead; the column is the class.
static void ClassIcon(const Frame &f, int player, float x, float y, float size)
{
	static int s_atlas = -1;
	if (s_atlas < 0)
		s_atlas = Texture("vgui/classIcons");
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	float row = 3.0f;
	if (g_PR->IsAlive(player))
	{
		const bool bSquad = pLocal && pLocal->GetTeamNumber() == g_PR->GetTeam(player) && pLocal->GetStar() == g_PR->GetStar(player)
			&& g_PR->GetStar(player) != STAR_NONE;
		row = bSquad ? (g_PR->GetTeam(player) == TEAM_JINRAI ? 1.0f : 2.0f) : 0.0f;
	}
	const float u = (2 + g_PR->GetClass(player)) / 8.0f, v = row / 4.0f;
	NeoGhostFlush();
	vgui::surface()->DrawSetTexture(s_atlas);
	vgui::surface()->DrawSetColor(255, 255, 255, Alpha(f, g_PR->IsAlive(player) ? 0.95f : 0.6f));
	vgui::surface()->DrawTexturedSubRect(RoundFloatToInt(x), RoundFloatToInt(y), RoundFloatToInt(x + size), RoundFloatToInt(y + size),
		u, v, u + 1.0f / 8.0f, v + 1.0f / 4.0f);
}

// One mate's row, its top at y; returns the next row's top.
static float Row(const Frame &f, int player, float y, bool bSmall, const Color *pOverride)
{
	const float s = f.s, h = (bSmall ? ROW_SMALL : ROW_BIG) * s, icon = h - 4.0f * s, mid = y + h * 0.5f;
	const bool bAlive = g_PR->IsAlive(player);
	C_NEO_Player *pMate = ToNEOPlayer(UTIL_PlayerByIndex(player));
	const float x = LIST_X * s, right = (LIST_X + LIST_W) * s;
	ClassIcon(f, player, x, y + 2.0f * s, icon);

	const char *pName;
	const char *pClass = GetNeoClassName(g_PR->GetClass(player));
	if (bAlive)
	{
		pName = pMate ? pMate->GetPlayerNameWithTakeoverContext(player) : g_PR->GetPlayerName(player);
	}
	else
	{
		C_NEO_Player *pImpersonator = pMate ? pMate->m_hSpectatorTakeoverPlayerImpersonatingMe.Get() : nullptr;
		pName = pImpersonator ? pImpersonator->GetPlayerName() : g_PR->GetPlayerName(player);
		if (pImpersonator)
			pClass = GetNeoClassName(pImpersonator->m_iClassBeforeTakeover);
	}
	wchar_t name[64], cls[32];
	g_pVGuiLocalize->ConvertANSIToUnicode(pName ? pName : "", name, sizeof(name));
	g_pVGuiLocalize->ConvertANSIToUnicode(pClass ? pClass : "", cls, sizeof(cls));
	V_wcsupr(cls);

	const float a = bAlive ? (bSmall ? 0.7f : 0.95f) : 0.4f;
	const Color nameColour = pOverride ? *pOverride : f.color;
	// The rank mark (NT's killfeed glyphs, as the spectator overlay picks them).
	const wchar_t rank[2] = { static_cast<wchar_t>(NEO_HUD_DEATHNOTICEICON_RANKLESS_DOG + GetRank(g_PR->GetXP(player))), L'\0' };
	float tx = x + icon + 6.0f * s;
	tx += Text(f, rank, tx, mid, 1, FONT_ICONS, f.color, 0.6f * a) + 5.0f * s;
	tx += Text(f, name, tx, mid, 1, FONT_VALUE, nameColour, a) + 7.0f * s;
	Text(f, cls, tx, mid, 1, FONT_LABEL, f.color, 0.45f * a);

	if (bAlive)
	{
		static ConVarRef cl_neo_hud_health_mode("cl_neo_hud_health_mode");
		const int mode = cl_neo_hud_health_mode.GetInt();
		const int shown = g_PR->GetDisplayedHealth(player, mode), percent = g_PR->GetDisplayedHealth(player, 0);
		const Color hc = percent <= 25 ? CRIT : percent <= 50 ? WARN : f.color;
		wchar_t health[16];
		V_snwprintf(health, ARRAYSIZE(health), mode ? L"%dHP" : L"%d%%", shown);
		Text(f, health, right, mid, -1, FONT_VALUE, hc, a);
		// A hairline of health under the row.
		const float x0 = x + icon + 6.0f * s, x1 = x0 + (right - x0) * clamp(percent / 100.0f, 0.0f, 1.0f);
		Rect(f, Vector2D(x0, y + h - 2.0f * s), Vector2D(right, y + h - 1.0f * s), f.color, 0.1f);
		Rect(f, Vector2D(x0, y + h - 2.0f * s), Vector2D(x1, y + h - 1.0f * s), hc, 0.5f * a);
	}
	else
	{
		Plate(f, L"KIA", right, mid, -1, 0.5f);
	}
	return y + h;
}

int SquadOrder(SquadEntry out[MAX_PLAYERS])
{
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	const int team = GetLocalPlayerTeam(), self = GetLocalPlayerIndex();
	if (!g_PR || !pLocal || !NEORules()->IsTeamplay() || (team != TEAM_JINRAI && team != TEAM_NSF))
		return 0;
	static ConVarRef cl_neo_hud_scoreboard_hide_others("cl_neo_hud_scoreboard_hide_others");
	if (cl_neo_hud_scoreboard_hide_others.GetBool() && g_pNeoScoreBoard && g_pNeoScoreBoard->IsVisible())
		return 0;
	static ConVarRef sv_neo_bot_cmdr_enable("sv_neo_bot_cmdr_enable");
	const bool bCommander = sv_neo_bot_cmdr_enable.IsValid() && sv_neo_bot_cmdr_enable.GetBool();
	const int star = g_PR->GetStar(self);

	int commanded[MAX_PLAYERS], squad[MAX_PLAYERS], rest[MAX_PLAYERS];
	int nCommanded = 0, nSquad = 0, nRest = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		if (i == self || !g_PR->IsConnected(i) || g_PR->GetTeam(i) != team)
			continue;
		if (star != STAR_NONE && g_PR->GetStar(i) == star)
		{
			C_NEO_Player *pMate = ToNEOPlayer(UTIL_PlayerByIndex(i));
			if (bCommander && pMate && pMate->m_hCommandingPlayer.Get() == pLocal)
				commanded[nCommanded++] = i;
			else
				squad[nSquad++] = i;
		}
		else
		{
			rest[nRest++] = i;
		}
	}
	int count = 0;
	for (int i = 0; i < nCommanded; ++i)
		out[count++] = { commanded[i], false, true, false };
	for (int i = 0; i < nSquad; ++i)
		out[count++] = { squad[i], false, false, false };
	for (int i = 0; i < nRest; ++i)
		out[count++] = { rest[i], true, false, i == 0 && count > 0 };
	return count;
}

void PaintSquad(const Frame &f)
{
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	const int team = GetLocalPlayerTeam();
	if (!g_PR || !pLocal || !NEORules()->IsTeamplay() || (team != TEAM_JINRAI && team != TEAM_NSF))
		return;
	PaintStar(f, pLocal->GetStar(), team);
	SquadEntry order[MAX_PLAYERS];
	const int count = SquadOrder(order);
	const Color teamColour = TeamColour(team);
	float y = LIST_Y * f.s;
	for (int i = 0; i < count; ++i)
	{
		if (order[i].bGapBefore)
			y += SQUAD_GAP * f.s;
		y = Row(f, order[i].player, y, order[i].bSmall, order[i].bCommanded ? &teamColour : nullptr);
	}
}
} // namespace NeoCyberbrain
