#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Keeping groups apart (from neo_cyberbrain_attention.cpp, which places them): the focus zone round the crosshair and
// the way each critical comes into it, groups in focus keeping off each other, and every pulled-in group stepping off
// its quieter neighbours (R5).

namespace NeoCyberbrain
{
// The focus zone: a group's whole extent kept outside this ellipse round the crosshair (pixels at 1080p, about the
// spread at the hip).
constexpr float FOCUS_X = 110.0f, FOCUS_Y = 80.0f;
constexpr float FOCUS_GAP = 6.0f;		// pixels at 1080p groups keep between their extents (in focus, and stepping off)
constexpr float ORBIT_STEP = 10.0f, ORBIT_MOST = 90.0f;	// degrees a group in focus swings its approach, a step and at most

// Whether a group's box (centre, half) keeps clear of the focus bound round the crosshair.
bool ClearOfFocus(const Frame &f, const Vector2D &centre, const Vector2D &half)
{
	const float nx = clamp(f.centre.x, centre.x - half.x, centre.x + half.x) - f.centre.x;
	const float ny = clamp(f.centre.y, centre.y - half.y, centre.y + half.y) - f.centre.y;
	return Square(nx / (FOCUS_X * f.s)) + Square(ny / (FOCUS_Y * f.s)) >= 1.0f;
}

// How far from the crosshair along `dir` a group's box, offset from its point by `offset`, first keeps clear of the
// focus bound (searched up to `most`).
float FocusReach(const Frame &f, const Vector2D &dir, const Vector2D &offset, const Vector2D &half, float most)
{
	float lo = 0.0f, hi = most;
	for (int i = 0; i < 12; ++i)
	{
		const float mid = 0.5f * (lo + hi);
		if (ClearOfFocus(f, f.centre + dir * mid + offset, half))
			hi = mid;
		else
			lo = mid;
	}
	return hi;
}

// Whether two boxes (centre, half) come within `gap` of each other.
static bool Meet(const Vector2D &ca, const Vector2D &ha, const Vector2D &cb, const Vector2D &hb, float gap)
{
	return fabsf(ca.x - cb.x) < ha.x + hb.x + gap && fabsf(ca.y - cb.y) < ha.y + hb.y + gap;
}

// The way each group in focus comes in (from the crosshair, a unit vector). Several criticals at once share the zone:
// the most attended comes straight in from its near home's side; each after it swings its approach round the
// crosshair, a step either way at a time (up to a quarter turn), until its extent, at the bound, keeps clear of those
// already in and inside the screen.
void FocusApproaches(const Frame &f, const Vector2D nearer[GROUP__COUNT], const float focus[GROUP__COUNT],
	const Place places[GROUP__COUNT], Vector2D dirs[GROUP__COUNT])
{
	int order[GROUP__COUNT], count = 0;
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		dirs[g] = nearer[g] - f.centre;
		dirs[g].NormalizeInPlace();
		if (focus[g] > 0.0f)
			order[count++] = g;
	}
	for (int i = 1; i < count; ++i)	// most attended first
	{
		for (int j = i; j > 0 && places[order[j]].att > places[order[j - 1]].att; --j)
			V_swap(order[j], order[j - 1]);
	}
	const float gap = FOCUS_GAP * f.s;
	Vector2D takenCentre[GROUP__COUNT], takenHalf[GROUP__COUNT];
	for (int n = 0; n < count; ++n)
	{
		const int g = order[n];
		Vector2D centre, half;
		GroupExtent(f, g, centre, half);
		const Vector2D offset = centre - places[g].pos;
		const float most = (nearer[g] - f.centre).Length(), base = atan2f(dirs[g].y, dirs[g].x);
		Vector2D chosen = dirs[g], chosenCentre = f.centre + dirs[g] * FocusReach(f, dirs[g], offset, half, most) + offset;
		bool bFound = false;
		for (int step = 0; step * ORBIT_STEP <= ORBIT_MOST && !bFound; ++step)
		{
			for (int side = 0; side < (step == 0 ? 1 : 2) && !bFound; ++side)
			{
				const float angle = base + DEG2RAD((side == 0 ? 1.0f : -1.0f) * step * ORBIT_STEP);
				const Vector2D dir(cosf(angle), sinf(angle));
				const Vector2D c = f.centre + dir * FocusReach(f, dir, offset, half, most) + offset;
				bool bClear = Inside(f, c, half).IsZero(0.5f);
				for (int m = 0; m < n && bClear; ++m)
					bClear = !Meet(c, half, takenCentre[m], takenHalf[m], gap);
				if (bClear)
				{
					chosen = dir;
					chosenCentre = c;
					bFound = true;
				}
			}
		}
		dirs[g] = chosen;
		takenCentre[n] = chosenCentre;
		takenHalf[n] = half;
	}
}

