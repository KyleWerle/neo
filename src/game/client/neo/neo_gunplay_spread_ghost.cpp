#include "cbase.h"
#include "neo_gunplay_spread_ghost.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_crosshair_ghost("cl_neo_gunplay_crosshair_ghost", "0.35", FCVAR_ARCHIVE,
	"The spread ghost: a faint outline at exactly the spread's edge, calm while the rest is knocked about. Its"
	" opacity; 0 = none.", true, 0, true, 1);
ConVar cl_neo_gunplay_crosshair_ghost_time("cl_neo_gunplay_crosshair_ghost_time", "0.08", FCVAR_ARCHIVE,
	"The spread ghost's size easing: its spring's time constant, seconds.", true, 0.01f, true, 1);
ConVar cl_neo_gunplay_crosshair_ghost_pop("cl_neo_gunplay_crosshair_ghost_pop", "3", FCVAR_ARCHIVE,
	"How far each shot pops the spread ghost out past the spread, pixels at 1080p at its peak (the spread view's pops"
	" about 13).", true, 0, true, 20);

namespace NeoGunplaySpreadGhost
{
// Sizes in pixels at 1080p.
static constexpr float TICK = 5.0f;				// the MG: a tick out from each side (its rails)
static constexpr float ARC_SPAN = 50.0f;		// the rifle: a broken ring, an arc this many degrees on each diagonal
static constexpr int ARC_SEGMENTS = 5;
static constexpr float SERIF = 2.5f;			// and a short serif out from each arc's ends
static constexpr float HEX_ARM = 3.5f;			// the pistol: a hexagon's corners (its impact mark, the Kyla's chambers)
static constexpr float BRACKET = 4.5f;			// the SMG and scoped: corner brackets on the edge's square
static constexpr float E = 2.7182818f;
static constexpr float STRONGER = 1.6f;			// every family but the shotgun: brighter and heavier than its ring
static constexpr int RING_DASHES = 12;			// the shotgun: a dashed circle, a dash centred under each of the ring's dots
static constexpr float RING_DASH = 0.5f;		// a dash's share of its twelfth

static struct
{
	float spread = 0.0f;
	float velocity = 0.0f;
} s_ghost;
} // namespace NeoGunplaySpreadGhost

float NeoGunplaySpreadGhostEdge()
{
	return Max(0.0f, NeoGunplaySpreadGhost::s_ghost.spread);
}

