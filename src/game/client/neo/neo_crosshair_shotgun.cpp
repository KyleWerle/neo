#include "cbase.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"
#include "weapon_supa7.h"
#include "neo_spread_pivot.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The shotgun family: a ring of dots at the pellet cone. Each shot bursts the ring outward; as the next shell
// chambers (the gun cycling to its next shot) the dots come home from the sides toward the top and bottom, and
// the ring is whole again just before it can fire. The burst is thrown toward where the pellets went (the shot's pattern,
// from its seed): each dot out as far as the pellets went its way. A slug in the Supa 7 draws the ring in to a tight diamond.

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
static constexpr float BURST_FLOOR = 0.3f;		// the burst of a dot no pellet went toward, of the most
static constexpr float PATTERN_AGE = 0.2f;		// a pattern this old isn't this shot's
static constexpr float FILL_LEAD = 0.08f;		// the ring is whole this long before the gun is ready
static constexpr float FILL_LEAD_MAX = 0.15f;	// but never earlier than this share of its cycle

static struct
{
	float burstTime = -100.0f;
	float morph = 0.0f;	// 0 the ring, 1 the slug's diamond
	float burstOf[DOTS];	// each dot's share of the burst, from the last shot's pattern
} s_shotgun;

// Each dot's share of the burst: how far the pellets went its way, the most 1. No pattern (a watched player's
// shot): all of it.
static void TakePattern()
{
	const NeoSpreadPattern &pattern = NeoSpreadPivotLastPattern();
	const bool bFresh = pattern.count > 0 && gpGlobals->realtime - pattern.time < PATTERN_AGE;
	float most = 0.0f;
	for (int i = 0; i < DOTS; ++i)
	{
		const float angle = 2.0f * M_PI_F * i / DOTS - 0.5f * M_PI_F;
		const Vector2D direction(cosf(angle), sinf(angle));
		float toward = 0.0f;
		for (int p = 0; bFresh && p < pattern.count; ++p)
		{
			// The cone is up-positive, the screen down-positive.
			toward += Max(0.0f, pattern.cone[p].x * direction.x - pattern.cone[p].y * direction.y);
		}
		s_shotgun.burstOf[i] = toward;
		most = Max(most, toward);
	}
	for (int i = 0; i < DOTS; ++i)
	{
		s_shotgun.burstOf[i] = (most > 0.0f) ? Lerp(s_shotgun.burstOf[i] / most, BURST_FLOOR, 1.0f) : 1.0f;
	}
}

// How far home dot i is, 0 waiting to 1 home. The ring fills from the sides: the left and right dots first, then
// pairs moving up and down both sides at once, meeting at the top and bottom last, each group gliding in over its
// quarter of the fill. The fill finishes a moment before the gun is ready (FILL_LEAD), so it visibly settles; the
// bridges locking is the exact moment it can fire.
static float DotHome(const NeoCrosshairFrame &frame, int i)
{
	const float lead = Min(FILL_LEAD / Max(frame.cycle, 0.001f), FILL_LEAD_MAX);
	const float fill = clamp(frame.ready / (1.0f - lead), 0.0f, 1.0f);
	// The dot's height off the horizontal middle, in steps of a twelfth of the ring: 0 (the sides) to 3 (top, bottom).
	const int group = abs(i % (DOTS / 2) - DOTS / 4);
	const int groups = DOTS / 4 + 1;
	return NeoSmoothStep(clamp(fill * groups - group, 0.0f, 1.0f));
}

// Where the ring's dot i is out to, before the slug's morph: waiting or home, and the shot's burst on top (every
// dot is waiting just after a shot, so a burst only on the home ones never showed).
static float DotRadius(const NeoCrosshairFrame &frame, int i, float radius, float burst)
{
	return radius * (Lerp(DotHome(frame, i), AWAY, 1.0f) + BURST_OUT * burst * s_shotgun.burstOf[i]);
}

