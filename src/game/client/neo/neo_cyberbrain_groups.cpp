#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The other receptor groups, in shapes rather than numbers. Optics: how visible you are (an iris that opens with the
// light you stand in, the therm-optic cells round it, shimmering hollow while cloaked, vision mode lighting its
// centre). Weapon: the round ticks and the count (the one number), magazines as pips, the fire mode as a glyph, the
// aim settle as a bar; a shot flicks its tick out, a reload sweeps them back. Link: ping as signal bars, neural load
// as cells, the squad as pips. Words and plates only come in with attention.

namespace NeoCyberbrain
{
static void FillCircle(const Frame &f, const Vector2D &c, float r, const Color &col, float a)
{
	NeoGhostBegin(col, Alpha(f, a));
	constexpr int SIDES = 12;
	for (int i = 0; i < SIDES; ++i)
	{
		const float q0 = 2.0f * M_PI_F * i / SIDES, q1 = 2.0f * M_PI_F * (i + 1) / SIDES;
		const Vector2D p0 = c + Vector2D(cosf(q0), sinf(q0)) * r, p1 = c + Vector2D(cosf(q1), sinf(q1)) * r;
		const Vector2D quad[4] = { c, p0, p1, p1 };
		NeoGhostFill(quad);
	}
}
void PaintOptics(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const Look look = LookOf(f, GROUP_OPTICS);
	const Local L = { f.pPlaces[GROUP_OPTICS].pos, f.s * look.scale, f.hand };
	const float a = look.alpha, m = static_cast<float>(L.m);
	const int side = L.m > 0 ? 1 : -1;
	const Vector2D c = L.origin;
	const float outer = 34.0f * L.k, aperture = (7.0f + 20.0f * s.light) * L.k;
	const bool bExposed = s.light > 0.6f && !s.bCloaked;
	const Color iris = bExposed ? WARN : f.color;
	// The iris, dashed while cloaked; its blades close in the dark and open in the light.
	for (int i = 0; i < 24; i += (s.bCloaked ? 2 : 1))
	{
		Arc(f, c, Vector2D(outer, outer), i * 15.0f, i * 15.0f + (s.bCloaked ? 10.0f : 15.0f), NEO_GHOST_MEDIUM, iris, 0.7f * a);
	}
	for (int i = 0; i < 6; ++i)
	{
		const float q = i * M_PI_F / 3.0f + 0.3f;
		Line(f, c + Vector2D(cosf(q), sinf(q)) * aperture, c + Vector2D(cosf(q + 0.9f), sinf(q + 0.9f)) * outer, NEO_GHOST_LIGHT, iris, 0.45f * a);
	}
	Arc(f, c, Vector2D(aperture, aperture), 0.0f, 360.0f, NEO_GHOST_LIGHT, iris, 0.6f * a);
	const bool bJuggernaut = s.neoClass == NEO_CLASS_JUGGERNAUT;
	if (s.bVision || bJuggernaut)
	{
		const float pulse = 0.22f + 0.08f * sinf((f.now - s.visionChanged) * 4.0f);
		FillCircle(f, c, aperture, f.color, pulse * a);
	}
	// The therm-optic as a tank under the iris, eight segments, filling from the gun side's far end: hollow while
	// cloaked (it's draining), its edge pulsing as it recharges.
	if (s.bHasCloak)
	{
		const Vector2D p0 = L.At(-44.0f, 46.0f), p1 = L.At(44.0f, 57.0f);
		TankStyle style;
		style.segments = 8;
		style.dir = L.m > 0 ? 1 : -1;
		style.bHollow = s.bCloaked;
		style.bCharging = !s.bCloaked && s.cloak < 0.995f;
		Tank(f, Vector2D(Min(p0.x, p1.x), p0.y), Vector2D(Max(p0.x, p1.x), p1.y), s.cloak, style, a);
	}
	// A word only when it's pulled in.
	if (look.labels > 0.02f)
	{
		const wchar_t *pWord = s.bCloaked ? L"CLOAKED" : bExposed ? L"EXPOSED" : s.light > 0.3f ? L"LIT" : s.light > 0.15f ? L"DIM" : L"DARK";
		const Vector2D wa = L.At(m * 56.0f, 0.0f);
		Text(f, pWord, wa.x, wa.y, side, FONT_LABEL, bExposed ? WARN : f.color, look.labels * a);
		const Vector2D pa = L.At(m * -40.0f, -58.0f);
		Plate(f, L"OPTICS", pa.x, pa.y, -side, look.labels, L"\u5149\u5b66");
	}
	if (s.bVision && s.pVision)
	{
		const Vector2D pa = L.At(0.0f, 74.0f);
		Plate(f, s.pVision, pa.x, pa.y, 0, 0.9f);
	}
	else if (bJuggernaut)
	{
		const Vector2D pa = L.At(0.0f, 74.0f);
		Plate(f, L"JGR56 ACTIVE", pa.x, pa.y, 0, 0.9f);
	}
}

// The fire mode as a glyph: AUTO three rounds, SEMI one, BUCK a spread, SLUG one heavy, THROW an arc.
static void ModeGlyph(const Frame &f, const Local &L, float x, float y, const wchar_t *pMode, float a)
{
	if (!pMode)
		return;
	const auto round = [&](float rx, float ry, float w, float h) { Rect(f, L.At(rx, ry), L.At(rx + w, ry + h), f.color, a); };
	if (!V_wcscmp(pMode, L"AUTO"))
	{
		for (int i = 0; i < 3; ++i)
			round(x + i * 5.0f, y - 4.0f - i * 2.0f, 3.0f, 9.0f);
	}
	else if (!V_wcscmp(pMode, L"SEMI"))
	{
		round(x, y - 5.0f, 3.0f, 10.0f);
	}
	else if (!V_wcscmp(pMode, L"BUCK"))
	{
		static const float s_dots[][2] = { { 0, 0 }, { 5, -4 }, { 5, 4 }, { 10, -2 }, { 10, 5 } };
		for (const auto &d : s_dots)
			round(x + d[0], y + d[1] - 1.5f, 3.0f, 3.0f);
	}
	else if (!V_wcscmp(pMode, L"SLUG"))
	{
		round(x, y - 5.0f, 6.0f, 10.0f);
	}
	else
	{
		Arc(f, L.At(x + 6.0f, y + 4.0f), Vector2D(7.0f, 7.0f) * L.k, -70.0f, 70.0f, NEO_GHOST_MEDIUM, f.color, a);
	}
}

void PaintWeapon(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const NeoQuickInfo::Ammo &ammo = s.ammo;
	if (!ammo.bShown)
	{
		return;
	}
	const Look look = LookOf(f, GROUP_WEAPON);
	const Local L = { f.pPlaces[GROUP_WEAPON].pos, f.s * look.scale, f.hand };
	const float a = look.alpha, m = static_cast<float>(L.m);
	const int side = L.m > 0 ? 1 : -1;
	if (ammo.bHeat)
	{
		const Color hc = ammo.heat > 0.8f ? CRIT : ammo.heat > 0.5f ? WARN : f.color;
		const float w = 150.0f, hw = w * ammo.heat;
		Rect(f, L.At(-w * 0.5f, 6.0f), L.At(w * 0.5f, 15.0f), f.color, 0.12f * a);
		Rect(f, L.At(m > 0.0f ? -w * 0.5f : w * 0.5f - hw, 6.0f), L.At(m > 0.0f ? -w * 0.5f + hw : w * 0.5f, 15.0f), hc,
			(ammo.bOverheated ? 0.6f + 0.35f * sinf(f.now * 12.0f) : 0.85f) * a);
	}
	else if (ammo.maxRounds > 0)
	{
		const int n = Min(ammo.maxRounds, 30);
		const float pitch = Min(6.0f, 150.0f / n), w = n * pitch, per = static_cast<float>(ammo.maxRounds) / n;
		const bool bLow = ammo.rounds <= ammo.maxRounds / 5;
		// A reload sweeps the ticks back in; a shot flicks the tick it spent.
		const float sweep = s.bReloading ? fmodf(f.now - s.reloadStart, 1.0f) : 1.0f;
		const int shown = s.bReloading ? static_cast<int>(sweep * n) : static_cast<int>(ceilf(ammo.rounds / per - 0.001f));
		const float flick = 1.0f - clamp((f.now - s.shotTime) / 0.25f, 0.0f, 1.0f);
		const Color col = bLow && !s.bReloading ? WARN : s.bReloading ? WARN : f.color;
		for (int i = 0; i < n; ++i)
		{
			const float x = m > 0.0f ? -w * 0.5f + i * pitch : w * 0.5f - i * pitch - 3.0f;
			const bool bSpent = !s.bReloading && i == shown && flick > 0.0f;
			const float lift = bSpent ? -4.0f * flick : 0.0f;
			Rect(f, L.At(x, 6.0f + lift), L.At(x + 3.0f, 19.0f + lift), col, (i < shown ? 0.85f : bSpent ? 0.85f * flick : 0.13f) * a);
		}
		wchar_t count[16];
		V_snwprintf(count, ARRAYSIZE(count), L"%d", ammo.rounds);
		const Vector2D ca = L.At(m * 84.0f, 12.0f);
		Text(f, count, ca.x, ca.y, side, FONT_VALUE_LARGE, col, Max(look.numbers, bLow ? 1.0f : 0.5f));
		ModeGlyph(f, L, m > 0.0f ? 84.0f : -96.0f, 34.0f, ammo.pMode, 0.7f * a);
		// Magazines as pips over the count (the Supa 7: shells, then slugs as taller pips).
		int mags = 0, slugs = 0;
		if (ammo.mags[0])
		{
			swscanf(ammo.mags, L"%d+%d", &mags, &slugs);
		}
		const int shownMags = Min(mags, 10), shownSlugs = Min(slugs, 6);
		for (int i = 0; i < shownMags + shownSlugs; ++i)
		{
			const bool bSlug = i >= shownMags;
			const float x = m * (84.0f + i * 6.0f) - (m < 0.0f ? 4.0f : 0.0f);
			Rect(f, L.At(x, bSlug ? -16.0f : -12.0f), L.At(x + 4.0f, -4.0f), ammo.bMagsOut ? CRIT : f.color, 0.75f * a);
		}
	}
	// SYNC: the aim settle, dipping on each shot.
	const float sw = 120.0f;
	Rect(f, L.At(-sw * 0.5f, 28.0f), L.At(sw * 0.5f, 31.0f), f.color, 0.1f * a);
	Rect(f, L.At(-sw * 0.5f, 28.0f), L.At(-sw * 0.5f + sw * s.sync, 31.0f), s.sync < 0.5f ? WARN : f.color, 0.7f * a);
	if (look.labels > 0.02f)
	{
		const Vector2D na = L.At(0.0f, -22.0f);
		Text(f, ammo.name, na.x, na.y, 0, FONT_LABEL, f.color, look.labels * a);
		const Vector2D pa = L.At(m * -84.0f, -22.0f);
		Plate(f, L"WPN", pa.x, pa.y, -side, look.labels, L"\u6b8b\u5f3e");
	}
}

void PaintLink(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const Look look = LookOf(f, GROUP_LINK);
	const Local L = { f.pPlaces[GROUP_LINK].pos, f.s * look.scale, f.hand };
	const float a = look.alpha, m = static_cast<float>(L.m);
	const int side = L.m > 0 ? 1 : -1;
	// Ping as signal bars, amber and red as it climbs.
	const int bars = s.ping < 50 ? 4 : s.ping < 90 ? 3 : s.ping < 150 ? 2 : 1;
	const Color pc = s.ping >= 200 ? CRIT : s.ping >= 100 ? WARN : f.color;
	for (int i = 0; i < 4; ++i)
	{
		const float x = m * (i * 6.0f) - (m < 0.0f ? 4.0f : 0.0f);
		Rect(f, L.At(x, -4.0f - i * 4.0f), L.At(x + 4.0f, 4.0f), pc, (i < bars ? 0.8f : 0.15f) * a);
	}
	// Neural load: a cell a system drawing on you.
	for (int i = 0; i < 6; ++i)
	{
		const float x = m > 0.0f ? 34.0f + i * 9.0f : -34.0f - i * 9.0f - 7.0f;
		Rect(f, L.At(x, -4.0f), L.At(x + 7.0f, 3.0f), s.load > 4 && i < s.load ? WARN : f.color, (i < s.load ? 0.8f : 0.13f) * a);
	}
	// The squad: a pip each, filled while alive.
	for (int i = 0; i < Min(s.squadTotal, 8); ++i)
	{
		const float x = m > 0.0f ? i * 11.0f : -i * 11.0f - 8.0f;
		RectOutline(f, L.At(x, 14.0f), L.At(x + 8.0f, 22.0f), NEO_GHOST_LIGHT, TEAM_OURS, 0.8f * a);
		if (i < s.squadAlive)
		{
			Rect(f, L.At(x + 2.0f, 16.0f), L.At(x + 6.0f, 20.0f), TEAM_OURS, 0.8f * a);
		}
	}
	if (look.labels > 0.02f)
	{
		const Vector2D ta = L.At(0.0f, -24.0f);
		Plate(f, L"LINK", ta.x, ta.y, side, look.labels, L"\u901a\u4fe1");
	}
}
} // namespace NeoCyberbrain