void NeoGunplayPaintSpreadGhost(const NeoCrosshairFrame &frame)
{
	using namespace NeoGunplaySpreadGhost;
	const float opacity = cl_neo_gunplay_crosshair_ghost.GetFloat();
	if (frame.bBoot || !IsFinite(s_ghost.spread))
	{
		s_ghost.spread = frame.spreadExact;
		s_ghost.velocity = 0.0f;
	}
	const float omega = 1.0f / cl_neo_gunplay_crosshair_ghost_time.GetFloat();
	// A little pop a shot, for the feel: a critically damped spring kicked at v peaks at v / (omega e).
	if (frame.bShot && !frame.bBoot)
	{
		s_ghost.velocity += cl_neo_gunplay_crosshair_ghost_pop.GetFloat() * frame.s * omega * E;
	}
	NeoCrosshairSpring(s_ghost.spread, s_ghost.velocity, frame.spreadExact, omega, frame.dt);
	if (opacity <= 0.0f)
	{
		return;
	}

	const float s = frame.s;
	const float edge = Max(0.0f, s_ghost.spread);
	const Vector2D at = frame.centre + frame.aimOffset;
	// Calm: not the shot scramble's flicker. The shotgun's ring of dots carries it light; the others' ticks and
	// brackets are fewer and smaller, so theirs is stronger (Kyle: more noticeable).
	const NeoCrosshairFamily family = NeoCrosshairFamilyOf(frame.pWeapon);
	const bool bShotgun = family == NEO_CROSSHAIR_SHOTGUN;
	const NeoGhostWeight weight = bShotgun ? NEO_GHOST_LIGHT : NEO_GHOST_MEDIUM;
	NeoGhostBegin(frame.color, RoundFloatToInt(frame.color.a() * Min(1.0f, opacity * (bShotgun ? 1.0f : STRONGER))));
	switch (family)
	{
	case NEO_CROSSHAIR_SHOTGUN:
	{
		// Placed as the ring's dots are, from the top, so each dot sits over the middle of its dash: symmetric both ways.
		const float step = 2.0f * M_PI_F / RING_DASHES;
		const float half = step * RING_DASH * 0.5f;
		for (int i = 0; i < RING_DASHES; ++i)
		{
			const float middle = i * step - 0.5f * M_PI_F;
			const float a = middle - half, b = middle + half;
			NeoGhostStroke(frame.pen, at + Vector2D(cosf(a), sinf(a)) * edge, at + Vector2D(cosf(middle), sinf(middle)) * edge,
				NEO_GHOST_LIGHT);
			NeoGhostStroke(frame.pen, at + Vector2D(cosf(middle), sinf(middle)) * edge, at + Vector2D(cosf(b), sinf(b)) * edge,
				NEO_GHOST_LIGHT);
		}
		return;
	}
	case NEO_CROSSHAIR_SMG:
	case NEO_CROSSHAIR_SCOPED:
	{
		// The edge's square: its sides touch the cone.
		static const Vector2D s_corners[4] = { Vector2D(-1, -1), Vector2D(1, -1), Vector2D(1, 1), Vector2D(-1, 1) };
		const float bracket = Min(BRACKET * s, edge);
		for (const Vector2D &corner : s_corners)
		{
			const Vector2D point = at + corner * edge;
			NeoGhostStroke(frame.pen, point, point - Vector2D(corner.x * bracket, 0.0f), weight);
			NeoGhostStroke(frame.pen, point, point - Vector2D(0.0f, corner.y * bracket), weight);
		}
		return;
	}
	case NEO_CROSSHAIR_PISTOL:
	{
		// A hexagon, its corners only: a corner at each side, where the side bridges meet it, and a flat bottom whose
		// open middle the chevron's bridge runs through.
		const float arm = Min(HEX_ARM * s, edge * 0.5f);
		for (int i = 0; i < 6; ++i)
		{
			const float angle = DEG2RAD(60.0f * i);
			const Vector2D corner = at + Vector2D(cosf(angle), sinf(angle)) * edge;
			for (int side = -1; side <= 1; side += 2)
			{
				const float next = DEG2RAD(60.0f * (i + side));
				const Vector2D along = at + Vector2D(cosf(next), sinf(next)) * edge - corner;
				NeoGhostStroke(frame.pen, corner, corner + along * (arm / Max(along.Length(), 0.001f)), weight);
			}
		}
		return;
	}
	case NEO_CROSSHAIR_RIFLE:
	{
		// A broken ring, a scope's: an arc on each diagonal, open where the arms, caps and bridges run, a serif out
		// from each end.
		for (int quarter = 0; quarter < 4; ++quarter)
		{
			const float middle = DEG2RAD(45.0f + 90.0f * quarter);
			const float from = middle - DEG2RAD(ARC_SPAN * 0.5f), step = DEG2RAD(ARC_SPAN) / ARC_SEGMENTS;
			for (int i = 0; i < ARC_SEGMENTS; ++i)
			{
				const float a = from + i * step, b = a + step;
				NeoGhostStroke(frame.pen, at + Vector2D(cosf(a), sinf(a)) * edge, at + Vector2D(cosf(b), sinf(b)) * edge, weight);
			}
			for (int end = 0; end <= 1; ++end)
			{
				const float angle = from + end * DEG2RAD(ARC_SPAN);
				const Vector2D way(cosf(angle), sinf(angle));
				NeoGhostStroke(frame.pen, at + way * edge, at + way * (edge + SERIF * s), weight);
			}
		}
		return;
	}
	default:
	{
		// The MG: a tick out from each side, its rails'.
		static const Vector2D s_ways[2] = { Vector2D(-1, 0), Vector2D(1, 0) };
		for (const Vector2D &way : s_ways)
		{
			NeoGhostStroke(frame.pen, at + way * edge, at + way * (edge + TICK * s), weight);
		}
		return;
	}
	}
}
