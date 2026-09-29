#include "cbase.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"
#include "weapon_supa7.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The shotgun family: a ring of dots at the pellet cone. Each shot bursts the ring outward; as the next shell
// chambers (the gun cycling to its next shot) the dots come home one by one, clockwise from the top, and the
// ring is whole again when it can fire. A slug in the Supa 7 draws the ring in to a tight diamond.

namespace NeoCrosshairShotgun
{
// Sizes in pixels at 1080p.
static constexpr int DOTS = 12;
static constexpr float GAP = 3.0f;				// beyond the spread's edge
static constexpr float BURST_OUT = 0.6f;			// how far out a shot throws the ring, of its radius
static constexpr float BURST_TIME = 0.14f;		// seconds the burst takes to fall back
static constexpr float AWAY = 1.3f;			// where the dots not yet home wait, of the ring's radius
static constexpr float SLUG_MIN = 5.0f;		// the slug's diamond's smallest radius
static constexpr float MORPH_TIME = 0.15f;		// ring to diamond and back
static constexpr float HIP_SIZE = 0.9f;		// the ring's dots are lighter at the hip

static struct
{
	float burstTime = -100.0f;
	float morph = 0.0f;	// 0 the ring, 1 the slug's diamond
} s_shotgun;

} // namespace NeoCrosshairShotgun

void NeoCrosshairPaintShotgun(const NeoCrosshairFrame &frame)
{
	using namespace NeoCrosshairShotgun;
	const float s = frame.s;
	const float now = gpGlobals->realtime;
	auto *pSupa = dynamic_cast<CWeaponSupa7 *>(frame.pWeapon);
	const bool bSlug = pSupa && pSupa->SlugLoaded();
	if (frame.bBoot)
	{
		s_shotgun.burstTime = -100.0f;
		s_shotgun.morph = bSlug ? 1.0f : 0.0f;
	}
	if (frame.bShot)
	{
		s_shotgun.burstTime = now;
	}
	s_shotgun.morph = Approach(bSlug ? 1.0f : 0.0f, s_shotgun.morph, frame.dt / MORPH_TIME);
	const float morph = NeoSmoothStep(s_shotgun.morph);
	const float burst = 1.0f - NeoSmoothStep((now - s_shotgun.burstTime) / BURST_TIME);

	const Vector2D near = frame.centre + frame.deviation + frame.jitter;
	const float radius = frame.spread + GAP * s;
	const float diamond = Max(frame.spread + GAP * s, SLUG_MIN * s);
	const NeoGhostWeight weight = (frame.aim >= 0.5f) ? NEO_GHOST_HEAVY : NEO_GHOST_MEDIUM;
	for (int i = 0; i < DOTS; ++i)
	{
		// Clockwise from the top.
		const float angle = 2.0f * M_PI_F * i / DOTS - 0.5f * M_PI_F;
		const Vector2D direction(cosf(angle), sinf(angle));
		// Home once the gun has cycled past this dot's share of it.
		const bool bHome = frame.ready >= static_cast<float>(i + 1) / DOTS;
		const float ringRadius = radius * (bHome ? 1.0f + BURST_OUT * burst : AWAY);
		// The same direction's point on the slug's diamond (|x| + |y| = its radius).
		const float onDiamond = diamond / Max(fabsf(direction.x) + fabsf(direction.y), 0.001f);
		const Vector2D at = near + direction * Lerp(morph, ringRadius, onDiamond);
		NeoGhostBegin(frame.color, frame.Alpha((bHome ? 1.0f : 0.6f) * Lerp(frame.aim, HIP_SIZE, 1.0f)));
		NeoCrosshairDot(frame, at, weight);
	}
	// The diamond's edges as the slug comes in.
	if (morph > 0.0f)
	{
		NeoGhostBegin(frame.color, frame.Alpha(morph * 0.9f));
		const Vector2D corners[4] = { near + Vector2D(0.0f, -diamond), near + Vector2D(diamond, 0.0f),
			near + Vector2D(0.0f, diamond), near + Vector2D(-diamond, 0.0f) };
		for (int c = 0; c < 4; ++c)
		{
			NeoGhostStroke(frame.pen, corners[c], corners[(c + 1) % 4], NEO_GHOST_LIGHT);
		}
	}
}
