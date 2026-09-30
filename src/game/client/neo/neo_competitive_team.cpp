#include "cbase.h"
#include "neo_competitive.h"
#include "neo_cyberbrain_team.h"
#include "c_neo_player.h"
#include "c_playerresource.h"
#include "neo_gamerules.h"
#include <vgui/ILocalize.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The Competitive HUD's team side, in the stock places as lowercase text: the round top centre (its label, the
// clock, the rounds each team has won either side, the tally, the status), the squad top left (the star's name, then
// the stock list's rows: name, rank, [class], health, or dead), the kill feed top right. The same readouts as the
// cyberbrain's (neo_cyberbrain_team.h), so both carry exactly what the stock elements do.

namespace NeoCompetitive
{
constexpr float TOP = 8.0f, SCORE_X = 90.0f, SQUAD_GAP = 12.0f, FEED_TOP = 16.0f;

void PaintRound(const Pen &pen)
{
	namespace NC = NeoCyberbrain;
	NC::RoundReadout r;
	NC::ReadRound(r);
	const float s = pen.s, cx = pen.wide * 0.5f, text = Height(FACE_TEXT), large = Height(FACE_LARGE);
	float y = TOP * s;
	if (r.bTimed)
	{
		Print(pen, r.round, cx, y, 0, FACE_TEXT, r.bPaused ? RED : FADED);
		y += text;
		Print(pen, r.clock, cx, y, 0, FACE_LARGE, r.bRed ? RED : WHITE);
		if (r.bTeamplay)
		{
			for (int i = 0; i < 2; ++i)
			{
				wchar_t won[8];
				V_snwprintf(won, ARRAYSIZE(won), L"%d", r.won[i]);
				Print(pen, won, cx + (i ? SCORE_X : -SCORE_X) * s, y, 0, FACE_LARGE, NC::TeamColour(r.teams[i]));
			}
		}
		y += large;
		wchar_t tally[32];
		if (r.tally[0])
			V_wcsncpy(tally, r.tally, sizeof(tally));
		else
			V_snwprintf(tally, ARRAYSIZE(tally), L"%d vs %d", r.alive[0], r.alive[1]);
		Print(pen, tally, cx, y, 0, FACE_TEXT, FADED);
		y += text;
	}
	else
	{
		y += text + large + text;
	}
	if (r.pStatus[0])
		Print(pen, r.pStatus, cx, y + 2.0f * s, 0, FACE_TEXT, WHITE);
}

void PaintSquad(const Pen &pen)
{
	namespace NC = NeoCyberbrain;
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	const int team = GetLocalPlayerTeam();
	if (!g_PR || !pLocal || !NEORules()->IsTeamplay())
		return;
	const float s = pen.s, x = EDGE * s, text = Height(FACE_TEXT);
	float y = TOP * s;
	// The star as its name, in the stock star's colour.
	static const wchar_t *s_stars[STAR__TOTAL] = { L"alpha", L"bravo", L"charlie", L"delta", L"echo", L"foxtrot", L"no squad" };
	const int star = clamp(pLocal->GetStar(), 0, STAR__TOTAL - 1);
	Print(pen, s_stars[star], x, y, 1, FACE_LARGE, star == STAR_NONE ? COLOR_NEO_WHITE : team == TEAM_NSF ? COLOR_NSF : COLOR_JINRAI);
	y += Height(FACE_LARGE) + 4.0f * s;

	NC::SquadEntry order[MAX_PLAYERS];
	const int count = NC::SquadOrder(order);
	static ConVarRef cl_neo_hud_health_mode("cl_neo_hud_health_mode");
	const Color teamColour = NC::TeamColour(team);
	for (int i = 0; i < count; ++i)
	{
		const NC::SquadEntry &e = order[i];
		if (e.bGapBefore)
			y += SQUAD_GAP * s;
		const int player = e.player;
		const bool bAlive = g_PR->IsAlive(player);
		C_NEO_Player *pMate = ToNEOPlayer(UTIL_PlayerByIndex(player));
		const char *pName, *pClass = GetNeoClassName(g_PR->GetClass(player));
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
		wchar_t name[64], rest[64];
		g_pVGuiLocalize->ConvertANSIToUnicode(pName ? pName : "", name, sizeof(name));
		if (bAlive)
		{
			const int mode = cl_neo_hud_health_mode.GetInt();
			wchar_t cls[24];
			g_pVGuiLocalize->ConvertANSIToUnicode(pClass ? pClass : "", cls, sizeof(cls));
			wchar_t rank[8];
			g_pVGuiLocalize->ConvertANSIToUnicode(GetRankName(g_PR->GetXP(player), true), rank, sizeof(rank));
			V_snwprintf(rest, ARRAYSIZE(rest), mode ? L" %ls  [%ls]  %dhp" : L" %ls  [%ls]  %d%%", rank, cls, g_PR->GetDisplayedHealth(player, mode));
		}
		else
		{
			wchar_t cls[24];
			g_pVGuiLocalize->ConvertANSIToUnicode(pClass ? pClass : "", cls, sizeof(cls));
			V_snwprintf(rest, ARRAYSIZE(rest), L"  [%ls]  dead", cls);
		}
		const Color c = e.bCommanded ? teamColour : !bAlive ? Color(255, 255, 255, 80) : e.bSmall ? FADED : WHITE;
		const float w = Print(pen, name, x, y, 1, FACE_TEXT, c, true);
		Print(pen, rest, x + w, y, 1, FACE_TEXT, c);
		y += text;
	}
}

void PaintFeed(const Pen &pen)
{
	namespace NC = NeoCyberbrain;
	const NC::FeedEntry *pFeed;
	const int count = NC::FeedEntries(&pFeed);
	const float s = pen.s, right = pen.wide - EDGE * s, line = Height(FACE_TEXT) + 4.0f * s;
	for (int i = 0; i < count; ++i)
	{
		const NC::FeedEntry &e = pFeed[i];
		// Names keep their case; the icons stay NT's glyphs; the words go lowercase.
		const auto faceOf = [](const NC::FeedSegment &seg) { return seg.font == NC::FONT_ICONS ? FACE_ICONS : FACE_TEXT; };
		float width = 0.0f;
		for (int k = 0; k < e.count; ++k)
			width += Width(e.seg[k].text, faceOf(e.seg[k]), e.seg[k].font == NC::FONT_VALUE);
		float x = right - width;
		const float y = FEED_TOP * s + i * line;
		if (e.bInvolved)
			Print(pen, L">", x - 10.0f * s, y, 1, FACE_TEXT, WHITE);
		for (int k = 0; k < e.count; ++k)
		{
			const NC::FeedSegment &seg = e.seg[k];
			const bool bName = seg.font == NC::FONT_VALUE;
			const float ty = seg.font == NC::FONT_ICONS ? y + (Height(FACE_TEXT) - Height(FACE_ICONS)) * 0.5f : y;
			x += Print(pen, seg.text, x, ty, 1, faceOf(seg), seg.color, bName);
		}
	}
}
} // namespace NeoCompetitive