// Groups in focus keep off each other (two criticals at once both come in by the crosshair): where two boxes overlap
// and either is in focus, the one less in focus steps out along the shallower way, its speed that way dropped, and
// stays out of the focus bound and inside the screen.
void SeparateFocused(const Frame &f, Place places[GROUP__COUNT])
{
	const float gap = FOCUS_GAP * f.s;
	for (int pass = 0; pass < 2; ++pass)
	{
		for (int a = 0; a < GROUP__COUNT; ++a)
		{
			for (int b = a + 1; b < GROUP__COUNT; ++b)
			{
				if (places[a].focus <= 0.0f && places[b].focus <= 0.0f)
					continue;
				Vector2D ca, ha, cb, hb;
				GroupExtent(f, a, ca, ha);
				GroupExtent(f, b, cb, hb);
				const float ox = ha.x + hb.x + gap - fabsf(ca.x - cb.x), oy = ha.y + hb.y + gap - fabsf(ca.y - cb.y);
				if (ox <= 0.0f || oy <= 0.0f)
					continue;
				const bool bMoveA = places[a].focus != places[b].focus ? places[a].focus < places[b].focus : places[a].att < places[b].att;
				const int mover = bMoveA ? a : b;
				Place &m = places[mover];
				const Vector2D away = bMoveA ? ca - cb : cb - ca;
				if (ox < oy)
				{
					m.pos.x += away.x < 0.0f ? -ox : ox;
					m.vel.x = 0.0f;
				}
				else
				{
					m.pos.y += away.y < 0.0f ? -oy : oy;
					m.vel.y = 0.0f;
				}
				Vector2D centre, half;
				GroupExtent(f, mover, centre, half);
				Vector2D dir = m.pos - f.centre;
				const float at = dir.NormalizeInPlace();
				if (at > 0.001f && !ClearOfFocus(f, centre, half))
				{
					m.pos = f.centre + dir * FocusReach(f, dir, centre - m.pos, half, at + 400.0f * f.s);
					GroupExtent(f, mover, centre, half);
				}
				m.pos += Inside(f, centre, half);
			}
		}
	}
}

// R5: every group steps off, not only those in focus. Where a pulled-in group's box, at its target, comes within the
// gap of a less attended neighbour's, the neighbour's target steps out the shallower way, so it eases off on the
// placement spring rather than jumping. Most attended first, so a group pushed while pulled in passes it on. Groups at
// rest never push (their homes are clear at near size), groups in focus keep their own pass (SeparateFocused), and only
// what's been measured counts (the link draws nothing). Each box is the group's own: the body's without the ring's
// disc, which doesn't move.
void StepOff(const Frame &f, const Place places[GROUP__COUNT], Vector2D targets[GROUP__COUNT])
{
	const float gap = FOCUS_GAP * f.s;
	const auto box = [&](int g, Vector2D &centre, Vector2D &half)
	{
		centre = targets[g] + places[g].drawnCentre;
		half = places[g].drawnHalf + Vector2D(DRAWN_PAD, DRAWN_PAD) * f.s;
	};
	int order[GROUP__COUNT], count = 0;
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		if (places[g].bDrawn && places[g].focus <= 0.0f)
			order[count++] = g;
	}
	for (int i = 1; i < count; ++i)
	{
		for (int j = i; j > 0 && places[order[j]].att > places[order[j - 1]].att; --j)
			V_swap(order[j], order[j - 1]);
	}
	for (int i = 0; i < count; ++i)
	{
		const int a = order[i];
		if (places[a].layer == LAYER_AMBIENT)
			continue;
		for (int j = i + 1; j < count; ++j)
		{
			const int b = order[j];
			Vector2D ca, ha, cb, hb;
			box(a, ca, ha);
			box(b, cb, hb);
			const float ox = ha.x + hb.x + gap - fabsf(ca.x - cb.x), oy = ha.y + hb.y + gap - fabsf(ca.y - cb.y);
			if (ox <= 0.0f || oy <= 0.0f)
				continue;
			if (ox < oy)
				targets[b].x += cb.x < ca.x ? -ox : ox;
			else
				targets[b].y += cb.y < ca.y ? -oy : oy;
		}
	}
}
} // namespace NeoCyberbrain
