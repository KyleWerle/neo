#include "cbase.h"
#include "neo_competitive.h"
#include "neo_hud_model_ammo.h"
#include "neo_hud_model_callouts.h"
#include "c_neo_player.h"
#include "c_team.h"
#include "neo_gamerules.h"
#include "view.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The Competitive HUD's vitals, in the stock panels' places as lowercase text: integrity, therm-optic and aux bottom
// left (the classes that have them), the weapon bottom right (its name, rounds of the magazine, magazines or the Supa
// 7's shells + slugs, the fire mode; the BALC's heat; grenades by count), the compass bottom centre (the stock rose
// over its 90 degrees, the objective's mark in its carrier's colour, the ghost's callouts in red with the newest's
// distance), and the rangefinder where the stock one sits while aiming.

namespace NeoCompetitive
{
constexpr float VITALS_VALUE_X = 200.0f;			// where the vitals' values end
constexpr float COMPASS_Y = 1044.0f, COMPASS_W = 450.0f, COMPASS_FOV = 90.0f;

static void Health(const Pen &pen, C_NEO_Player *pPlayer)
{
	static ConVarRef cl_neo_hud_health_mode("cl_neo_hud_health_mode");
	const float s = pen.s, large = Height(FACE_LARGE), line = large + 2.0f * s;
	const int neoClass = pPlayer->GetClass();
	const bool bSupport = neoClass == NEO_CLASS_SUPPORT, bJuggernaut = neoClass == NEO_CLASS_JUGGERNAUT;
	struct Row { const wchar_t *pLabel; int value; };
	Row rows[3];
	int count = 0;
	rows[count++] = { L"integrity", pPlayer->GetDisplayedHealth(cl_neo_hud_health_mode.GetInt()) };
	if (!bSupport && !bJuggernaut)
		rows[count++] = { L"therm-optic", static_cast<int>(roundf(pPlayer->m_HL2Local.m_cloakPower)) };
	if (!bSupport)
		rows[count++] = { L"aux power", static_cast<int>(pPlayer->m_HL2Local.m_flSuitPower) };
	const float bottom = pen.tall - EDGE * s, top = bottom - count * line, pad = BOX_PAD * s;
	Box(EDGE * s - pad, top - pad, VITALS_VALUE_X * s + pad, bottom - 2.0f * s + pad);
	for (int i = 0; i < count; ++i)
	{
		const float y = top + i * line;
		wchar_t value[16];
		V_snwprintf(value, ARRAYSIZE(value), L"%d", rows[i].value);
		Print(pen, rows[i].pLabel, EDGE * s, y + (large - Height(FACE_TEXT)) * 0.5f, 1, FACE_TEXT, FADED);
		Print(pen, value, VITALS_VALUE_X * s, y, -1, FACE_LARGE, WHITE);
	}
}

static void Ammo(const Pen &pen, C_NEO_Player *pPlayer)
{
	NeoHud::Ammo ammo;
	NeoHud::ReadAmmo(pPlayer, ammo);
	if (!ammo.bShown)
		return;
	const float s = pen.s, right = pen.wide - EDGE * s, bottom = pen.tall - EDGE * s, gap = 12.0f * s, pad = BOX_PAD * s;
	const float large = Height(FACE_LARGE), text = Height(FACE_TEXT);
	const float y1 = bottom - large, y0 = y1 - text - 4.0f * s, yText = y1 + (large - text) * 0.5f;

	// The bottom row, measured before it's drawn so the box fits: the mode, the rounds (or the heat), the magazines.
	wchar_t main[24] = L"";
	const wchar_t *pMode = nullptr, *pMags = nullptr;
	Color mainColour = WHITE;
	if (ammo.bHeat)
	{
		if (ammo.bOverheated)
			V_wcsncpy(main, L"overheat", sizeof(main));
		else
			V_snwprintf(main, ARRAYSIZE(main), L"heat %d%%", RoundFloatToInt(ammo.heat * 100.0f));
		mainColour = ammo.bOverheated || ammo.heat > 0.8f ? RED : WHITE;
	}
	else if (ammo.maxRounds > 0)
	{
		const bool bThrown = ammo.pMode && !V_wcscmp(ammo.pMode, L"THROW");
		if (bThrown)
			V_snwprintf(main, ARRAYSIZE(main), L"x%d", ammo.rounds);
		else
			V_snwprintf(main, ARRAYSIZE(main), L"%d/%d", ammo.rounds, ammo.maxRounds);
		mainColour = ammo.rounds == 0 ? RED : WHITE;
		pMode = bThrown ? nullptr : ammo.pMode;
		pMags = ammo.mags[0] ? ammo.mags : nullptr;
	}
	const float magsW = pMags ? Width(pMags, FACE_TEXT) + gap : 0.0f, mainW = Width(main, FACE_LARGE);
	const float modeW = pMode ? Width(pMode, FACE_TEXT) + gap : 0.0f;
	const float rowW = magsW + mainW + modeW, width = Max(rowW, Width(ammo.name, FACE_TEXT));
	Box(right - width - pad, (main[0] ? y0 : y1) - pad, right + pad, bottom - 2.0f * s + pad);

	Print(pen, ammo.name, right, main[0] ? y0 : y1, -1, FACE_TEXT, FADED);
	float x = right;
	if (pMags)
		x -= Print(pen, pMags, x, yText, -1, FACE_TEXT, ammo.bMagsOut ? RED : FADED) + gap;
	x -= Print(pen, main, x, y1, -1, FACE_LARGE, mainColour) + gap;
	if (pMode)
		Print(pen, pMode, x, yText, -1, FACE_TEXT, FADED);
}

// A bearing (degrees from ahead, right positive) across the compass, or false outside its view.
static bool CompassX(const Pen &pen, float bearing, float &x)
{
	if (fabsf(bearing) > COMPASS_FOV * 0.5f)
		return false;
	x = pen.wide * 0.5f + (bearing / COMPASS_FOV) * COMPASS_W * pen.s;
	return true;
}

static void Compass(const Pen &pen, C_NEO_Player *pPlayer)
{
	static const wchar_t *s_rose[] = { L"s", L"sw", L"w", L"nw", L"n", L"ne", L"e", L"se" };
	const float s = pen.s, y = COMPASS_Y * s - Height(FACE_TEXT) * 0.5f, yaw = MainViewAngles()[YAW], pad = BOX_PAD * s;
	const float over = (Height(FACE_LARGE) - Height(FACE_TEXT)) * 0.5f;	// the cardinals stand above and below the line
	Box(pen.wide * 0.5f - COMPASS_W * 0.5f * s - pad, y - over - pad * 0.5f, pen.wide * 0.5f + COMPASS_W * 0.5f * s + pad,
		y + Height(FACE_TEXT) + over + pad * 0.5f);
	// The stock rose: S at world yaw 0's opposite; each point's bearing from where you look.
	for (int i = 0; i < 8; ++i)
	{
		const float bearing = AngleNormalize(i * 45.0f + yaw);
		float x;
		if (!CompassX(pen, bearing, x))
			continue;
		// The cardinals large and full white; the points between smaller, fading toward the ends (as stock).
		const float edge = 1.0f - fabsf(bearing) / (COMPASS_FOV * 0.5f);
		if (i % 2 == 0)
			Print(pen, s_rose[i], x, y - (Height(FACE_LARGE) - Height(FACE_TEXT)) * 0.5f, 0, FACE_LARGE, WHITE);
		else
			Print(pen, s_rose[i], x, y, 0, FACE_TEXT, Color(255, 255, 255, static_cast<int>(90 + 165 * edge)));
	}
	Print(pen, L"|", pen.wide * 0.5f, y - over - Height(FACE_TEXT), 0, FACE_TEXT, FADED);
	const float markY = y - over - Height(FACE_TEXT) - 2.0f * s;
	const auto bearingTo = [&](const Vector &at) { const Vector d = at - MainViewOrigin(); return AngleNormalize(yaw - RAD2DEG(atan2f(d.y, d.x))); };

	// The objective, clamped to the edge as the stock arrow, in its carrier's colour; not while you carry it.
	const bool bObjective = NEORules()->GhostExists() || NEORules()->GetJuggernautMarkerPos() != vec3_origin;
	if (bObjective && !pPlayer->IsObjective())
	{
		const Vector at = NEORules()->GetGameType() == NEO_GAME_TYPE_JGR ? NEORules()->GetJuggernautMarkerPos() : NEORules()->GetGhostPos();
		float x;
		CompassX(pen, clamp(bearingTo(at), -COMPASS_FOV * 0.5f, COMPASS_FOV * 0.5f), x);
		const int ghoster = NEORules()->GetGhosterTeam();
		const bool bCarried = ghoster == TEAM_JINRAI || ghoster == TEAM_NSF;
		const Color c = !bCarried ? WHITE : ghoster != pPlayer->GetTeamNumber() ? RED
			: (ghoster == TEAM_JINRAI ? COLOR_NEO_GREEN : COLOR_NEO_BLUE);
		Print(pen, L"v", x, markY, 0, FACE_TEXT, c);
	}
	// The ghost's callouts, red and fading, the newest with its distance.
	NeoHud::Callout callouts[NeoHud::MAX_CALLOUTS];
	int newest;
	const int calloutCount = NeoHud::ReadCallouts(gpGlobals->realtime, callouts, newest);
	for (int i = 0; i < calloutCount; ++i)
	{
		const NeoHud::Callout &c = callouts[i];
		float x;
		CompassX(pen, clamp(AngleNormalize(yaw - c.yaw), -COMPASS_FOV * 0.5f, COMPASS_FOV * 0.5f), x);
		const Color red(255, 0, 0, clamp(RoundFloatToInt(255.0f * c.life), 0, 255));
		Print(pen, L"v", x, markY, 0, FACE_TEXT, red);
		if (i == newest)
		{
			wchar_t metres[16];
			V_snwprintf(metres, ARRAYSIZE(metres), L"%dm", static_cast<int>(c.metres));
			Print(pen, metres, x, markY - Height(FACE_TEXT), 0, FACE_TEXT, red);
		}
	}
}

// The rangefinder, where the stock one sits, while aiming.
static void Range(const Pen &pen, C_NEO_Player *pPlayer)
{
	static ConVarRef enabled("cl_neo_hud_rangefinder_enabled"), fracX("cl_neo_hud_rangefinder_pos_frac_x"),
		fracY("cl_neo_hud_rangefinder_pos_frac_y");
	if (!enabled.GetBool() || !pPlayer->IsInAim())
		return;
	Vector forward;
	AngleVectors(MainViewAngles(), &forward);
	trace_t tr;
	UTIL_TraceLine(MainViewOrigin(), MainViewOrigin() + forward * MAX_TRACE_LENGTH, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr);
	const float metres = METERS_PER_INCH * tr.startpos.DistTo(tr.endpos);
	wchar_t range[24];
	if (metres >= 999.0f || (tr.surface.flags & (SURF_SKY | SURF_SKY2D)))
		V_wcsncpy(range, L"range ---m", sizeof(range));
	else
		V_snwprintf(range, ARRAYSIZE(range), L"range %.0fm", metres);
	Print(pen, range, pen.wide * fracX.GetFloat(), pen.tall * fracY.GetFloat(), 1, FACE_TEXT, WHITE);
}

void PaintVitals(const Pen &pen, bool bHealth, bool bAmmo, bool bCompass)
{
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (bHealth)
		Health(pen, pPlayer);
	if (bAmmo)
		Ammo(pen, pPlayer);
	if (bCompass)
	{
		Compass(pen, pPlayer);
		Range(pen, pPlayer);
	}
}
} // namespace NeoCompetitive
