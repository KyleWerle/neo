#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_cyberbrain_gun.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The optics group, in shapes rather than numbers: how visible you are (a halftone patch that fills with the light
// you stand in, hollow and displaced while cloaked, vision mode lighting its brackets; the therm-optic tank under it).
// Nothing loops: motion is for events. The weapon group is neo_cyberbrain_weapon.cpp; the link is the squad list's
// party view (neo_cyberbrain_squad.cpp). Words and plates only come in with attention.

namespace NeoCyberbrain
{
constexpr int HALFTONE_CELLS = 5;
constexpr float CLOAK_BAR = 5.0f;		// the therm-optic frame's bars, pixels at 1080p
constexpr float CLOAK_LOW_SECONDS = 3.0f;	// the frame and its number go amber under this many seconds of cloak
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
		// Seconds, not percent (Kyle, 2026-10-01: the frame must show the seconds of cloak you have): eight bars for the
		// class's whole cloak (13 s recon, 8 s assault), filled by the unrounded seconds left, a tick on the track at
		// every whole second, and the number beside the patch. Amber under three seconds, for both classes.
		const float cap = Max(s.cloakCap, 1.0f);
		const bool bLow = s.cloakSeconds < CLOAK_LOW_SECONDS;
		const Color frame = bLow ? WARN : f.color;
		const float lit = s.cloakSeconds / cap * 8.0f;
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
			// The seconds on the track: a tick at each whole second falling inside this segment.
			for (int sec = 1; sec < static_cast<int>(cap); ++sec)
			{
				const float u = sec / cap * 8.0f - i;
				if (u < 0.0f || u >= 1.0f)
					continue;
				const Vector2D p = c0 + dir * (seg * ((i % 2) + u));
				Line(f, at(p + normal * thick), at(p + normal * (thick + 3.0f)), NEO_GHOST_LIGHT, f.color, 0.5f * a);
			}
			if (t > 0.0f)
				bar(from, from + (to - from) * t, !s.bCloaked, frame, (t < 1.0f ? 0.75f : 0.9f) * a);
		}
	}
	// The seconds as a number on the outer side of the patch (a real number, so it holds principle 3): whole seconds
	// drain one a second, the recharge goes in tenths. Always at least faint; full while cloaked or under three seconds.
	if (s.bHasCloak)
	{
		wchar_t secs[16];
		V_snwprintf(secs, ARRAYSIZE(secs), L"%.1f S", s.cloakSeconds);
		const bool bLow = s.cloakSeconds < CLOAK_LOW_SECONDS;
		const float strength = Max(Max(look.numbers, 0.45f), (s.bCloaked || bLow) ? 1.0f : 0.0f);
		Text(f, secs, L.At(-m * (half + 6.0f + CLOAK_BAR + 6.0f), 0.0f).x, L.At(0.0f, 0.0f).y, -side, FONT_VALUE,
			bLow ? WARN : f.color, strength * a);
	}
	// R4's slots, a gap off the brackets (the group's outermost marks): the plate on the top rail, and at the foot what
	// the machine says (the vision mode, the JGR56), or when it says nothing the state word, only when pulled in. The
	// foot's row is as tall as either, so neither moves when the other takes it (as gate 3's rail: the call takes it,
	// the word yields).
	const float gap = ROW_GAP * L.k;
	const bool bLabels = look.labels > 0.02f;
	if (bLabels)
	{
		static const wchar_t *const s_kanji = L"\u5149\u5b66";
		Stack rail = { L.At(0.0f, -edge).y - gap, gap, -1 };
		const float y = rail.Row(PlateTall(f, L"OPTICS", s_kanji));
		wchar_t word[16];
		Plate(f, Crystallise(f, GROUP_OPTICS, L"OPTICS", word, ARRAYSIZE(word)), L.At(m * -40.0f, 0.0f).x, y, -side, look.labels, s_kanji);
	}
	Stack foot = { L.At(0.0f, edge).y + gap, gap, 1 };
	const float footY = foot.Row(Max(MachinePlateTall(f), FontTall(FONT_LABEL)));
	const wchar_t *pMachine = s.bVision && s.pVision ? s.pVision : bJuggernaut ? L"JGR56 ACTIVE" : nullptr;
	if (pMachine)
	{
		MachinePlate(f, pMachine, L.At(0.0f, 0.0f).x, footY, 0, 0.9f);
	}
	else if (bLabels)
	{
		const wchar_t *pWord = s.bCloaked ? Word("neo_hud_cb_cloaked", L"CLOAKED") : bExposed ? Word("neo_hud_cb_exposed", L"EXPOSED")
			: s.light > 0.3f ? Word("neo_hud_cb_lit", L"LIT") : s.light > 0.15f ? Word("neo_hud_cb_dim", L"DIM") : Word("neo_hud_cb_dark", L"DARK");
		Text(f, pWord, L.At(0.0f, 0.0f).x, footY, 0, FONT_LABEL, bExposed ? WARN : f.color, look.labels * a);
	}
}

// The fire mode as a glyph: AUTO three rounds, SEMI one, BUCK a spread, SLUG one heavy, THROW an arc.
} // namespace NeoCyberbrain
