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
//
// The party view (Kyle, 2026-09-30; it replaced the link group's fan): the link drawn into the list. You are a
// chamfered cell under the star, filling with your neural load, your ping beside it; a spine runs down the list's left
// edge and a branch goes into each squadmate's row. A branch's line is that mate's ping (solid under 90 ms, dashed to
// 150, sparse past it; a bot's is solid) and the number sits by their health. A mate dying turns their branch red and
// breaks it off short, ending on a hollow red mark. The rest of the team isn't in your link: no branch. Any change to
// who's in it (the star, a join, a leave) traces every branch out from the spine again: the link breaking up and
// reforming. The lines are medium strokes, never hairlines (Kyle: minimal, but not too thin).

namespace NeoCyberbrain
{
constexpr float STAR_X = 14.0f, STAR_Y = 8.0f, STAR_W = 150.0f, STAR_H = 37.5f;
constexpr float LIST_X = 18.0f, LIST_Y = 62.0f, LIST_W = 330.0f;
constexpr float SPINE_X = 24.0f, ROW_X = 46.0f;		// pixels at 1080p: the party's spine, and where a row starts
constexpr float NODE_Y = 54.0f, NODE_HALF = 6.0f;		// you: the cell under the star
constexpr float BROKEN = 0.35f;						// how much of a dead mate's branch is left
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
	const float x = ROW_X * s, right = (LIST_X + LIST_W) * s;
	ClassIcon(f, player, x, y + 2.0f * s, icon);

	const char *pName, *pClass;
	SquadMateNames(player, &pName, &pClass);
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
	tx += Text(f, name, tx, mid, 1, FONT_NAME, nameColour, a) + 7.0f * s;
	Text(f, cls, tx, mid, 1, FONT_LABEL, f.color, 0.6f * a);

	if (bAlive)
	{
		static ConVarRef cl_neo_hud_health_mode("cl_neo_hud_health_mode");
		const int mode = cl_neo_hud_health_mode.GetInt();
		const int shown = g_PR->GetDisplayedHealth(player, mode), percent = g_PR->GetDisplayedHealth(player, 0);
		const Color hc = percent <= 25 ? CRIT : percent <= 50 ? WARN : f.color;
		wchar_t health[16];
		V_snwprintf(health, ARRAYSIZE(health), mode ? L"%dHP" : L"%d%%", shown);
		const float healthW = Text(f, health, right, mid, -1, FONT_VALUE, hc, a);
		// A squadmate's ping by their health (the party view: their branch's line is the same ping).
		if (!bSmall)
		{
			wchar_t ping[16];
			if (g_PR->IsFakePlayer(player))
				V_wcsncpy(ping, L"BOT", sizeof(ping));
			else
				V_snwprintf(ping, ARRAYSIZE(ping), L"%d MS", g_PR->GetPing(player));
			Text(f, ping, right - healthW - 10.0f * s, mid, -1, FONT_LABEL, f.color, 0.6f * a);
		}
		// A line of health under the row (two pixels: not a hairline).
		const float x0 = x + icon + 6.0f * s, x1 = x0 + (right - x0) * clamp(percent / 100.0f, 0.0f, 1.0f);
		Rect(f, Vector2D(x0, y + h - 3.0f * s), Vector2D(right, y + h - 1.0f * s), f.color, 0.1f);
		Rect(f, Vector2D(x0, y + h - 3.0f * s), Vector2D(x1, y + h - 1.0f * s), hc, 0.5f * a);
	}
	else
	{
		Plate(f, L"KIA", right, mid, -1, 0.5f);
	}
	return y + h;
}

// A line in dashes, screen pixels; the gap at 1080p (0 draws it solid).
static void Dashed(const Frame &f, const Vector2D &from, const Vector2D &to, float gap, NeoGhostWeight weight, const Color &c, float a)
{
	const Vector2D d = to - from;
	const float len = d.Length();
	if (len < 0.5f)
		return;
	if (gap <= 0.0f)
	{
		Line(f, from, to, weight, c, a);
		return;
	}
	const float dash = 3.0f * f.s;
	const Vector2D u = d / len;
	for (float t = 0.0f; t < len; t += dash + gap * f.s)
		Line(f, from + u * t, from + u * Min(t + dash, len), weight, c, a);
}

// The party across frames: who's in it, when that last changed, and who died when.
struct Party
{
	int star = -2, count = 0;
	int roster[MAX_PLAYERS] = {};
	float changed = -100.0f;
	bool alive[MAX_PLAYERS + 1] = {};
	float died[MAX_PLAYERS + 1] = {};
};
static Party s_party;

// The comfort forms (cl_neo_hud_motion): how long a branch takes to trace out (and the stagger down the list), and a
// death's break. Lower is slower, never shorter; still, nothing travels: branches fade in whole.
struct PartyMotion { float trace, stagger, breakFor; bool bTravel; };
static PartyMotion MotionOf()
{
	static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
	switch (cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1)
	{
	case 0:		return { 0.5f, 0.0f, 1.0f, false };
	case 2:		return { 0.3f, 0.05f, 0.6f, true };
	default:	return { 0.5f, 0.03f, 0.8f, true };
	}
}

