#include "cbase.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"
#include "neo_gunplay_spread_ghost.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The SMG family: the MPN45's dotted box. Four dotted corner brackets frame the spread, opening with bloom. While
// the gun fires, a bright run chases around the box, faster the faster the gun cycles, and slows to a stop after
// the burst; aimed, a row of dots under the box is the magazine, draining from the right.

namespace NeoCrosshairSmg
{
// Sizes in pixels at 1080p.
static constexpr float GAP = 4.0f;				// beyond the spread's edge
static constexpr float MIN_HALF = 7.0f;		// the box's smallest half-size
static constexpr float HIP_ARM = 4.0f;			// each corner's arms, at the hip and aimed
static constexpr float AIM_ARM = 7.0f;
static constexpr float DOT_STEP = 2.2f;		// between a bracket's dots
static constexpr float RUN_WIDTH = 0.06f;		// the chase's bright run, a fraction of the way around
static constexpr float RUN_LAPS = 0.12f;		// laps of the box per shot's cycle, at full speed
static constexpr float RUN_EASE = 0.35f;		// seconds the run takes to slow to a stop after the burst
static constexpr int MAX_MAG_DOTS = 15;		// more rounds than this: a dot stands for a group
static constexpr float MAG_STEP = 2.4f;
static constexpr float MAG_GAP = 5.0f;			// under the box
// The aim crosshair: a small box of corner brackets, its corners bridging out to the spread ghost's on the
// diagonals when settled (a box in a box, a tunnel, the dotted box framing it).
static constexpr float AIM_HALF = 3.0f;
static constexpr float AIM_BRACKET = 1.8f;

static struct
{
	float phase = 0.0f;	// where the run is, 0 to 1 around the box
	float speed = 0.0f;	// 0 to 1: eased toward 1 while firing
} s_smg;

} // namespace NeoCrosshairSmg

void NeoCrosshairPaintSmg(const NeoCrosshairFrame &frame)
{
	using namespace NeoCrosshairSmg;
	const float s = frame.s;
	const float aim = frame.aim;
	if (frame.bBoot)
	{
		s_smg.speed = 0.0f;
	}
	// The run: on while the gun keeps firing (a shot within two cycles), easing off after.
	const bool bFiring = frame.sinceShot < frame.cycle * 2.0f + 0.05f;
	s_smg.speed = Approach(bFiring ? 1.0f : 0.0f, s_smg.speed, frame.dt / RUN_EASE);
	const float lapsPerSecond = RUN_LAPS / Max(frame.cycle, 0.03f);
	s_smg.phase += NeoSmoothStep(s_smg.speed) * lapsPerSecond * frame.dt;
	s_smg.phase -= floorf(s_smg.phase);
	const float run = NeoSmoothStep(s_smg.speed);

	const Vector2D near = frame.centre + frame.deviation + frame.jitter;
	const float half = Max(frame.spread + GAP * s, MIN_HALF * s);
	const float arm = Lerp(aim, HIP_ARM, AIM_ARM) * s;
	const int dotsPerArm = Max(2, static_cast<int>(arm / (DOT_STEP * s)) + 1);

	// Each corner's two arms, as dots from the corner outward; each dot's place around the box (0 to 1, clockwise
	// from the top left) sets how near the run it is.
	static const Vector2D s_corners[4] = { Vector2D(-1, -1), Vector2D(1, -1), Vector2D(1, 1), Vector2D(-1, 1) };
	for (int c = 0; c < 4; ++c)
	{
		const Vector2D corner = near + s_corners[c] * half;
		// Along the box's edge each way from the corner: the clockwise edge and the one before it.
		const Vector2D next = s_corners[(c + 1) % 4] - s_corners[c];
		const Vector2D previous = s_corners[(c + 3) % 4] - s_corners[c];
		for (int side = 0; side < 2; ++side)
		{
			const Vector2D direction = (side ? previous : next) * 0.5f;
			for (int i = (side ? 1 : 0); i < dotsPerArm; ++i)
			{
				const float along = i * DOT_STEP * s;
				const Vector2D at = corner + direction * along;
				// Around the box: the corner is c / 4, its arms either side of it.
				float place = c * 0.25f + (side ? -1.0f : 1.0f) * (along / (8.0f * half));
				place -= floorf(place);
				float distance = fabsf(place - s_smg.phase);
				distance = Min(distance, 1.0f - distance);
				const float bright = run * Max(0.0f, 1.0f - distance / RUN_WIDTH);
				NeoGhostBegin(frame.color, frame.Alpha(0.8f + 0.2f * bright));
				NeoCrosshairDot(frame, at, (bright > 0.5f) ? NEO_GHOST_HEAVY : NEO_GHOST_MEDIUM);
			}
		}
	}

	// Aimed: the magazine as a row of dots under the box, draining from the right.
	if (aim > 0.0f && frame.clip > 0 && frame.maxClip > 0)
	{
		const int perDot = (frame.maxClip + MAX_MAG_DOTS - 1) / MAX_MAG_DOTS;
		const int dots = (frame.clip + perDot - 1) / perDot;
		const int slots = (frame.maxClip + perDot - 1) / perDot;
		const float width = (slots - 1) * MAG_STEP * s;
		const Vector2D row = frame.centre + frame.deviation * 0.5f + frame.jitter + Vector2D(-width * 0.5f, half + MAG_GAP * s);
		NeoGhostBegin(frame.color, frame.Alpha(aim * 0.9f));
		for (int i = 0; i < dots; ++i)
		{
			NeoCrosshairDot(frame, row + Vector2D(i * MAG_STEP * s, 0.0f), NEO_GHOST_LIGHT);
		}
	}
}

void NeoCrosshairAimSmg(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph)
{
	using namespace NeoCrosshairSmg;
	const float s = frame.s;
	const float inner = (bGlyph ? AIM_HALF : 1.0f) * s;
	// The spread ghost's corners, around the same point as the aim crosshair: from the dotted box's they swung with
	// its knock (Kyle: started from the grid, looked weird).
	const float ghost = NeoGunplaySpreadGhostEdge();
	static const Vector2D s_corners[4] = { Vector2D(-1, -1), Vector2D(1, -1), Vector2D(1, 1), Vector2D(-1, 1) };

	// Settled: each small corner runs out on the diagonal to the spread ghost's.
	for (int c = 0; ghost > inner && c < 4; ++c)
	{
		NeoCrosshairBridge(frame, at + s_corners[c] * inner, at + s_corners[c] * ghost);
	}
	if (!bGlyph)
	{
		return;
	}
	// The brightest layer: full strength whatever the layer opacity (that one is the spread view's).
	NeoGhostBegin(frame.color, frame.color.a());
	const float bracket = AIM_BRACKET * s;
	for (int c = 0; c < 4; ++c)
	{
		const Vector2D corner = at + s_corners[c] * inner;
		NeoGhostStroke(frame.pen, corner, corner - Vector2D(s_corners[c].x * bracket, 0.0f), NEO_GHOST_MEDIUM);
		NeoGhostStroke(frame.pen, corner, corner - Vector2D(0.0f, s_corners[c].y * bracket), NEO_GHOST_MEDIUM);
	}
}

float NeoCrosshairReachSmg(const NeoCrosshairFrame &frame, float spread)
{
	using namespace NeoCrosshairSmg;
	const float s = frame.s, half = Max(spread + GAP * s, MIN_HALF * s);
	return half + (MAG_GAP + 2.0f) * s * frame.aim;	// aimed, the magazine's dot row under the box
}
