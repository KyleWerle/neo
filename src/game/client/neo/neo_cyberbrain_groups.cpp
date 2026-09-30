#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_cyberbrain_gun.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The other receptor groups, in shapes rather than numbers. Optics: how visible you are (a halftone patch that fills
// with the light you stand in, hollow and displaced while cloaked, vision mode lighting its brackets; the therm-optic
// tank under it). Nothing loops: motion is for events. Weapon: the round ticks and the count (the one number), magazines as pips, the fire mode as a glyph, the
// aim settle as two registration crosses coming into register, the range while aiming; a shot flicks its tick out, a reload sweeps them back. The count hangs
// from the gun's own magazine on a short leader (neo_cyberbrain_gun.h), in the group only when it can't. Link: a node
// graph (your team, your ping as the links' line, your load as your node's fill). Words and plates only come in with
// attention.

namespace NeoCyberbrain
{
constexpr int MAX_MAG_PIPS = 10, MAX_SLUG_PIPS = 6;
constexpr int HALFTONE_CELLS = 5;
constexpr float CLOAK_BAR = 5.0f;		// the therm-optic frame's bars, pixels at 1080p
constexpr int LINK_MAX_MATES = 9;
constexpr float LINK_YOU_Y = 14.0f, LINK_RADIUS = 30.0f, LINK_FAN = 70.0f;	// pixels at 1080p; degrees either side
constexpr float ROUNDS_W = 150.0f, ROUNDS_Y = 12.5f;	// pixels at 1080p: the rounds' row, and its middle
constexpr float SYNC_Y = 30.0f, SYNC_ARM = 6.0f, SYNC_SPREAD = 10.0f;	// pixels at 1080p: where, a cross's arm, the most out of register
constexpr float HALFTONE_PITCH = 13.0f, HALFTONE_MIN = 1.5f;	// pixels at 1080p: a cell, and a square in the dark

void PaintOptics(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const Look look = LookOf(f, GROUP_OPTICS);
	const Local L = { f.pPlaces[GROUP_OPTICS].pos, f.s * look.scale, f.hand };
	const float a = look.alpha, m = static_cast<float>(L.m);
	const int side = L.m > 0 ? 1 : -1;
	// The halftone patch (Kyle's pick over the lens): a grid of squares that grow with the light you stand in, so a
	// dense block reads as solid to other eyes. A slight diagonal ramp makes it read as print halftone rather than a
	// uniform grid. Exposed, it goes amber. Cloaked, the squares go hollow and alternate rows sit displaced, as the
	// therm-optic's shimmer, without moving. Vision mode lights the brackets round it (a flash as it comes on).
	const bool bExposed = s.bExposed;
	const Color dot = bExposed ? WARN : f.color;
	const float half = HALFTONE_CELLS * HALFTONE_PITCH * 0.5f;
	for (int row = 0; row < HALFTONE_CELLS; ++row)
	{
		const float shift = s.bCloaked && (row & 1) ? 3.0f * m : 0.0f;
		for (int col = 0; col < HALFTONE_CELLS; ++col)
		{
			const float ramp = (row + (m > 0.0f ? col : HALFTONE_CELLS - 1 - col)) / (2.0f * (HALFTONE_CELLS - 1));
			const float t = clamp(s.light * 1.25f - 0.25f * ramp, 0.0f, 1.0f);
			const float size = HALFTONE_MIN + (HALFTONE_PITCH - 2.0f - HALFTONE_MIN) * t;
			const float cx = -half + (col + 0.5f) * HALFTONE_PITCH + shift, cy = -half + (row + 0.5f) * HALFTONE_PITCH;
			const Vector2D p0 = L.At(cx - size * 0.5f, cy - size * 0.5f), p1 = L.At(cx + size * 0.5f, cy + size * 0.5f);
			if (s.bCloaked)
				RectOutline(f, p0, p1, NEO_GHOST_LIGHT, dot, 0.6f * a);
			else
				Rect(f, p0, p1, dot, 0.85f * a);
		}
	}
	const bool bJuggernaut = s.neoClass == NEO_CLASS_JUGGERNAUT;
	const float flash = expf(-Max(0.0f, f.now - s.visionChanged) / 0.25f);
	const float bracket = (s.bVision || bJuggernaut) ? 0.75f + 0.25f * flash : 0.25f;
	const float edge = half + (s.bHasCloak ? 14.0f : 4.0f), arm = 7.0f;
	for (int corner = 0; corner < 4; ++corner)
	{
		const float sx = (corner & 1) ? 1.0f : -1.0f, sy = (corner & 2) ? 1.0f : -1.0f;
		const Vector2D at = L.At(sx * edge, sy * edge);
		Line(f, at, L.At(sx * (edge - arm), sy * edge), NEO_GHOST_MEDIUM, f.color, bracket * a);
		Line(f, at, L.At(sx * edge, sy * (edge - arm)), NEO_GHOST_MEDIUM, f.color, bracket * a);
	}
	// The therm-optic as the patch's frame (Kyle's pick): eight segments round it, two a side, filling clockwise from
	// the top corner away from the gun, the last one part-lit as it charges; amber under a quarter. Cloaked (it's
	// draining), the lit segments go to outlines and sit displaced outward, as the patch's rows do. Solid bars, not
	// lines (Kyle: much thicker, it was a hard read), on a faint track of the same weight.
	if (s.bHasCloak)
	{
		const float r = half + 6.0f, out = s.bCloaked ? 2.0f : 0.0f, seg = r, thick = CLOAK_BAR * 0.5f;	// each segment half a side
		const Vector2D corners[4] = { Vector2D(-r, -r), Vector2D(r, -r), Vector2D(r, r), Vector2D(-r, r) };
		const Color frame = s.cloak < 0.25f ? WARN : f.color;
		const float lit = s.cloak * 8.0f;
		for (int i = 0; i < 8; ++i)
		{
			// Segment i: the half side from corner i/2, clockwise (mirrored with the hand).
			const Vector2D c0 = corners[i / 2], c1 = corners[(i / 2 + 1) % 4], dir = (c1 - c0) / (2.0f * r);
			const Vector2D normal(dir.y, -dir.x);
			const Vector2D from = c0 + dir * (seg * (i % 2) + 1.5f), to = c0 + dir * (seg * (i % 2 + 1) - 1.5f);
			const float t = clamp(lit - i, 0.0f, 1.0f);
			const auto at = [&](const Vector2D &p) { const Vector2D q = p + normal * out; return L.At(q.x * m, q.y); };
			// A bar from p0 to p1, the frame's weight across it.
			const auto bar = [&](const Vector2D &p0, const Vector2D &p1, bool bFill, const Color &c, float alpha)
			{
				const Vector2D q[4] = { at(p0 - normal * thick), at(p1 - normal * thick), at(p1 + normal * thick), at(p0 + normal * thick) };
				if (bFill)
				{
					NeoGhostBegin(c, Alpha(f, alpha));
					NeoGhostFill(q);
				}
				else
				{
					for (int k = 0; k < 4; ++k)
						Line(f, q[k], q[(k + 1) % 4], NEO_GHOST_MEDIUM, c, alpha);
				}
			};
			bar(from, to, true, f.color, 0.15f * a);
			if (t > 0.0f)
				bar(from, from + (to - from) * t, !s.bCloaked, frame, (t < 1.0f ? 0.75f : 0.9f) * a);
		}
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

// The rounds on the gun itself: a short leader out from the muzzle (Kyle: the muzzle, not the magazine; level, toward
// the screen's centre side, then a drop), the count at its end, riding with the gun. False when the muzzle isn't on
// screen or sits in the crosshair's keep-out (on the sights), and the group shows the count instead.
static bool RoundsOnGun(const Frame &f, const wchar_t *pCount, const Color &c, float a, Vector2D &below, int &align)
{
	Vector2D at;
	if (!NeoCyberGunPointOnScreen(NEO_GUN_MUZZLE, at) || InKeepout(f, at))
		return false;
	const float out = static_cast<float>(-f.hand) * f.s;
	const Vector2D elbow = at + Vector2D(out * 20.0f, 0.0f), end = elbow + Vector2D(out * 8.0f, 10.0f * f.s);
	const float b = 4.0f * f.s;
	Line(f, at + Vector2D(-b, -b), at + Vector2D(-b, b), NEO_GHOST_LIGHT, c, 0.7f * a);
	Line(f, at + Vector2D(b, -b), at + Vector2D(b, b), NEO_GHOST_LIGHT, c, 0.7f * a);
	Line(f, at, elbow, NEO_GHOST_LIGHT, c, 0.6f * a);
	Line(f, elbow, end, NEO_GHOST_LIGHT, c, 0.6f * a);
	Text(f, pCount, end.x + out * 4.0f, end.y + 8.0f * f.s, f.hand > 0 ? -1 : 1, FONT_VALUE_LARGE, c, a);
	below.Init(end.x + out * 4.0f, end.y + 30.0f * f.s);
	align = f.hand > 0 ? -1 : 1;
	return true;
}

// The ammo calls (Kyle's pick): NT's plates hung under the count, saying what to do while the state holds: RELOAD (the
// magazine's empty, spares left), LOW (the last magazine in, no spares), OUT in red (nothing left). Kyle's words. Each
// blinks off once as it starts (motion only for the event).
enum AmmoCall { CALL_NONE, CALL_RELOAD, CALL_LAST, CALL_NONE_LEFT };

static void PaintAmmoCall(const Frame &f, const Senses &s, const NeoQuickInfo::Ammo &ammo, const Vector2D &at, int align)
{
	static AmmoCall s_call = CALL_NONE;
	static float s_since = -100.0f;
	AmmoCall call = CALL_NONE;
	if (!s.bReloading && ammo.maxRounds > 0 && !ammo.bHeat && !(ammo.pMode && !V_wcscmp(ammo.pMode, L"THROW")))
	{
		call = ammo.rounds == 0 ? (ammo.bMagsOut ? CALL_NONE_LEFT : CALL_RELOAD) : ammo.bMagsOut ? CALL_LAST : CALL_NONE;
	}
	if (call != s_call)
	{
		s_call = call;
		s_since = f.now;
	}
	if (call == CALL_NONE)
		return;
	const float age = f.now - s_since;
	if (age > 0.08f && age < 0.16f)
		return;	// the one blink
	const wchar_t *pText = call == CALL_RELOAD ? L"RELOAD" : call == CALL_LAST ? L"LOW" : L"OUT";
	if (call == CALL_NONE_LEFT)
		PlateIn(f, pText, at.x, at.y, align, 1.0f, CRIT, Color(252, 235, 235, 255));
	else
		Plate(f, pText, at.x, at.y, align, 0.95f);
}

// The rounds as ticks (weapons without a bullet glyph: the detpack): the magazine a tick each (or grouped past 30).
// A reload fills them back in on the weapon's own clock (a magazine over its whole reload; shells as the real count,
// the next one filling as it goes in); a shot flicks the tick it spent.
static void Ticks(const Frame &f, const Local &L, const Senses &s, const NeoQuickInfo::Ammo &ammo, const Color &col, float flick, float a)
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
static void Bullets(const Frame &f, const Local &L, const Senses &s, const NeoQuickInfo::Ammo &ammo, const Color &col, float flick,
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

void PaintWeapon(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const NeoQuickInfo::Ammo &ammo = s.ammo;
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
	if (ammo.bHeat)
	{
		const Color hc = s.heatLevel == 2 ? CRIT : s.heatLevel == 1 ? WARN : f.color;
		const float w = 150.0f, hw = w * ammo.heat;
		Rect(f, L.At(-w * 0.5f, 6.0f), L.At(w * 0.5f, 15.0f), f.color, 0.12f * a);
		Rect(f, L.At(m > 0.0f ? -w * 0.5f : w * 0.5f - hw, 6.0f), L.At(m > 0.0f ? -w * 0.5f + hw : w * 0.5f, 15.0f), hc,
			(ammo.bOverheated ? 0.6f + 0.35f * sinf(f.now * 12.0f) : 0.85f) * a);
		// The stock panel's word, while it can't fire.
		if (ammo.bOverheated)
		{
			const Vector2D oa = L.At(0.0f, -8.0f);
			Plate(f, L"OVERHEAT", oa.x, oa.y, 0, 0.95f);
		}
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
		Vector2D callAt;
		int callAlign;
		if (!RoundsOnGun(f, count, col, countAlpha, callAt, callAlign))
		{
			const Vector2D ca = L.At(m * 84.0f, 12.0f);
			Text(f, count, ca.x, ca.y, side, FONT_VALUE_LARGE, col, countAlpha);
			callAt = L.At(0.0f, 62.0f);
			callAlign = 0;
		}
		PaintAmmoCall(f, s, ammo, callAt, callAlign);
		ModeGlyph(f, L, m > 0.0f ? 84.0f : -96.0f, 34.0f, ammo.pMode, 0.7f * a);
		// Magazines as pips over the count (the Supa 7: shells, then slugs as taller pips).
		int mags = 0, slugs = 0;
		if (ammo.mags[0])
		{
			swscanf(ammo.mags, L"%d+%d", &mags, &slugs);
		}
		// More than the pips hold (a Supa 7's reserve of shells): the stock panel's count instead, never fewer shown
		// than you carry.
		if (mags > MAX_MAG_PIPS || slugs > MAX_SLUG_PIPS)
		{
			const Vector2D ma = L.At(m * 84.0f, -10.0f);
			Text(f, ammo.mags, ma.x, ma.y, side, FONT_VALUE, f.color, 0.8f * a);
		}
		else
		{
			for (int i = 0; i < mags + slugs; ++i)
			{
				const bool bSlug = i >= mags;
				const float x = m * (84.0f + i * 6.0f) - (m < 0.0f ? 4.0f : 0.0f);
				Rect(f, L.At(x, bSlug ? -16.0f : -12.0f), L.At(x + 4.0f, -4.0f), ammo.bMagsOut ? CRIT : f.color, 0.75f * a);
			}
		}
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
	// The range while aiming (the rangefinder's), under the settle: the ring only says where.
	if (s.bRange)
	{
		wchar_t range[24];
		if (s.rangeMetres < 0.0f)
			V_wcsncpy(range, L"RNG ---", sizeof(range));
		else
			V_snwprintf(range, ARRAYSIZE(range), L"RNG %.0f M", s.rangeMetres);
		const Vector2D rp = L.At(0.0f, 46.0f);
		Text(f, range, rp.x, rp.y, 0, FONT_VALUE, f.color, Max(0.7f, look.numbers));
	}
	if (look.labels > 0.02f)
	{
		const Vector2D na = L.At(0.0f, -22.0f);
		Text(f, ammo.name, na.x, na.y, 0, FONT_LABEL, f.color, look.labels * a);
		const Vector2D pa = L.At(m * -84.0f, -22.0f);
		Plate(f, L"WPN", pa.x, pa.y, -side, look.labels, L"\u6b8b\u5f3e");
	}
}

// A line in dashes: dash and gap in pixels at 1080p (a gap of 0 draws it solid).
static void DashedLine(const Frame &f, const Local &L, const Vector2D &from, const Vector2D &to, float dash, float gap, const Color &c,
	float a)
{
	const Vector2D d = to - from;
	const float len = d.Length();
	if (len < 0.5f)
		return;
	if (gap <= 0.0f)
	{
		Line(f, L.At(from.x, from.y), L.At(to.x, to.y), NEO_GHOST_LIGHT, c, a);
		return;
	}
	const Vector2D u = d / len;
	for (float t = 0.0f; t < len; t += dash + gap)
	{
		const Vector2D p0 = from + u * t, p1 = from + u * Min(t + dash, len);
		Line(f, L.At(p0.x, p0.y), L.At(p1.x, p1.y), NEO_GHOST_LIGHT, c, a);
	}
}

// The link group as a node graph (Kyle's pick): you as a node linked to a node per teammate, fanned above you in a
// fixed arc (never where they are). The links' line is your ping: solid while it's good, dashed as it strains, sparse
// dots when it's bad, always the HUD's colour. A dead teammate is a hollow red node, its link broken off short. Your
// own node is a chamfered cell filling with your neural load (a cell a system drawing on you, of six).
void PaintLink(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const Look look = LookOf(f, GROUP_LINK);
	const Local L = { f.pPlaces[GROUP_LINK].pos, f.s * look.scale, f.hand };
	const float a = look.alpha;
	const int side = L.m > 0 ? 1 : -1;
	const Vector2D you(0.0f, LINK_YOU_Y);
	const float dash = 3.0f, gap = s.ping < 90 ? 0.0f : s.ping < 150 ? 3.0f : 7.0f;
	const int mates = Min(s.squadTotal, LINK_MAX_MATES);
	for (int i = 0; i < mates; ++i)
	{
		const float t = mates > 1 ? static_cast<float>(i) / (mates - 1) : 0.5f;
		const float q = DEG2RAD(-LINK_FAN + 2.0f * LINK_FAN * t);
		const Vector2D node = you + Vector2D(sinf(q) * LINK_RADIUS, -cosf(q) * LINK_RADIUS);
		const bool bAlive = i < s.squadAlive;
		const Vector2D dir = (node - you) / LINK_RADIUS;
		const Vector2D from = you + dir * 9.0f, to = node - dir * 5.0f;
		if (bAlive)
		{
			DashedLine(f, L, from, to, dash, gap, f.color, 0.75f * a);
			Rect(f, L.At(node.x - 3.5f, node.y - 3.5f), L.At(node.x + 3.5f, node.y + 3.5f), f.color, 0.9f * a);
		}
		else
		{
			DashedLine(f, L, from, from + (to - from) * 0.35f, dash, 3.0f, CRIT, 0.5f * a);
			RectOutline(f, L.At(node.x - 3.5f, node.y - 3.5f), L.At(node.x + 3.5f, node.y + 3.5f), NEO_GHOST_LIGHT, CRIT, 0.85f * a);
		}
	}
	// You: a cell filling with your neural load.
	CellStyle style;
	style.chamfer = side;
	style.fill = f.color;
	Cells(f, L.At(-7.0f, you.y - 7.0f), L.At(7.0f, you.y + 7.0f), clamp(s.load / 6.0f, 0.0f, 1.0f), style, a);
	if (look.labels > 0.02f)
	{
		wchar_t ping[16];
		V_snwprintf(ping, ARRAYSIZE(ping), L"%d MS", s.ping);
		const Vector2D pa = L.At(side * 16.0f, you.y);
		Text(f, ping, pa.x, pa.y, side, FONT_VALUE, f.color, look.labels * a);
		const Vector2D ta = L.At(0.0f, you.y + 22.0f);
		Plate(f, L"LINK", ta.x, ta.y, 0, look.labels, L"\u901a\u4fe1");
	}
}
} // namespace NeoCyberbrain
