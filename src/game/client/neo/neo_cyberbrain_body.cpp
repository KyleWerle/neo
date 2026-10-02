#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The body group: proprioception. An abstracted capsule person in your posture (shorter crouched, tilted leaning,
// lifted in the air) holds integrity as a solid fill draining from the top, a hit leaving a red afterimage of what it took,
// with integrity's number large beside it (the one number that matters most); the ground disc under it carries your
// velocity (in the On the body style the surround ring is that disc); support's armour plates flank it. Speed and
// stamina are the motion group beside it (neo_cyberbrain_motion.cpp). A hit flashes the whole outline: NT doesn't say
// which side a hit came from, so neither does this (HUD-REDESIGN.md, "Held back for fairness").

namespace NeoCyberbrain
{
constexpr float DISC_Y = 60.0f, DISC_RX = 42.0f, DISC_RY = 12.0f;
constexpr float CAPSULE_H = 128.0f, HEAD_R = 11.0f, BODY_W = 38.0f;
constexpr float SHOULDER = 10.0f;	// the body's cut top corners
constexpr float CELL_PAD = 4.0f;
constexpr float LIFT_MOST = 24.0f;	// how far the capsule lifts at the top of a jump
constexpr float TRAIL_RATE = 0.5f;	// the hit afterimage drains this much integrity a second

static struct { float trail = 1.0f, last = -100.0f; } s_body;

// The capsule's frame: rotated by the lean round its feet, lifted in the air.
struct Posture
{
	const Local *pL;
	float cosA, sinA, lift;
	Vector2D At(float x, float y) const
	{
		const float rx = x * cosA - y * sinA, ry = x * sinA + y * cosA;
		return pL->At(rx, DISC_Y - 4.0f - lift + ry);
	}
};

// A box with its corners cut (NT's rounded brackets, in straight strokes).
static void CutBox(const Frame &f, const Posture &p, float x0, float y0, float x1, float y1, float cut, const Color &c, float a,
	NeoGhostWeight weight = NEO_GHOST_MEDIUM)
{
	const Vector2D pts[8] = { p.At(x0 + cut, y0), p.At(x1 - cut, y0), p.At(x1, y0 + cut), p.At(x1, y1 - cut * 0.4f),
		p.At(x1 - cut * 0.4f, y1), p.At(x0 + cut * 0.4f, y1), p.At(x0, y1 - cut * 0.4f), p.At(x0, y0 + cut) };
	for (int i = 0; i < 8; ++i)
	{
		Line(f, pts[i], pts[(i + 1) % 8], weight, c, a);
	}
}
// A band of the body from y0 to y1, inset to follow the body's cut corners (the shoulders at the top, the smaller cut
// at the bottom), so nothing pokes out past the outline. The inset is linear between these corners only, so a band
// is filled a piece at a time.
static void Band(const Frame &f, const Posture &p, float bodyTop, float y0, float y1, const Color &c, float a)
{
	const auto inset = [&](float y)
	{
		const float fromTop = y - bodyTop, fromBottom = -y;
		return CELL_PAD + Max(0.0f, SHOULDER + 1.5f - fromTop) + Max(0.0f, SHOULDER * 0.4f + 1.5f - fromBottom);
	};
	const float half = BODY_W * 0.5f, i0 = inset(y0), i1 = inset(y1);
	if (half - i0 <= 0.5f || half - i1 <= 0.5f)
	{
		return;
	}
	NeoGhostBegin(c, Alpha(f, a));
	const Vector2D corners[4] = { p.At(-half + i0, y0), p.At(half - i0, y0), p.At(half - i1, y1), p.At(-half + i1, y1) };
	NeoGhostFill(corners);
}

// The integrity fill from y0 down to y1, split where the cut corners' insets change slope.
static void Fill(const Frame &f, const Posture &p, float bodyTop, float y0, float y1, const Color &c, float a)
{
	const float breaks[2] = { bodyTop + SHOULDER + 1.5f, -(SHOULDER * 0.4f + 1.5f) };
	float from = y0;
	for (const float at : breaks)
	{
		if (at > from && at < y1)
		{
			Band(f, p, bodyTop, from, at, c, a);
			from = at;
		}
	}
	if (y1 - from > 0.25f)
		Band(f, p, bodyTop, from, y1, c, a);
}
void PaintBody(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const Look look = LookOf(f, GROUP_BODY);
	const Local L = { f.pPlaces[GROUP_BODY].pos, f.s * look.scale, f.hand };
	// Integrity matters most: the capsule never dims far, whatever the attention.
	const float a = Max(look.alpha, 0.85f), m = static_cast<float>(L.m);
	const bool bRing = f.style == NEO_HUD_STYLE_BODY;	// the surround ring is the ground disc (neo_cyberbrain_ring.cpp)
	const Color col = s.hp <= 0.25f ? CRIT : s.hp <= 0.5f ? WARN : f.color;
	const bool bHit = f.now - s.hitTime < 0.35f;
	const float since = f.now - s_body.last;
	s_body.trail = (since < 0.0f || since > 0.5f) ? s.hp : Max(s.hp, s_body.trail - TRAIL_RATE * since);
	s_body.last = f.now;

	// The ground disc and your velocity on it.
	if (!bRing)
	{
		Arc(f, L.At(0.0f, DISC_Y), Vector2D(DISC_RX, DISC_RY) * L.k, 0.0f, 360.0f, NEO_GHOST_LIGHT, f.color, 0.35f * a);
	}
	const float v = clamp(s.speed / 7.0f, 0.0f, 1.0f), q = DEG2RAD(s.moveYaw);
	if (v > 0.05f)
	{
		Line(f, L.At(0.0f, DISC_Y), L.At(sinf(q) * DISC_RX * v * 1.6f, DISC_Y - cosf(q) * DISC_RY * v * 3.2f), NEO_GHOST_HEAVY,
			s.bSprinting ? WARN : f.color, 0.9f * a);
	}

	// The capsule, in your posture.
	const float h = CAPSULE_H * (1.0f - 0.28f * s.crouch), lift = s.air * LIFT_MOST, angle = s.lean * 0.2f;
	const Posture p = { &L, cosf(angle), sinf(angle), lift };
	const Color outline = bHit ? CRIT : col;
	const float top = -h, bodyTop = top + HEAD_R * 2.0f + 4.0f, bodyH = -bodyTop;
	CutBox(f, p, -HEAD_R, top, HEAD_R, top + HEAD_R * 2.0f, 7.0f, outline, (bHit ? 1.0f : 0.95f) * a, NEO_GHOST_HEAVY);
	CutBox(f, p, -BODY_W * 0.5f, bodyTop, BODY_W * 0.5f, 0.0f, SHOULDER, outline, (bHit ? 1.0f : 0.95f) * a, NEO_GHOST_HEAVY);
	// Integrity as one solid fill (Kyle's pick over the cells): the torso drains from the top, the last hit's loss a red
	// band above the fill that runs down to it.
	const float inTop = bodyTop + 3.0f, inBottom = -3.0f, inH = inBottom - inTop;
	const float level = inBottom - inH * clamp(s.hp, 0.0f, 1.0f), trailLevel = inBottom - inH * clamp(s_body.trail, 0.0f, 1.0f);
	if (trailLevel < level - 0.25f)
		Fill(f, p, bodyTop, trailLevel, level, CRIT, 0.7f * a);
	if (inBottom - level > 0.25f)
		Fill(f, p, bodyTop, level, inBottom, col, 0.9f * a);
	if (s.bArmour)
	{
		for (int side = -1; side <= 1; side += 2)
		{
			for (int i = 0; i < 3; ++i)
			{
				const float x0 = side * (BODY_W * 0.5f + 4.0f) - (side < 0 ? 7.0f : 0.0f), y0 = bodyTop + 8.0f + i * bodyH / 3.3f;
				const Color pc = s.hp > (i + 0.5f) / 3.2f ? f.color : Color(f.color.r(), f.color.g(), f.color.b(), 90);
				CutBox(f, p, x0, y0, x0 + 7.0f, y0 + bodyH / 3.3f - 5.0f, 1.0f, pc, (s.hp > (i + 0.5f) / 3.2f ? 0.75f : 0.2f) * a);
			}
		}
	}
	if (s.hp <= 0.25f)
	{
		for (int e = 1; e <= 2; ++e)	// the failing signal's echoes
		{
			CutBox(f, p, -BODY_W * 0.5f + e * 4.0f, bodyTop - e * 2.0f, BODY_W * 0.5f + e * 4.0f, -e * 2.0f, 7.0f, CRIT, 0.25f / e * a);
		}
	}
	if (lift > 1.0f)
	{
		Line(f, L.At(0.0f, DISC_Y - 4.0f), L.At(0.0f, DISC_Y - 2.0f - lift), NEO_GHOST_LIGHT, f.color, 0.35f * a);
	}

	// Integrity: the one big number on the outer side (R4), beside the torso away from the gun, always at full strength.
	const int side = L.m > 0 ? 1 : -1;
	wchar_t number[16];
	V_snwprintf(number, ARRAYSIZE(number), L"%d", s.hpNumber);
	// Well clear of the capsule (Kyle: it sat too close to the body).
	const Vector2D na = L.At(-m * (BODY_W * 0.5f + 22.0f), DISC_Y - 4.0f - h * 0.5f);
	Text(f, number, na.x, na.y, -side, FONT_INTEGRITY, bHit ? CRIT : col, 1.0f);
	// The top rail: the plate a gap above the highest the head goes (standing, at a jump's full lift), so it never
	// moves with the posture.
	if (look.labels > 0.02f)
	{
		static const wchar_t *const s_kanji = L"\u751f\u4f53";
		const float gap = ROW_GAP * L.k;
		Stack rail = { L.At(0.0f, DISC_Y - 4.0f - LIFT_MOST - CAPSULE_H).y - gap, gap, -1 };
		const wchar_t *pLine = PlateLine("neo_hud_cb_plate_biomech", L"BIOMECH");
		const float y = rail.Row(PlateTall(f, L"BIOMECH", s_kanji, pLine));
		wchar_t word[16], line[48];
		Plate(f, Crystallise(f, GROUP_BODY, L"BIOMECH", word, ARRAYSIZE(word)), L.At(m * -34.0f, 0.0f).x, y, -side, look.labels, s_kanji,
			Crystallise(f, GROUP_BODY, pLine, line, ARRAYSIZE(line)));
	}
}
} // namespace NeoCyberbrain
