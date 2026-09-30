#include "cbase.h"
#include "neo_competitive.h"
#include "neo_cyberbrain_internal.h"
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
constexpr float METRES_PER_UNIT = 0.0254f;

static void Health(const Pen &pen, C_NEO_Player *pPlayer)
{
	static ConVarRef cl_neo_hud_health_mode("cl_neo_hud_health_mode");
	const float s = pen.s, line = Height(FACE_LARGE) + 2.0f * s;
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
	const float bottom = pen.tall - EDGE * s;
	for (int i = 0; i < count; ++i)
	{
		const float y = bottom - (count - i) * line;
		wchar_t value[16];
		V_snwprintf(value, ARRAYSIZE(value), L"%d", rows[i].value);
		Print(pen, rows[i].pLabel, EDGE * s, y + (Height(FACE_LARGE) - Height(FACE_TEXT)) * 0.5f, 1, FACE_TEXT, FADED);
		Print(pen, value, VITALS_VALUE_X * s, y, -1, FACE_LARGE, WHITE);
	}
}

static void Ammo(const Pen &pen, C_NEO_Player *pPlayer)
{
	NeoQuickInfo::Ammo ammo;
	NeoQuickInfo::ReadAmmo(pPlayer, ammo);
	if (!ammo.bShown)
		return;
	const float s = pen.s, right = pen.wide - EDGE * s, bottom = pen.tall - EDGE * s;
	const float large = Height(FACE_LARGE), text = Height(FACE_TEXT);
	const float y1 = bottom - large, y0 = y1 - text - 4.0f * s;
	Print(pen, ammo.name, right, y0, -1, FACE_TEXT, FADED);
	if (ammo.bHeat)
	{
		wchar_t heat[24];
		if (ammo.bOverheated)
			V_wcsncpy(heat, L"overheat", sizeof(heat));
		else
			V_snwprintf(heat, ARRAYSIZE(heat), L"heat %d%%", RoundFloatToInt(ammo.heat * 100.0f));
		Print(pen, heat, right, y1, -1, FACE_LARGE, ammo.bOverheated || ammo.heat > 0.8f ? RED : WHITE);
		return;
	}
	if (ammo.maxRounds <= 0)
		return;
	// Right to left: the magazines, the rounds, the mode.
	float x = right;
	if (ammo.mags[0])
	{
		x -= Print(pen, ammo.mags, x, y1 + (large - text) * 0.5f, -1, FACE_TEXT, ammo.bMagsOut ? RED : FADED) + 12.0f * s;
	}
	wchar_t rounds[24];
	if (ammo.pMode && !V_wcscmp(ammo.pMode, L"THROW"))
		V_snwprintf(rounds, ARRAYSIZE(rounds), L"x%d", ammo.rounds);
	else
		V_snwprintf(rounds, ARRAYSIZE(rounds), L"%d/%d", ammo.rounds, ammo.maxRounds);
	x -= Print(pen, rounds, x, y1, -1, FACE_LARGE, ammo.rounds == 0 ? RED : WHITE) + 12.0f * s;
	if (ammo.pMode && V_wcscmp(ammo.pMode, L"THROW"))
		Print(pen, ammo.pMode, x, y1 + (large - text) * 0.5f, -1, FACE_TEXT, FADED);
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
	const float s = pen.s, y = COMPASS_Y * s - Height(FACE_TEXT) * 0.5f, yaw = MainViewAngles()[YAW];
	// The stock rose: S at world yaw 0's opposite; each point's bearing from where you look.
	for (int i = 0; i < 8; ++i)
	{
		const float bearing = AngleNormalize(i * 45.0f + yaw);
		float x;
		if (!CompassX(pen, bearing, x))
			continue;
		const float edge = 1.0f - fabsf(bearing) / (COMPASS_FOV * 0.5f);
		const Color c(255, 255, 255, static_cast<int>(90 + 165 * edge));
		Print(pen, s_rose[i], x, y, 0, FACE_TEXT, c);
	}
	Print(pen, L"|", pen.wide * 0.5f, y - Height(FACE_TEXT), 0, FACE_TEXT, FADED);
	const float markY = y - Height(FACE_TEXT) - 2.0f * s;
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
	NeoCyberbrain::Senses senses;
	NeoCyberbrain::SenseCallouts(gpGlobals->realtime, senses);
	for (int i = 0; i < senses.calloutCount; ++i)
	{
		const NeoCyberbrain::Callout &c = senses.callout[i];
		float x;
		CompassX(pen, clamp(AngleNormalize(yaw - c.yaw), -COMPASS_FOV * 0.5f, COMPASS_FOV * 0.5f), x);
		const Color red(255, 0, 0, clamp(RoundFloatToInt(255.0f * c.life), 0, 255));
		Print(pen, L"v", x, markY, 0, FACE_TEXT, red);
		if (i == senses.calloutNewest)
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
	const float metres = METRES_PER_UNIT * tr.startpos.DistTo(tr.endpos);
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
