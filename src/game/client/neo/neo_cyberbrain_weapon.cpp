#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_cyberbrain_gun.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The weapon group, in shapes rather than numbers: the round ticks and the count (the one number), magazines as pips,
// the fire mode as a glyph, the aim settle as two registration crosses coming into register, the range while aiming; a
// shot flicks its tick out, a reload sweeps them back. The count hangs from the gun's own magazine on a short leader
// (neo_cyberbrain_gun.h), in the group only when it can't. Carrying the ghost, its uplink takes the group's place.

ConVar cl_neo_hud_gun_count("cl_neo_hud_gun_count", "0", FCVAR_ARCHIVE,
	"The rounds' count (and its calls) hung from the gun's muzzle: stubbed off until each gun gets a hand-placed spot;"
	" 0 keeps them in the weapon group.", true, 0, true, 1);

namespace NeoCyberbrain
{
constexpr int MAX_MAG_PIPS = 10, MAX_SLUG_PIPS = 6;
constexpr float ROUNDS_W = 150.0f, ROUNDS_Y = 12.5f;	// pixels at 1080p: the rounds' row, and its middle
constexpr float SYNC_Y = 30.0f, SYNC_ARM = 6.0f, SYNC_SPREAD = 10.0f;	// pixels at 1080p: where, a cross's arm, the most out of register
constexpr float ROW_TOP = 6.0f, OUTER_X = 84.0f, PIP_TALL = 12.0f;	// pixels at 1080p: the rounds' row's top, the outer column, a slug pip
constexpr float MODE_TALL = 13.0f;	// pixels at 1080p: the fire mode glyphs' row (AUTO's stagger, the tallest)
static const wchar_t *const WPN_KANJI = L"\u6b8b\u5f3e";

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

// The rounds on the gun itself: a short leader out from the muzzle (Kyle: the muzzle, not the magazine; level, toward
// the screen's centre side, then a drop), the count at its end, riding with the gun. False when the muzzle isn't on
// screen or sits in the crosshair's keep-out (on the sights), and the group shows the count instead.
static bool RoundsOnGun(const Frame &f, const wchar_t *pCount, const Color &c, float a)
{
	// Stubbed (Kyle: the muzzle readout sits too inconsistently across the guns; it needs placing by hand, per gun).
	if (!cl_neo_hud_gun_count.GetBool())
		return false;
	Vector2D at;
	if (!NeoCyberGunPointOnScreen(NEO_GUN_MUZZLE, at) || InKeepout(f, at))
		return false;
	const float out = static_cast<float>(-f.hand) * f.s;
	const Vector2D elbow = at + Vector2D(out * 20.0f, 0.0f), end = elbow + Vector2D(out * 8.0f, 10.0f * f.s);
	const float b = 4.0f * f.s;
	MeasurePause(true);	// at the muzzle, not the group
	Line(f, at + Vector2D(-b, -b), at + Vector2D(-b, b), NEO_GHOST_LIGHT, c, 0.7f * a);
	Line(f, at + Vector2D(b, -b), at + Vector2D(b, b), NEO_GHOST_LIGHT, c, 0.7f * a);
	Line(f, at, elbow, NEO_GHOST_LIGHT, c, 0.6f * a);
	Line(f, elbow, end, NEO_GHOST_LIGHT, c, 0.6f * a);
	Text(f, pCount, end.x + out * 4.0f, end.y + 8.0f * f.s, f.hand > 0 ? -1 : 1, FONT_NUMBER, c, a);
	MeasurePause(false);
	return true;
}

// The machine's calls (Kyle's pick): what to do while the state holds: RELOAD (the magazine's empty, spares left), LOW
// (the last magazine in, no spares), OUT in red (nothing left), OVERHEAT (the BALC can't fire). Kyle's words. Each
// blinks off once as it starts (motion only for the event). Gate 3 (Kyle, 2026-10-01, "calls on the rail"): a call takes
// the top rail's middle, where the weapon's name sits, the edge nearest the crosshair; the name yields while it's up.
enum AmmoCall { CALL_NONE, CALL_RELOAD, CALL_LAST, CALL_NONE_LEFT, CALL_OVERHEAT };

// The call this frame (null for none), whether it's red, and whether one holds the rail (through its blink too).
static const wchar_t *AmmoCallOf(const Frame &f, const Senses &s, const NeoHud::Ammo &ammo, bool &bCritical, bool &bHeld)
{
	static AmmoCall s_call = CALL_NONE;
	static float s_since = -100.0f;
	AmmoCall call = CALL_NONE;
	if (ammo.bHeat)
	{
		call = ammo.bOverheated ? CALL_OVERHEAT : CALL_NONE;
	}
	else if (!s.bReloading && ammo.maxRounds > 0 && !(ammo.pMode && !V_wcscmp(ammo.pMode, L"THROW")))
	{
		call = ammo.rounds == 0 ? (ammo.bMagsOut ? CALL_NONE_LEFT : CALL_RELOAD) : ammo.bMagsOut ? CALL_LAST : CALL_NONE;
	}
	if (call != s_call)
	{
		s_call = call;
		s_since = f.now;
	}
	bCritical = call == CALL_NONE_LEFT;
	bHeld = call != CALL_NONE;
	const float age = f.now - s_since;
	if (call == CALL_NONE || (age > 0.08f && age < 0.16f))
		return nullptr;	// none, or the one blink
	return call == CALL_RELOAD ? Word("neo_hud_cb_reload", L"RELOAD") : call == CALL_LAST ? Word("neo_hud_cb_low", L"LOW")
		: call == CALL_OVERHEAT ? Word("neo_hud_cb_overheat", L"OVERHEAT") : Word("neo_hud_cb_out", L"OUT");
}

// The rounds as ticks (weapons without a bullet glyph: the detpack): the magazine a tick each (or grouped past 30).
// A reload fills them back in on the weapon's own clock (a magazine over its whole reload; shells as the real count,
// the next one filling as it goes in); a shot flicks the tick it spent.
static void Ticks(const Frame &f, const Local &L, const Senses &s, const NeoHud::Ammo &ammo, const Color &col, float flick, float a)
{
	const float m = static_cast<float>(L.m);
	const int n = Min(ammo.maxRounds, 30);
	const float pitch = Min(6.0f, ROUNDS_W / n), w = n * pitch, per = static_cast<float>(ammo.maxRounds) / n;
	const bool bSweep = s.bReloading && !s.bReloadShells;
	const int loaded = static_cast<int>(ceilf(ammo.rounds / per - 0.001f));
	const int shown = bSweep ? static_cast<int>(s.reloadProgress * n) : loaded;
	const int filling = s.bReloading && s.bReloadShells && loaded < n ? loaded : -1;
	for (int i = 0; i < n; ++i)
	{
		const float x = m > 0.0f ? -w * 0.5f + i * pitch : w * 0.5f - i * pitch - 3.0f;
		const bool bSpent = !s.bReloading && i == shown && flick > 0.0f;
		const float lift = bSpent ? -4.0f * flick : 0.0f;
		const float tick = i < shown ? 0.85f : i == filling ? 0.13f + 0.72f * s.reloadProgress : bSpent ? 0.85f * flick : 0.13f;
		Rect(f, L.At(x, 6.0f + lift), L.At(x + 3.0f, 19.0f + lift), col, tick * a);
	}
}

// The rounds as the stock panel draws them: the weapon's own bullet glyph a round, spent ones dim; a magazine too
// long for the row ends in "+" as stock does (full, the "+" lit too). Drawn as two strings (full, spent) and the one
// glyph moving: the round a shot just spent flicking up, or the shell going in filling.
static void Bullets(const Frame &f, const Local &L, const Senses &s, const NeoHud::Ammo &ammo, const Color &col, float flick,
	float glyphW, float a)
{
	const float m = static_cast<float>(L.m);
	const int fit = Max(1, static_cast<int>(ROUNDS_W / glyphW));
	const bool bOverflow = ammo.maxRounds > fit;
	const int n = bOverflow ? fit : ammo.maxRounds;
	const int loaded = bOverflow ? (ammo.rounds >= ammo.maxRounds ? n : Min(ammo.rounds, n - 1)) : ammo.rounds;
	const bool bSweep = s.bReloading && !s.bReloadShells;
	const int shown = clamp(bSweep ? static_cast<int>(s.reloadProgress * n) : loaded, 0, n);
	wchar_t glyphs[128];
	const int count = Min(n, static_cast<int>(ARRAYSIZE(glyphs)) - 1);
	for (int i = 0; i < count; ++i)
		glyphs[i] = (bOverflow && i == count - 1) ? L'+' : ammo.bullet;
	glyphs[count] = 0;
	// The one glyph moving: the round just spent, or the shell going in.
	const bool bSpent = !s.bReloading && flick > 0.0f && shown < count;
	const bool bFilling = s.bReloading && s.bReloadShells && shown < count;
	const bool bMoving = bSpent || bFilling;
	const float w = count * glyphW, y = ROUNDS_Y;
	// Right-handed the rounds run left to right from the gun's far side; left-handed, mirrored.
	const auto slotX = [&](int i) { return m > 0.0f ? -w * 0.5f + i * glyphW : w * 0.5f - (i + 1) * glyphW; };
	wchar_t full[128], empty[128];
	V_wcsncpy(full, glyphs, sizeof(full));
	full[shown] = 0;
	const int emptyFrom = shown + (bMoving ? 1 : 0);
	V_wcsncpy(empty, glyphs + Min(emptyFrom, count), sizeof(empty));
	const int emptyN = count - Min(emptyFrom, count);
	if (m > 0.0f)
	{
		Text(f, full, L.At(slotX(0), y).x, L.At(0.0f, y).y, 1, FONT_BULLETS, col, 0.9f * a);
		if (emptyN > 0)
			Text(f, empty, L.At(slotX(emptyFrom), y).x, L.At(0.0f, y).y, 1, FONT_BULLETS, col, 0.2f * a);
	}
	else
	{
		Text(f, full, L.At(w * 0.5f, y).x, L.At(0.0f, y).y, -1, FONT_BULLETS, col, 0.9f * a);
		if (emptyN > 0)
			Text(f, empty, L.At(slotX(emptyFrom) + glyphW, y).x, L.At(0.0f, y).y, -1, FONT_BULLETS, col, 0.2f * a);
	}
	if (bMoving)
	{
		const wchar_t one[2] = { glyphs[shown], 0 };
		const float lift = bSpent ? -4.0f * flick : 0.0f;
		const float alpha = bSpent ? 0.2f + 0.7f * flick : 0.2f + 0.7f * s.reloadProgress;
		Text(f, one, L.At(slotX(shown), y + lift).x, L.At(0.0f, y + lift).y, 1, FONT_BULLETS, col, alpha * a);
	}
}

// Gate 3's stack C (art\hud-next\wstack-01.png): the top rail (the WPN plate at the side away from the gun; the name
// or a call in the middle), the rounds' row with the count on its outer side, the magazines and the mode stacked under
// the count, SYNC under the rounds, the range under SYNC's knock room. Each row sits by the measured height of what's
// in it plus a gap (R3): the faces may change, the rows don't collide.
void PaintWeapon(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const NeoHud::Ammo &ammo = s.ammo;
	const Look look = LookOf(f, GROUP_WEAPON);
	const Local L = { f.pPlaces[GROUP_WEAPON].pos, f.s * look.scale, f.hand };
	// Carrying the ghost, its uplink takes the weapon group's place (the ghost has no rounds).
	if (s.bGhost)
	{
		PaintUplink(f, L, look.alpha, look.labels);
		return;
	}
	if (!ammo.bShown)
	{
		return;
	}
	const float a = look.alpha, m = static_cast<float>(L.m);
	const int side = L.m > 0 ? 1 : -1;
	const float gap = ROW_GAP * L.k;	// gaps follow attention, text doesn't (R3)
	const auto local = [&](float screenY) { return (screenY - L.origin.y) / L.k; };	// a screen height in the group's units

	// The rounds' row (or the BALC's heat), and the count on its outer side.
	if (ammo.bHeat)
	{
		const Color hc = s.heatLevel == 2 ? CRIT : s.heatLevel == 1 ? WARN : f.color;
		const float w = 150.0f, hw = w * ammo.heat;
		Rect(f, L.At(-w * 0.5f, ROW_TOP), L.At(w * 0.5f, ROW_TOP + 9.0f), f.color, 0.12f * a);
		Rect(f, L.At(m > 0.0f ? -w * 0.5f : w * 0.5f - hw, ROW_TOP), L.At(m > 0.0f ? -w * 0.5f + hw : w * 0.5f, ROW_TOP + 9.0f), hc,
			(ammo.bOverheated ? 0.6f + 0.35f * sinf(f.now * 12.0f) : 0.85f) * a);
	}
	else if (ammo.maxRounds > 0)
	{
		const bool bLow = s.bAmmoLow;
		const Color col = bLow && !s.bReloading ? WARN : s.bReloading ? WARN : f.color;
		const float flick = 1.0f - clamp((f.now - s.shotTime) / 0.25f, 0.0f, 1.0f);
		const wchar_t bulletText[2] = { ammo.bullet, 0 };
		const float glyphW = ammo.bullet ? TextWidth(bulletText, FONT_BULLETS) / L.k : 0.0f;
		if (glyphW > 0.5f)
			Bullets(f, L, s, ammo, col, flick, glyphW, a);
		else
			Ticks(f, L, s, ammo, col, flick, a);
		wchar_t count[16];
		V_snwprintf(count, ARRAYSIZE(count), L"%d", ammo.rounds);
		const float countAlpha = Max(look.numbers, bLow ? 1.0f : 0.5f);
		const Vector2D ca = L.At(m * OUTER_X, ROUNDS_Y);
		if (!RoundsOnGun(f, count, col, countAlpha))
			Text(f, count, ca.x, ca.y, side, FONT_NUMBER, col, countAlpha);

		// Under the count (the outer side's stack): the magazines as pips (the Supa 7: shells, then slugs as taller
		// pips), or past what the pips hold the stock panel's count, never fewer shown than you carry; then the fire mode.
		Stack outer = { ca.y + FontTall(FONT_NUMBER) * 0.5f + gap, gap, 1 };
		const int mags = ammo.magCount, slugs = ammo.slugCount;
		if (mags > MAX_MAG_PIPS || slugs > MAX_SLUG_PIPS)
		{
			Text(f, ammo.mags, ca.x, outer.Row(FontTall(FONT_VALUE)), side, FONT_VALUE, f.color, 0.8f * a);
		}
		else if (mags + slugs > 0)
		{
			const float bottom = local(outer.Row(PIP_TALL * L.k)) + PIP_TALL * 0.5f;
			for (int i = 0; i < mags + slugs; ++i)
			{
				const bool bSlug = i >= mags;
				const float x = m * (OUTER_X + i * 6.0f) - (m < 0.0f ? 4.0f : 0.0f);
				Rect(f, L.At(x, bottom - (bSlug ? PIP_TALL : PIP_TALL - 4.0f)), L.At(x + 4.0f, bottom), ammo.bMagsOut ? CRIT : f.color, 0.75f * a);
			}
		}
		// The glyph centred on its row (it was centred on the row's top edge, reaching up into the pips).
		ModeGlyph(f, L, m > 0.0f ? OUTER_X : -OUTER_X - 12.0f, local(outer.Row(MODE_TALL * L.k)), ammo.pMode, 0.7f * a);
	}

	// SYNC, the aim settle, as registration (Kyle's pick over the bar): two of the HUD's registration crosses knocked out
	// of register by each shot and drifting back together as the aim settles, until they're one aligned cross with its
	// target ring: in register, ready. It moves only after a shot.
	{
		const float off = SYNC_SPREAD * (1.0f - clamp(s.sync, 0.0f, 1.0f));
		const Color sc = s.sync < 0.5f ? WARN : f.color;
		const Vector2D centre(0.0f, SYNC_Y), d(off * m, off * 0.6f);
		if (off < 0.5f)
		{
			Line(f, L.At(-SYNC_ARM, SYNC_Y), L.At(SYNC_ARM, SYNC_Y), NEO_GHOST_HEAVY, sc, 0.9f * a);
			Line(f, L.At(0.0f, SYNC_Y - SYNC_ARM), L.At(0.0f, SYNC_Y + SYNC_ARM), NEO_GHOST_HEAVY, sc, 0.9f * a);
			Arc(f, L.At(0.0f, SYNC_Y), Vector2D(4.5f, 4.5f) * L.k, 0.0f, 360.0f, NEO_GHOST_LIGHT, sc, 0.7f * a);
		}
		else
		{
			for (int k = -1; k <= 1; k += 2)
			{
				const Vector2D c = centre + d * static_cast<float>(k);
				Line(f, L.At(c.x - SYNC_ARM, c.y), L.At(c.x + SYNC_ARM, c.y), NEO_GHOST_MEDIUM, sc, 0.85f * a);
				Line(f, L.At(c.x, c.y - SYNC_ARM), L.At(c.x, c.y + SYNC_ARM), NEO_GHOST_MEDIUM, sc, 0.85f * a);
			}
		}
	}
	// The range while aiming (the rangefinder's), under the room SYNC's crosses take when knocked furthest.
	if (s.bRange)
	{
		wchar_t range[24];
		if (s.rangeMetres < 0.0f)
			V_wcsncpy(range, L"RNG ---", sizeof(range));
		else
			V_snwprintf(range, ARRAYSIZE(range), L"RNG %.0f M", s.rangeMetres);
		const Vector2D rp = L.At(0.0f, SYNC_Y + SYNC_ARM + SYNC_SPREAD * 0.6f);
		Stack foot = { rp.y + gap, gap, 1 };
		Text(f, range, rp.x, foot.Row(FontTall(FONT_VALUE)), 0, FONT_VALUE, f.color, Max(0.7f, look.numbers));
	}

	// The top rail, a gap above the rounds' row, as tall as its tallest occupant: the call, the plate and its kanji, the name.
	bool bCritical, bHeld;
	const wchar_t *pCall = AmmoCallOf(f, s, ammo, bCritical, bHeld);
	const bool bLabels = look.labels > 0.02f;
	if (!bLabels && !bHeld)
		return;
	Stack rail = { L.At(0.0f, ROW_TOP).y - gap, gap, -1 };
	const wchar_t *pLine = PlateLine("neo_hud_cb_plate_wpn", L"WPN");
	const float railY = rail.Row(Max(Max(MachinePlateTall(f), PlateTall(f, L"WPN", WPN_KANJI, pLine)), FontTall(FONT_LABEL)));
	if (pCall)
		MachinePlate(f, pCall, L.At(0.0f, 0.0f).x, railY, 0, bCritical ? 1.0f : 0.95f, bCritical);
	else if (bLabels && !bHeld)
		Text(f, ammo.name, L.At(0.0f, 0.0f).x, railY, 0, FONT_LABEL, f.color, look.labels * a);
	if (bLabels)
	{
		wchar_t word[16], line[48];
		Plate(f, Crystallise(f, GROUP_WEAPON, L"WPN", word, ARRAYSIZE(word)), L.At(m * -OUTER_X, 0.0f).x, railY, -side, look.labels, WPN_KANJI,
			Crystallise(f, GROUP_WEAPON, pLine, line, ARRAYSIZE(line)));
	}
}
} // namespace NeoCyberbrain
