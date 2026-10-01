#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_hud_model_team.h"
#include "neo_hud_spring.h"

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
//   codes): one faint cross at rest, more with attention.
// - An activation (the group rising a perception layer) dissolves it, the trail scattered and thinned in the
//   highlight colour, the corners knocked off, and it reforms into register.
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

	// Notched attention: four registrations, eased between (always registered when still).
	static float s_notch[GROUP__COUNT], s_notchVel[GROUP__COUNT];
	float &notch = s_notch[slot];
	if (!m.bTravel)
		notch = 1.0f;
	else
		NeoHudSpring(notch, s_notchVel[slot], floorf(clamp(p.att, 0.0f, 1.0f) * 4.0f + 0.5f) / 4.0f, 5.0f / m.notchFor, 1.0f,
			gpGlobals->frametime);
	notch = clamp(notch, 0.0f, 1.0f);

	// An activation dissolves it; it reforms.
	const float age = f.now - p.layerChanged;
	const float broken = m.bTravel && p.layer > p.lastLayer && age < m.reformFor ? 1.0f - NeoSmoothStep(age / m.reformFor) : 0.0f;
	const int trail = Min(TRAIL_MAX, 1 + static_cast<int>(p.att * TRAIL_MAX + 0.5f));
	const float strength = 0.6f + 0.4f * p.att;

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
		GridCross(f, Vector2D(roundf(at.x), roundf(at.y)), CORNER * s, hand, broken > 0.0f ? highlight : base,
			Min(1.0f, CORNER_ALPHA * strength * (1.0f + broken)));

		// The trail: the grid continuing outward along both of the corner's outer edges.
		for (int axis = 0; axis < 2; ++axis)
		{
			if (axis == 1 && k >= 2)
				continue;	// not down from the bottom corners: the channel codes are there
			for (int n = 1; n <= trail; ++n)
			{
				Vector2D t = at + (axis == 0 ? Vector2D(out.x * sub * n, 0.0f) : Vector2D(0.0f, out.y * sub * n));
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
				GridCross(f, Vector2D(roundf(t.x), roundf(t.y)), TRAIL * s, hand, c, a);
			}
		}
	}
}
} // namespace NeoCyberbrain
