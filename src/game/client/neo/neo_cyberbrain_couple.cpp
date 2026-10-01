#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Couplings (HUD-REWORK.md, phase 4): modules that act together register with each other. Each pair shows only in its
// real game state, as a heavy cross and a ring on the cross of each module nearest its partner (the registration), and a
// short bridge between them only when they are neighbours and a line wouldn't cross the crosshair's place.
// - Sprint: body and motion, bridged.
// - Aim: optics and weapon, registration only (a line would cross under the crosshair).
// - Cloaked while moving: optics and motion, bridged, in the warning colour.
// - Ghost carried and working: the uplink (the weapon group) and the ring.
// Comfort forms: Full snaps to registration and the bridge draws in over 0.15 s; Calm eases the registration in over
// 0.25 s and fades the bridge in whole; Still eases the registration in over 0.4 s and out over 0.6 s, no bridge.

namespace NeoCyberbrain
{
enum Pair { PAIR_SPRINT, PAIR_AIM, PAIR_CLOAK, PAIR_GHOST, PAIR__COUNT };
struct PairSpec { int a, b; bool bBridge; bool bWarn; };
static const PairSpec s_pairs[PAIR__COUNT] = {
	{ GROUP_BODY, GROUP_MOTION, true, false },
	{ GROUP_OPTICS, GROUP_WEAPON, false, false },
	{ GROUP_OPTICS, GROUP_MOTION, true, true },
	{ GROUP_WEAPON, BRIGHT_RING, false, false },
};
// On the body there is no separate ring (the body's disc is it): the ghost pair registers with the body.
constexpr float NEIGHBOURS = 280.0f;	// the most two crosses can be apart, pixels at 1080p, for a bridge

// How much each pair is wanted, 0 to 1. The cloak's is the game's own interference value (Kyle, 2026-10-01: "we can even get
// exact cloak interference value (from moving)"): none while it hides you, rising as moving makes the cloak leak, over a
// fifth of it before the coupling shows at all.
static float Wanted(const Senses &s, Pair pair)
{
	switch (pair)
	{
	case PAIR_SPRINT:	return s.bSprinting ? 1.0f : 0.0f;
	case PAIR_AIM:		return s.bInAim ? 1.0f : 0.0f;
	case PAIR_CLOAK:	return s.bCloaked ? clamp((s.cloakFactor - 0.2f) / 0.6f, 0.0f, 1.0f) : 0.0f;
	default:			return s.bGhost && s.bGhostWorking ? 1.0f : 0.0f;
	}
}

ConVar cl_neo_hud_couple_debug("cl_neo_hud_couple_debug", "0", FCVAR_NONE,
	"Cyberbrain HUD: print each coupling's weight (and the cloak factor) twice a second.", true, 0, true, 1);

void PaintCouplings(const Frame &f)
{
	static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
	static float s_weight[PAIR__COUNT];
	static int s_frame = -1;
	const int motion = cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1;
	const float inFor = motion >= 2 ? 0.15f : motion == 1 ? 0.25f : 0.4f, outFor = motion >= 2 ? 0.15f : motion == 1 ? 0.25f : 0.6f;
	const bool bAdvance = s_frame != gpGlobals->framecount;
	s_frame = gpGlobals->framecount;
	for (int k = 0; k < PAIR__COUNT; ++k)
	{
		PairSpec pair = s_pairs[k];
		if (pair.b == BRIGHT_RING && f.style == NEO_HUD_STYLE_BODY)
			pair.b = GROUP_BODY;
		const float goal = Wanted(*f.pSenses, static_cast<Pair>(k));
		if (bAdvance)
			s_weight[k] = Approach(goal, s_weight[k], gpGlobals->frametime / (goal > s_weight[k] ? inFor : outFor));
		const float w = s_weight[k];
		if (cl_neo_hud_couple_debug.GetBool() && k == 0 && bAdvance && static_cast<int>(f.now * 2.0f) != static_cast<int>((f.now - gpGlobals->frametime) * 2.0f))
			Msg("couple: sprint %.2f aim %.2f cloak %.2f ghost %.2f (cloak factor %.2f)\n", s_weight[0], s_weight[1], s_weight[2], s_weight[3], f.pSenses->cloakFactor);
		if (w <= 0.01f)
			continue;
		Vector2D ca, cb;
		const Vector2D centreA = [&] { Vector2D c, h; GroupExtent(f, pair.a, c, h); return c; }();
		const Vector2D centreB = [&] { Vector2D c, h; GroupExtent(f, pair.b, c, h); return c; }();
		if (!FrameCross(f, pair.a, centreB, ca) || !FrameCross(f, pair.b, centreA, cb))
			continue;
		const Color c = pair.bWarn ? WARN : f.color;
		const float s = f.s, ease = NeoSmoothStep(w);
		for (const Vector2D &at : { ca, cb })
		{
			// The heavy cross and its ring: the registration.
			Line(f, Vector2D(at.x - 7.0f * s, at.y), Vector2D(at.x + 7.0f * s, at.y), NEO_GHOST_HEAVY, c, 0.85f * ease);
			Line(f, Vector2D(at.x, at.y - 7.0f * s), Vector2D(at.x, at.y + 7.0f * s), NEO_GHOST_HEAVY, c, 0.85f * ease);
			Arc(f, at, Vector2D(9.0f, 9.0f) * s, 0.0f, 360.0f, NEO_GHOST_LIGHT, c, 0.6f * ease);
		}
		if (pair.bBridge && motion > 0 && (ca - cb).Length() < NEIGHBOURS * s)
		{
			// Full draws the bridge in from the first cross; Calm fades it in whole.
			const Vector2D end = motion >= 2 ? ca + (cb - ca) * w : cb;
			Line(f, ca, end, NEO_GHOST_MEDIUM, c, (motion >= 2 ? 0.6f : 0.6f * ease));
		}
	}
}
} // namespace NeoCyberbrain
