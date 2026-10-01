#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_hud_model_team.h"
#include "neo_hud_spring.h"
#include "neo_enums.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The registration grid (HUD-NEXT.md; Kyle, 2026-09-30 and 10-01). pushbak's sheets carry a grid of registration
// crosses about 36 units apart at 640 x 480: here one grid for the whole screen, its pitch from the height (whole rows,
// so it holds at every resolution and aspect), shown only where it marks the receptor groups. Never behind the readout
// (Kyle: a field of crosses behind the information was too much; "grid points marking directly to elements" was the
// good part):
// - Each group's registration crosses sit at its four corners, on the deep layer (the chassis's crosses, these now).
// - Linked grids: while a group is quiet its crosses sit where its shape puts them; as its attention rises they notch
//   (four notches) onto the screen's grid, so the groups that matter register with each other: the neural link.
// - From each corner a short trail of the grid runs outward, away from the readout (never down past the channel
//   codes): one faint cross at rest, more with attention. A trail stops where it would leave the screen, enter the
//   crosshair's keep-out, another group, the ring or the squad list.
// - An activation (the group rising a perception layer) dissolves it, the trail scattered and thinned in the
//   highlight colour, the corners knocked off, and it reforms into register.
// - Aiming focuses it (Kyle: the world past the sight tuned out, the HUD clicking into register round it): every
//   group's crosses register, they strengthen, and the trails run one cross further.
// - Vision modes put it through the class's sensor, never competing with what the mode shows, never looking like a
//   target (Kyle: "still visible but distorted based on vision mode"): night vision, phosphor (green, grain, a
//   little bloom); thermal, cold (dark blue, a slight shimmer), never warm; motion vision, near gone while you hold
//   still and shown by your own turning, a short smear in the HUD's colour, never the mode's highlight.
// Team: your team's colour, its faction's crosses (Jinrai's cut through the middle, NSF's long and fine). Neutral: the
// issued grid, plain crosses in the HUD's colour, orange highlights (small, less intense pops, as the style guide has
// them). Still motion: always registered, nothing scatters; only strength changes.

ConVar cl_neo_hud_grid("cl_neo_hud_grid", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's registration grid: each group's corner crosses notching onto one screen grid with attention,"
	" a short trail of it running outward. 0 = the plain crosses (no grid), 1 = team (your team's colour and its"
	" faction's crosses), 2 = neutral (the issued grid, orange highlights).", true, 0, true, 2);

