#include "cbase.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The rifle family: a nod to the original NT crosshairs. At the hip their minimal "Alt" (short square caps on the
// horizontal, a post below), aimed their heavier "Default" (long pill caps, a heavy post, a tick ladder above).
// The caps and post ride the spread and sit near the gun; the ladder is the magazine.

namespace NeoCrosshairRifle
{
// Sizes in pixels at 1080p.
static constexpr float GAP = 5.0f;					// beyond the spread's edge
static constexpr float HIP_CAP = 4.0f;				// "Alt": short square caps
static constexpr float AIM_CAP = 13.0f;			// "Default": long pill caps
static constexpr float HIP_POST = 5.0f;
static constexpr float AIM_POST = 9.0f;
static constexpr float TICK_WIDTH = 5.0f;			// the ladder's ticks
static constexpr float TICK_STEP = 3.0f;
static constexpr int MAX_TICKS = 12;				// more rounds than this: a tick stands for a group
static constexpr float LADDER_PARALLAX = 0.4f;		// the ladder sits further out than the caps
static constexpr float READOUT_GAP = 4.0f;
static constexpr float TYPE_DELAY = 0.04f;			// the readout types in after the strokes trace in
static constexpr float TYPE_TIME = 0.16f;
static constexpr float READY_WIDTH = 10.0f;		// the ready bar under the readout, on slow guns
static constexpr float READY_CYCLE = 0.25f;		// guns cycling this slowly or slower get it
// The aim crosshair: a small echo of the Alt, its arms and post bridging out to the caps and post when settled.
static constexpr float AIM_GAP = 1.5f;
static constexpr float AIM_ARM = 4.0f;
static constexpr float AIM_POST_LENGTH = 3.0f;

} // namespace NeoCrosshairRifle

void NeoCrosshairAimRifle(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph)
{
	using namespace NeoCrosshairRifle;
	const float s = frame.s;
	const float gap = AIM_GAP * s;
	const float arm = bGlyph ? AIM_ARM * s : 0.0f;
	const float post = bGlyph ? AIM_POST_LENGTH * s : 0.0f;
	const float edge = frame.spread + GAP * s;

	// Settled, one crosshair: the arms run on into the caps, the post into the post (straight out from the aim
	// crosshair, where the caps and post sit once the layers have come together).
	for (int side = -1; side <= 1; side += 2)
	{
		NeoCrosshairBridge(frame, at + Vector2D(side * (gap + arm), 0.0f), at + Vector2D(side * edge, 0.0f));
	}
	NeoCrosshairBridge(frame, at + Vector2D(0.0f, gap + post), at + Vector2D(0.0f, edge));
	if (!bGlyph)
	{
		return;
	}
	// The brightest layer: full strength whatever the layer opacity (that one is the spread view's).
	NeoGhostBegin(frame.color, frame.color.a());
	for (int side = -1; side <= 1; side += 2)
	{
		NeoGhostStroke(frame.pen, at + Vector2D(side * gap, 0.0f), at + Vector2D(side * (gap + arm), 0.0f), NEO_GHOST_HEAVY);
	}
	NeoGhostStroke(frame.pen, at + Vector2D(0.0f, gap), at + Vector2D(0.0f, gap + post), NEO_GHOST_MEDIUM);
}

void NeoCrosshairPaintRifle(const NeoCrosshairFrame &frame)
{
	using namespace NeoCrosshairRifle;
	const float s = frame.s;
	const float aim = frame.aim;
	const Vector2D near = frame.centre + frame.deviation + frame.jitter;
	const float edge = frame.spread + GAP * s;
	NeoGhostBegin(frame.color, frame.Alpha(1.0f));

	// End caps on the horizontal: short and square at the hip, long pills aimed.
	const float cap = Lerp(aim, HIP_CAP, AIM_CAP) * s;
	for (int side = -1; side <= 1; side += 2)
	{
		NeoGhostStroke(frame.pen, near + Vector2D(side * edge, 0.0f), near + Vector2D(side * (edge + cap), 0.0f), NEO_GHOST_HEAVY);
	}
	// The post below, heavier aimed.
	const float post = Lerp(aim, HIP_POST, AIM_POST) * s;
	NeoGhostStroke(frame.pen, near + Vector2D(0.0f, edge), near + Vector2D(0.0f, edge + post),
		(aim >= 0.5f) ? NEO_GHOST_HEAVY : NEO_GHOST_MEDIUM);
	if (aim <= 0.0f)
	{
		return;
	}

	// Aimed: the ladder above is the magazine, a tick a round (or a group of them), clearing from the top.
	if (frame.clip > 0 && frame.maxClip > 0)
	{
		const int perTick = (frame.maxClip + MAX_TICKS - 1) / MAX_TICKS;
		const int ticks = (frame.clip + perTick - 1) / perTick;
		const Vector2D ladder = frame.centre + frame.deviation * LADDER_PARALLAX + frame.jitter;
		NeoGhostPen tickPen = frame.pen;
		tickPen.trace = frame.pen.trace * aim;
		for (int i = 0; i < ticks; ++i)
		{
			const float at = edge + (i + 1) * TICK_STEP * s;
			NeoGhostStroke(tickPen, ladder + Vector2D(-TICK_WIDTH * 0.5f * s, -at), ladder + Vector2D(TICK_WIDTH * 0.5f * s, -at),
				NEO_GHOST_LIGHT);
		}
	}

	// Beside the right cap: the cone's full width in degrees.
	const Vector2D readoutAt = near + Vector2D(edge + cap + READOUT_GAP * s, 0.0f);
	wchar_t text[16];
	V_snwprintf(text, ARRAYSIZE(text) - 1, L"%.1f", RAD2DEG(2.0f * atanf(frame.pWeapon->GetBulletSpread().x)));
	const float typed = Min(aim, clamp((frame.sinceBoot - TYPE_DELAY) / TYPE_TIME, 0.0f, 1.0f));
	NeoCrosshairReadout(frame, readoutAt, text, typed, aim * 0.9f);

	// Slow guns: a ready bar under it, filling as the gun cycles to its next shot.
	if (frame.cycle >= READY_CYCLE)
	{
		const Vector2D bar = readoutAt + Vector2D(0.0f, NeoCrosshairReadoutTall() * 0.5f + 2.0f * s);
		NeoGhostBegin(frame.color, frame.Alpha(aim * (frame.ready >= 1.0f ? 1.0f : 0.75f)));
		NeoGhostStroke(frame.pen, bar, bar + Vector2D(READY_WIDTH * s * frame.ready, 0.0f), NEO_GHOST_MEDIUM);
	}
}

float NeoCrosshairReachRifle(const NeoCrosshairFrame &frame, float spread)
{
	using namespace NeoCrosshairRifle;
	const float s = frame.s, edge = spread + GAP * s;
	const float cap = Lerp(frame.aim, HIP_CAP, AIM_CAP) * s, post = Lerp(frame.aim, HIP_POST, AIM_POST) * s;
	// Aimed: the magazine ladder above, the readout beside the right cap (its text about 28 px wide).
	const float ladder = edge + MAX_TICKS * TICK_STEP * s * frame.aim;
	const float readout = edge + cap + (READOUT_GAP + 28.0f) * s * frame.aim;
	return Max(Max(edge + Max(cap, post), ladder), readout);
}