// Notes the roster (a change re-traces the link) and each squadmate's death.
static void TrackParty(const SquadEntry *order, int count, int star, float now)
{
	int roster[MAX_PLAYERS], n = 0;
	for (int i = 0; i < count; ++i)
		if (!order[i].bSmall)
			roster[n++] = order[i].player;
	if (star != s_party.star || n != s_party.count || V_memcmp(roster, s_party.roster, n * sizeof(int)) != 0)
	{
		s_party.star = star;
		s_party.count = n;
		V_memcpy(s_party.roster, roster, n * sizeof(int));
		s_party.changed = now;
		for (int i = 0; i < n; ++i)
		{
			s_party.alive[roster[i]] = g_PR->IsAlive(roster[i]);
			s_party.died[roster[i]] = -100.0f;	// already dead when they came in: no break to show
		}
	}
	for (int i = 0; i < n; ++i)
	{
		const int p = roster[i];
		const bool bAlive = g_PR->IsAlive(p);
		if (s_party.alive[p] && !bAlive)
			s_party.died[p] = now;
		s_party.alive[p] = bAlive;
	}
}

// You: a chamfered cell under the star, filling with your neural load (empty when the cyberbrain isn't sensing: dead,
// or watching someone), your ping beside it.
static void PaintNode(const Frame &f)
{
	const float s = f.s;
	Color unused;
	const Senses *pSenses = PublishedSenses(unused);
	CellStyle style;
	style.chamfer = 1;
	style.fill = f.color;
	Cells(f, Vector2D((SPINE_X - NODE_HALF) * s, (NODE_Y - NODE_HALF) * s), Vector2D((SPINE_X + NODE_HALF) * s, (NODE_Y + NODE_HALF) * s),
		pSenses ? clamp(pSenses->load / 6.0f, 0.0f, 1.0f) : 0.0f, style, 1.0f);
	wchar_t ping[16];
	V_snwprintf(ping, ARRAYSIZE(ping), L"%d MS", g_PR->GetPing(GetLocalPlayerIndex()));
	Text(f, ping, (SPINE_X + NODE_HALF + 7.0f) * s, NODE_Y * s, 1, FONT_VALUE, f.color, 0.85f);
}

// One squadmate's branch, from the spine into their row at mid (screen y); k is its place down the list.
static void PaintBranch(const Frame &f, int player, float mid, int k, const PartyMotion &m)
{
	const float s = f.s, age = f.now - s_party.changed;
	const float x0 = SPINE_X * s, x1 = (ROW_X - 5.0f) * s;
	float reach = 1.0f, alpha = 1.0f;
	if (m.bTravel)
		reach = NeoSmoothStep((age - k * m.stagger) / m.trace);
	else
		alpha = NeoSmoothStep(age / m.trace);
	if (reach <= 0.0f || alpha <= 0.0f)
		return;
	const Vector2D from(x0, mid);
	if (g_PR->IsAlive(player))
	{
		const int ping = g_PR->GetPing(player);
		const float gap = g_PR->IsFakePlayer(player) || ping < 90 ? 0.0f : ping < 150 ? 3.0f : 7.0f;
		Dashed(f, from, Vector2D(x0 + (x1 - x0) * reach, mid), gap, NEO_GHOST_MEDIUM, f.color, 0.8f * alpha);
		if (reach >= 1.0f)
			Rect(f, Vector2D(x1 - 2.5f * s, mid - 2.5f * s), Vector2D(x1 + 2.5f * s, mid + 2.5f * s), f.color, 0.95f * alpha);
		return;
	}
	// Dead: red, breaking off short (already broken if they died before this roster formed; still: no travel).
	const float broken = m.bTravel ? NeoSmoothStep((f.now - s_party.died[player]) / m.breakFor) : 1.0f;
	const float left = reach * (1.0f - (1.0f - BROKEN) * broken);
	Dashed(f, from, Vector2D(x0 + (x1 - x0) * left, mid), 3.0f, NEO_GHOST_MEDIUM, CRIT, 0.75f * alpha);
	if (reach >= 1.0f)
		RectOutline(f, Vector2D(x1 - 3.0f * s, mid - 3.0f * s), Vector2D(x1 + 3.0f * s, mid + 3.0f * s), NEO_GHOST_MEDIUM, CRIT,
			(0.6f + 0.35f * (1.0f - broken)) * alpha);
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
	TrackParty(order, count, pLocal->GetStar(), f.now);
	const PartyMotion motion = MotionOf();
	const Color teamColour = TeamColour(team);
	float y = LIST_Y * f.s, spineEnd = 0.0f;
	int k = 0;
	for (int i = 0; i < count; ++i)
	{
		if (order[i].bGapBefore)
			y += SQUAD_GAP * f.s;
		if (!order[i].bSmall)
		{
			const float mid = y + ROW_BIG * f.s * 0.5f;
			PaintBranch(f, order[i].player, mid, k++, motion);
			spineEnd = mid;
		}
		y = Row(f, order[i].player, y, order[i].bSmall, order[i].bCommanded ? &teamColour : nullptr);
	}
	// The spine, from you down to the last squadmate's branch.
	if (k > 0)
		Line(f, Vector2D(SPINE_X * f.s, (NODE_Y + NODE_HALF + 2.0f) * f.s), Vector2D(SPINE_X * f.s, spineEnd), NEO_GHOST_MEDIUM, f.color, 0.8f);
	PaintNode(f);
}
} // namespace NeoCyberbrain