namespace NeoCyberbrain
{
constexpr int GRID_ROWS = 13, GRID_SUB = 3;		// the screen's rows; the trails step in thirds of a cell
constexpr int TRAIL_MAX = 3;					// grid crosses per arm of a corner's trail, at full attention
constexpr float CORNER = 4.0f, TRAIL = 2.5f;	// arm lengths, pixels at 1080p
constexpr float CORNER_ALPHA = 0.5f, TRAIL_ALPHA = 0.35f;
const Color GRID_NEUTRAL(208, 123, 43, 255);	// Grupo 6 orange (ART-DIRECTION.md)

bool GridOn()
{
	return cl_neo_hud_grid.GetInt() != 0;
}

// The comfort forms: how fast the notching eases and how long an activation takes to reform; whether anything travels.
struct GridMotion { float notchFor, reformFor; bool bTravel; };
static GridMotion GridMotionOf()
{
	static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
	switch (cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1)
	{
	case 0:		return { 0.5f, 0.6f, false };
	case 2:		return { 0.12f, 0.45f, true };
	default:	return { 0.25f, 0.6f, true };
	}
}

static float GridHash(int x, int y, int seed)
{
	unsigned int h = static_cast<unsigned int>(x) * 374761393u + static_cast<unsigned int>(y) * 668265263u
		+ static_cast<unsigned int>(seed) * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

// One cross in the grid's hand: arm in pixels.
static void GridCross(const Frame &f, const Vector2D &at, float arm, int hand, const Color &c, float a)
{
	if (hand == TEAM_JINRAI)
	{
		// Heavy and hand-cut: a notch out of the middle.
		const float gap = Max(1.0f, arm * 0.3f);
		Line(f, Vector2D(at.x - arm, at.y), Vector2D(at.x - gap, at.y), NEO_GHOST_LIGHT, c, a);
		Line(f, Vector2D(at.x + gap, at.y), Vector2D(at.x + arm, at.y), NEO_GHOST_LIGHT, c, a);
		Line(f, Vector2D(at.x, at.y - arm), Vector2D(at.x, at.y - gap), NEO_GHOST_LIGHT, c, a);
		Line(f, Vector2D(at.x, at.y + gap), Vector2D(at.x, at.y + arm), NEO_GHOST_LIGHT, c, a);
		return;
	}
	// NSF's run long and fine; the issued grid's are plain.
	const float k = hand == TEAM_NSF ? 1.35f : 1.0f;
	Line(f, Vector2D(at.x - arm * k, at.y), Vector2D(at.x + arm * k, at.y), NEO_GHOST_LIGHT, c, a);
	Line(f, Vector2D(at.x, at.y - arm * k), Vector2D(at.x, at.y + arm * k), NEO_GHOST_LIGHT, c, a);
}

// What the grid is seen through this frame: the aim's focus and the vision mode.
enum GridVision { VISION_NONE, VISION_NIGHT, VISION_THERMAL, VISION_MOTION };
struct Lens { float aim = 0.0f; GridVision vision = VISION_NONE; float turn = 0.0f, motionSeen = 1.0f; bool bTravel = true; };
static Lens LensOf(const Frame &f, const GridMotion &m)
{
	static Lens s_lens;
	static int s_frame = -1;
	if (s_frame == gpGlobals->framecount)
		return s_lens;
	s_frame = gpGlobals->framecount;
	const Senses &sense = *f.pSenses;
	// The aim eases as the ADS blend does (0.35 s); still, it's there or not.
	const float goal = sense.bInAim ? 1.0f : 0.0f;
	s_lens.aim = m.bTravel ? Approach(goal, s_lens.aim, gpGlobals->frametime / 0.35f) : goal;
	s_lens.vision = !sense.bVision ? VISION_NONE : sense.neoClass == NEO_CLASS_RECON ? VISION_NIGHT
		: sense.neoClass == NEO_CLASS_SUPPORT ? VISION_THERMAL : sense.neoClass == NEO_CLASS_ASSAULT ? VISION_MOTION : VISION_NONE;
	// Motion vision: seen by how fast you turn (degrees a second), the smear opposite the turn.
	const float rate = sqrtf(sense.yawRate * sense.yawRate + sense.pitchRate * sense.pitchRate);
	s_lens.motionSeen = clamp(rate / 120.0f, 0.08f, 1.0f);
	s_lens.turn = clamp(sense.yawRate * 0.04f, -14.0f, 14.0f);
	s_lens.bTravel = m.bTravel;
	return s_lens;
}

const Color GRID_PHOSPHOR(120, 255, 140, 255), GRID_COLD(70, 100, 180, 255);

// One grid cross through the lens; i, j seed its grain.
static void GridMark(const Frame &f, const Lens &lens, const Vector2D &at, float arm, int hand, const Color &c, float a, int i, int j)
{
	switch (lens.vision)
	{
	case VISION_NIGHT:
	{
		// Phosphor: the tube's green, its grain crawling (held still at Still), a faint bloom round each cross.
		const float grain = lens.bTravel ? 0.55f + 0.45f * GridHash(i, j, static_cast<int>(f.now * 20.0f)) : 0.8f;
		GridCross(f, at, arm * 1.8f, hand, GRID_PHOSPHOR, 0.2f * a);
		GridCross(f, at, arm, hand, GRID_PHOSPHOR, 0.8f * a * grain);
		return;
	}
	case VISION_THERMAL:
	{
		// Cold: the palette's dark end, a slight heat shimmer (none at Still).
		const float shimmer = lens.bTravel ? sinf(f.now * 6.0f + at.x * 0.05f) * 1.2f * f.s : 0.0f;
		GridCross(f, Vector2D(at.x, at.y + shimmer), arm, hand, GRID_COLD, 0.75f * a);
		return;
	}
	case VISION_MOTION:
	{
		// Near gone held still; your turning shows it, a short smear trailing the turn (no smear at Still).
		const float seen = a * lens.motionSeen;
		GridCross(f, at, arm, hand, c, seen);
		if (lens.bTravel && fabsf(lens.turn) > 1.0f)
			Line(f, at, Vector2D(at.x - lens.turn * f.s, at.y), NEO_GHOST_LIGHT, c, 0.6f * seen);
		return;
	}
	default:
		GridCross(f, at, arm, hand, c, a);
	}
}

// The boxes a trail mustn't run into this frame: the other groups, the ring (not the body's own disc on the body),
// the squad list (pixels, centre and half).
struct GridBlock { Vector2D centre, half; };
static int GridBlocks(const Frame &f, int slot, GridBlock *pOut)
{
	int n = 0;
	for (int g = 0; g <= BRIGHT_RING; ++g)
	{
		if (g == slot || g == GROUP_LINK || (g == GROUP_WEAPON && !f.pSenses->ammo.bShown) || (g == BRIGHT_RING && (f.ringRadii.x <= 0.0f || (slot == GROUP_BODY && f.style == NEO_HUD_STYLE_BODY))))
			continue;
		GroupExtent(f, g, pOut[n].centre, pOut[n].half);
		++n;
	}
	// The squad list's corner (the team element, top left): layout_check.py's box.
	pOut[n].centre.Init(184.0f * f.s, 130.0f * f.s);
	pOut[n].half.Init(176.0f * f.s, 130.0f * f.s);
	return n + 1;
}

static bool GridClear(const Frame &f, const GridBlock *pBlocks, int blocks, const Vector2D &at, float pad)
{
	if (at.x < pad || at.y < pad || at.x > f.wide - pad || at.y > f.tall - pad || InKeepout(f, at))
		return false;
	for (int b = 0; b < blocks; ++b)
	{
		if (fabsf(at.x - pBlocks[b].centre.x) < pBlocks[b].half.x + pad && fabsf(at.y - pBlocks[b].centre.y) < pBlocks[b].half.y + pad)
			return false;
	}
	return true;
}

void PaintGrid(const Frame &f, int slot, float left, float right, float top, float bottom)
{
	const Place &p = f.pPlaces[slot];
	const GridMotion m = GridMotionOf();
	const int team = GetLocalPlayerTeam();
	const bool bTeam = cl_neo_hud_grid.GetInt() == 1 && (team == TEAM_JINRAI || team == TEAM_NSF);
	const int hand = bTeam ? team : 0;
	const Color base = bTeam ? NeoHud::TeamColour(team) : f.color;
	const Color highlight = bTeam ? NeoHud::TeamColour(team) : GRID_NEUTRAL;
	const float s = f.s;

	// The screen's grid: whole rows, columns centred, pixel-snapped; the trails step in its thirds.
	const float pitch = static_cast<float>(Max(RoundFloatToInt(f.tall / static_cast<float>(GRID_ROWS)), GRID_SUB));
	const float sub = pitch / GRID_SUB;
	const Vector2D origin((f.wide - floorf(f.wide / pitch) * pitch) * 0.5f, (f.tall - GRID_ROWS * pitch) * 0.5f);

	// Notched attention: four registrations, eased between (always registered when still); aiming registers all.
	const Lens lens = LensOf(f, m);
	static float s_notch[GROUP__COUNT], s_notchVel[GROUP__COUNT];
	float &notch = s_notch[slot];
	if (!m.bTravel)
		notch = 1.0f;
	else
		NeoHudSpring(notch, s_notchVel[slot], Max(floorf(clamp(p.att, 0.0f, 1.0f) * 4.0f + 0.5f) / 4.0f, lens.aim > 0.5f ? 1.0f : 0.0f),
			5.0f / m.notchFor, 1.0f, gpGlobals->frametime);
	notch = clamp(notch, 0.0f, 1.0f);

	// An activation dissolves it; it reforms.
	const float age = f.now - p.layerChanged;
	const float broken = m.bTravel && p.layer > p.lastLayer && age < m.reformFor ? 1.0f - NeoSmoothStep(age / m.reformFor) : 0.0f;
	const int trail = Min(TRAIL_MAX + 1, 1 + static_cast<int>(p.att * TRAIL_MAX + 0.5f) + (lens.aim > 0.5f ? 1 : 0));
	const float strength = (0.6f + 0.4f * p.att) * (1.0f + 0.5f * lens.aim);

	GridBlock blocks[BRIGHT_RING + 2];
	const int nBlocks = GridBlocks(f, slot, blocks);
	const Vector2D corners[4] = { Vector2D(left, top), Vector2D(right, top), Vector2D(left, bottom), Vector2D(right, bottom) };
	for (int k = 0; k < 4; ++k)
	{
		const Vector2D natural = corners[k];
		const int gi = RoundFloatToInt((natural.x - origin.x) / sub), gj = RoundFloatToInt((natural.y - origin.y) / sub);
		const Vector2D snapped(origin.x + gi * sub, origin.y + gj * sub);
		Vector2D at = natural + (snapped - natural) * notch;
		const Vector2D out(k % 2 ? 1.0f : -1.0f, k < 2 ? -1.0f : 1.0f);	// away from the readout
		if (broken > 0.0f)
			at += Vector2D(GridHash(gi, gj, slot) - 0.5f, GridHash(gj, gi, slot + 3) - 0.5f) * (0.8f * sub * broken);
		GridMark(f, lens, Vector2D(roundf(at.x), roundf(at.y)), CORNER * s, hand, broken > 0.0f ? highlight : base,
			Min(1.0f, CORNER_ALPHA * strength * (1.0f + broken)), gi, gj);

		// The trail: the grid continuing outward along both of the corner's outer edges.
		for (int axis = 0; axis < 2; ++axis)
		{
			if (axis == 1 && k >= 2)
				continue;	// not down from the bottom corners: the channel codes are there
			for (int n = 1; n <= trail; ++n)
			{
				Vector2D t = at + (axis == 0 ? Vector2D(out.x * sub * n, 0.0f) : Vector2D(0.0f, out.y * sub * n));
				if (!GridClear(f, blocks, nBlocks, t, 6.0f * s))
					break;	// the trail ends here
				float a = TRAIL_ALPHA * strength * (1.0f - n / (trail + 1.0f));
				Color c = base;
				if (broken > 0.0f)
				{
					if (GridHash(gi * 7 + n, gj * 5 + axis, slot) < 0.4f * broken)
						continue;	// gone a moment
					t += Vector2D(GridHash(n, axis, gi + slot) - 0.5f, GridHash(axis, n, gj + slot) - 0.5f) * (1.4f * sub * broken);
					c = highlight;
					a = Min(1.0f, a * (1.0f + 1.5f * broken));
				}
				GridMark(f, lens, Vector2D(roundf(t.x), roundf(t.y)), TRAIL * s, hand, c, Min(1.0f, a), gi * 8 + n, gj * 8 + axis);
			}
		}
	}
}
} // namespace NeoCyberbrain
