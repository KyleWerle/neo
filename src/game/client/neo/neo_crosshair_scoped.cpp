#include "cbase.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "view.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The scoped family, at the hip (aimed, the scope takes over): a wide faint corner frame around the hip's big
// spread, a bolt bar under it whose marker slides back and home as the gun cycles, and the range to whatever the
// crosshair is on, typed in beside it.

namespace NeoCrosshairScoped
{
// Sizes in pixels at 1080p.
static constexpr float GAP = 4.0f;				// beyond the spread's edge
static constexpr float ARM = 6.0f;				// each corner's arms
static constexpr float BOLT_WIDTH = 16.0f;		// the bolt bar
static constexpr float BOLT_MARK = 3.0f;
static constexpr float BOLT_GAP = 5.0f;		// under the frame
static constexpr float READOUT_GAP = 4.0f;
static constexpr float TYPE_DELAY = 0.04f;
static constexpr float TYPE_TIME = 0.16f;
static constexpr float RANGE_INTERVAL = 0.1f;	// seconds between range traces
static constexpr float MAX_RANGE = 8192.0f;
static constexpr float METRES_PER_UNIT = 0.0254f;

static struct
{
	float range = 0.0f;	// metres, 0 if nothing in reach
	float lastTrace = -100.0f;
} s_scoped;

} // namespace NeoCrosshairScoped

void NeoCrosshairPaintScoped(const NeoCrosshairFrame &frame)
{
	using namespace NeoCrosshairScoped;
	const float s = frame.s;
	const float now = gpGlobals->realtime;
	if (frame.bBoot || now - s_scoped.lastTrace >= RANGE_INTERVAL)
	{
		s_scoped.lastTrace = now;
		trace_t trace;
		UTIL_TraceLine(MainViewOrigin(), MainViewOrigin() + MainViewForward() * MAX_RANGE, MASK_SHOT, frame.pPlayer,
			COLLISION_GROUP_NONE, &trace);
		s_scoped.range = (trace.fraction < 1.0f) ? trace.fraction * MAX_RANGE * METRES_PER_UNIT : 0.0f;
	}

	const Vector2D near = frame.centre + frame.deviation + frame.jitter;
	const float half = frame.spread + GAP * s;
	const float arm = ARM * s;
	NeoGhostBegin(frame.color, frame.Alpha(0.9f));
	static const Vector2D s_corners[4] = { Vector2D(-1, -1), Vector2D(1, -1), Vector2D(1, 1), Vector2D(-1, 1) };
	for (const Vector2D &corner : s_corners)
	{
		const Vector2D at = near + corner * half;
		NeoGhostStroke(frame.pen, at, at - Vector2D(corner.x * arm, 0.0f), NEO_GHOST_LIGHT);
		NeoGhostStroke(frame.pen, at, at - Vector2D(0.0f, corner.y * arm), NEO_GHOST_LIGHT);
	}

	// The bolt: its marker out and home again over the cycle, home when ready.
	const Vector2D bar = near + Vector2D(-BOLT_WIDTH * 0.5f * s, half + BOLT_GAP * s);
	NeoGhostBegin(frame.color, frame.Alpha(0.75f));
	NeoGhostStroke(frame.pen, bar, bar + Vector2D(BOLT_WIDTH * s, 0.0f), NEO_GHOST_LIGHT);
	const float travel = sinf(M_PI_F * frame.ready) * (BOLT_WIDTH - BOLT_MARK) * s;
	const Vector2D mark = bar + Vector2D(travel, 0.0f);
	NeoGhostBegin(frame.color, frame.Alpha(frame.ready >= 1.0f ? 1.0f : 0.9f));
	NeoGhostStroke(frame.pen, mark, mark + Vector2D(BOLT_MARK * s, 0.0f), NEO_GHOST_HEAVY);

	// The range, beside the frame's right edge.
	wchar_t text[16];
	if (s_scoped.range > 0.0f)
	{
		V_snwprintf(text, ARRAYSIZE(text) - 1, L"%.0f", s_scoped.range);
	}
	else
	{
		V_wcsncpy(text, L"---", sizeof(text));
	}
	const float typed = clamp((frame.sinceBoot - TYPE_DELAY) / TYPE_TIME, 0.0f, 1.0f);
	NeoCrosshairReadout(frame, near + Vector2D(half + READOUT_GAP * s, 0.0f), text, typed, 0.9f);
}
