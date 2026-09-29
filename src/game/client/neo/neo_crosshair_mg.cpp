#include "cbase.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The MG family: a wide rail either side of the spread, ticked like a belt. Each shot feeds the belt a tick
// outward (eased, so sustained fire scrolls it); sustained fire builds strain, a bar along the right rail that
// grows heavier and cools off after, and the rails sink a little under it and more with the gun's knock.

namespace NeoCrosshairMg
{
// Sizes in pixels at 1080p.
static constexpr float GAP = 5.0f;				// beyond the spread's edge
static constexpr float HIP_RAIL = 12.0f;		// each rail's length, at the hip and aimed
static constexpr float AIM_RAIL = 24.0f;
static constexpr float TICK_STEP = 4.0f;		// the belt's ticks
static constexpr float TICK_HEIGHT = 2.5f;
static constexpr float FEED_TIME = 0.05f;		// a tick's feed eases over this
static constexpr float STRAIN_PER_SHOT = 0.07f;
static constexpr float STRAIN_COOL = 0.4f;		// per second
static constexpr float STRAIN_SINK = 2.5f;		// pixels the rails sink at full strain
static constexpr float RAIL_PARALLAX = 1.4f;	// the rails ride the knock more than the other families' parts
static constexpr float STRAIN_GAP = 3.0f;
static constexpr float STRAIN_SETTLED = 0.1f;	// the channel's bridges wait for the strain to cool below this		// the strain bar under the right rail

static struct
{
	float feed = 0.0f;		// the belt's scroll, in ticks (eased toward fed)
	float fed = 0.0f;		// ticks fed
	float strain = 0.0f;	// 0 to 1
} s_mg;

} // namespace NeoCrosshairMg

void NeoCrosshairPaintMg(const NeoCrosshairFrame &frame)
{
	using namespace NeoCrosshairMg;
	const float s = frame.s;
	const float aim = frame.aim;
	if (frame.bBoot)
	{
		s_mg.feed = s_mg.fed = 0.0f;
		s_mg.strain = 0.0f;
	}
	if (frame.bShot)
	{
		s_mg.fed += 1.0f;
		s_mg.strain = Min(1.0f, s_mg.strain + STRAIN_PER_SHOT);
	}
	s_mg.strain = Max(0.0f, s_mg.strain - STRAIN_COOL * frame.dt);
	s_mg.feed += (s_mg.fed - s_mg.feed) * Min(1.0f, frame.dt / FEED_TIME);
	if (s_mg.fed > 1000.0f)
	{
		s_mg.fed -= 1000.0f;
		s_mg.feed -= 1000.0f;
	}

	const Vector2D near = frame.centre + frame.deviation * RAIL_PARALLAX + frame.jitter
		+ Vector2D(0.0f, STRAIN_SINK * s * NeoSmoothStep(s_mg.strain));
	const float edge = frame.spread + GAP * s;
	const float rail = Lerp(aim, HIP_RAIL, AIM_RAIL) * s;
	const float step = TICK_STEP * s;
	const float scroll = (s_mg.feed - floorf(s_mg.feed)) * step;

	for (int side = -1; side <= 1; side += 2)
	{
		const Vector2D inner = near + Vector2D(side * edge, 0.0f);
		NeoGhostBegin(frame.color, frame.Alpha(0.9f));
		NeoGhostStroke(frame.pen, inner, inner + Vector2D(side * rail, 0.0f), NEO_GHOST_MEDIUM);
		// The belt's ticks, scrolling outward, fading in at the inner end and out at the outer.
		for (float along = scroll; along <= rail; along += step)
		{
			const float t = along / rail;
			const float fade = 0.6f + 0.4f * NeoSmoothStep(t / 0.2f) * (1.0f - NeoSmoothStep((t - 0.75f) / 0.25f));
			NeoGhostBegin(frame.color, frame.Alpha(fade));
			const Vector2D at = inner + Vector2D(side * along, 0.0f);
			NeoGhostStroke(frame.pen, at - Vector2D(0.0f, TICK_HEIGHT * s), at + Vector2D(0.0f, TICK_HEIGHT * s), NEO_GHOST_LIGHT);
		}
	}
	// A short post below, as every NT crosshair has.
	NeoGhostBegin(frame.color, frame.Alpha(0.9f));
	NeoGhostStroke(frame.pen, near + Vector2D(0.0f, edge), near + Vector2D(0.0f, edge + 4.0f * s), NEO_GHOST_MEDIUM);

	// Strain: a bar under the right rail, heavier as it builds.
	if (s_mg.strain > 0.01f)
	{
		const Vector2D start = near + Vector2D(edge, TICK_HEIGHT * s + STRAIN_GAP * s);
		const NeoGhostWeight weight = (s_mg.strain > 0.7f) ? NEO_GHOST_HEAVY : (s_mg.strain > 0.35f) ? NEO_GHOST_MEDIUM : NEO_GHOST_LIGHT;
		NeoGhostBegin(frame.color, frame.Alpha(0.75f + 0.25f * s_mg.strain));
		NeoGhostStroke(frame.pen, start, start + Vector2D(rail * s_mg.strain, 0.0f), weight);
	}
}

// The aim crosshair: a pair of small brackets, the belt's channel. Settled, the brackets' top and bottom run out to
// the rails' inner ends, two lines a side, a channel the belt feeds through.
void NeoCrosshairAimMg(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph)
{
	using namespace NeoCrosshairMg;
	const float s = frame.s;
	const float gap = (bGlyph ? 2.5f : 1.0f) * s;
	const float height = TICK_HEIGHT * s;
	const float edge = frame.spread + GAP * s;
	// Straight out from the aim crosshair, where the rails sit once the layers have come together; and only once the
	// strain has cooled (the rails sink under it, and the channel ran at a slant to them), fading in as it does.
	NeoCrosshairFrame linked = frame;
	linked.link *= 1.0f - NeoSmoothStep(s_mg.strain / STRAIN_SETTLED);
	for (int side = -1; side <= 1; side += 2)
	{
		for (int edgeSide = -1; edgeSide <= 1; edgeSide += 2)
		{
			NeoCrosshairBridge(linked, at + Vector2D(side * gap, edgeSide * height),
				at + Vector2D(side * edge, edgeSide * height), NEO_GHOST_LIGHT);
		}
	}
	if (!bGlyph)
	{
		return;
	}
	// The brightest layer: full strength whatever the layer opacity (that one is the spread view's).
	NeoGhostBegin(frame.color, frame.color.a());
	const float foot = 1.2f * s;
	for (int side = -1; side <= 1; side += 2)
	{
		const Vector2D top = at + Vector2D(side * gap, -height), bottom = at + Vector2D(side * gap, height);
		NeoGhostStroke(frame.pen, top, bottom, NEO_GHOST_HEAVY);
		NeoGhostStroke(frame.pen, top, top - Vector2D(side * foot, 0.0f), NEO_GHOST_MEDIUM);
		NeoGhostStroke(frame.pen, bottom, bottom - Vector2D(side * foot, 0.0f), NEO_GHOST_MEDIUM);
	}
}

float NeoCrosshairReachMg(const NeoCrosshairFrame &frame, float spread)
{
	using namespace NeoCrosshairMg;
	return spread + (GAP + Lerp(frame.aim, HIP_RAIL, AIM_RAIL)) * frame.s;
}