// The ring's centre: thrown with the gun by the shot, then drawn back to the aim as it refills. A shotgun's turn
// toward its pellets holds for most of its cycle; riding it all the way, the ring refilled off-centre, lopsided.
static Vector2D RingCentre(const NeoCrosshairFrame &frame)
{
	return frame.centre + frame.jitter + frame.deviation * (1.0f - NeoSmoothStep(frame.ready));
}

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
		for (float &share : s_shotgun.burstOf)
		{
			share = 1.0f;
		}
	}
	if (frame.bShot)
	{
		s_shotgun.burstTime = now;
		TakePattern();
	}
	s_shotgun.morph = Approach(bSlug ? 1.0f : 0.0f, s_shotgun.morph, frame.dt / MORPH_TIME);
	const float morph = NeoSmoothStep(s_shotgun.morph);
	const float burst = 1.0f - NeoSmoothStep((now - s_shotgun.burstTime) / BURST_TIME);

	const Vector2D near = RingCentre(frame);
	const float radius = frame.spread + GAP * s;
	const float diamond = Max(frame.spread + GAP * s, SLUG_MIN * s);
	const NeoGhostWeight weight = (frame.aim >= 0.5f) ? NEO_GHOST_HEAVY : NEO_GHOST_MEDIUM;
	for (int i = 0; i < DOTS; ++i)
	{
		// Placed clockwise from the top.
		const float angle = 2.0f * M_PI_F * i / DOTS - 0.5f * M_PI_F;
		const Vector2D direction(cosf(angle), sinf(angle));
		// Home once the gun has cycled past this dot's share of it.
		const float home = DotHome(frame, i);
		const float ringRadius = DotRadius(frame, i, radius, burst);
		// The same direction's point on the slug's diamond (|x| + |y| = its radius).
		const float onDiamond = diamond / Max(fabsf(direction.x) + fabsf(direction.y), 0.001f);
		const Vector2D at = near + direction * Lerp(morph, ringRadius, onDiamond);
		NeoGhostBegin(frame.color, frame.Alpha(Lerp(home, 0.6f, 1.0f) * Lerp(frame.aim, HIP_SIZE, 1.0f)));
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

// The aim crosshair: a small ring broken where four spokes leave it. Settled, the spokes run out to the ring's
// dots at the top, right, bottom and left (the slug's diamond's corners): a wheel.
void NeoCrosshairAimShotgun(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph)
{
	using namespace NeoCrosshairShotgun;
	const float s = frame.s;
	const float hub = (bGlyph ? 3.0f : 1.0f) * s;
	const float now = gpGlobals->realtime;
	const float morph = NeoSmoothStep(s_shotgun.morph);
	const float burst = 1.0f - NeoSmoothStep((now - s_shotgun.burstTime) / BURST_TIME);
	const float radius = frame.spread + GAP * s;
	const float diamond = Max(frame.spread + GAP * s, SLUG_MIN * s);
	// Straight out from the aim crosshair, to where the ring's dots sit once the layers have come together.
	for (int spoke = 0; spoke < 4; ++spoke)
	{
		// The same place as the ring's dot there (the one a quarter of the way round per spoke).
		const int dot = spoke * DOTS / 4;
		const float angle = 2.0f * M_PI_F * dot / DOTS - 0.5f * M_PI_F;
		const Vector2D direction(cosf(angle), sinf(angle));
		const float ringRadius = DotRadius(frame, dot, radius, burst);
		NeoCrosshairBridge(frame, at + direction * hub, at + direction * Lerp(morph, ringRadius, diamond));
	}
	if (!bGlyph)
	{
		return;
	}
	// The brightest layer: full strength whatever the layer opacity (that one is the spread view's). An arc between
	// each two spokes, clear of them.
	NeoGhostBegin(frame.color, frame.color.a());
	static constexpr float CLEAR = 0.35f;	// radians either side of a spoke
	for (int arc = 0; arc < 4; ++arc)
	{
		const float from = arc * 0.5f * M_PI_F - 0.5f * M_PI_F + CLEAR, to = from + 0.5f * M_PI_F - 2.0f * CLEAR;
		const float middle = (from + to) * 0.5f;
		const Vector2D a = at + Vector2D(cosf(from), sinf(from)) * hub, b = at + Vector2D(cosf(middle), sinf(middle)) * hub;
		const Vector2D c = at + Vector2D(cosf(to), sinf(to)) * hub;
		NeoGhostStroke(frame.pen, a, b, NEO_GHOST_MEDIUM);
		NeoGhostStroke(frame.pen, b, c, NEO_GHOST_MEDIUM);
	}
}

float NeoCrosshairReachShotgun(const NeoCrosshairFrame &frame, float spread)
{
	using namespace NeoCrosshairShotgun;
	// The ring thrown out by a shot's burst (or waiting out at AWAY), or the slug's diamond at its smallest.
	return Max((spread + GAP * frame.s) * Max(1.0f + BURST_OUT, AWAY), SLUG_MIN * frame.s);
}
