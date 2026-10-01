#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_hud_model_team.h"
#include "neo_hud_spring.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The registration grid on the backings (HUD-NEXT.md; Kyle, 2026-09-30 and 10-01). pushbak's sheets carry a grid of
// registration crosses about 36 units apart at 640 x 480: here one grid for the whole screen, its pitch from the
// height (whole rows, so it holds at every resolution and aspect), but drawn only on the receptor groups' backings,
// tight to them, a little of it running off the soft edge. Each backing shows the grid's thirds, the master crosses
// larger. The grid is alive:
// - Linked grids: a group's own grid sits off-register while it's quiet and notches into the screen's grid as its
//   attention rises (four notches), so the groups that matter line up with each other: the neural link.
// - It swells with attention: stronger, and a slow breath with full motion.
// - An activation (the group rising a perception layer) breaks its grid up, the crosses scattered and some gone, in
//   the highlight colour, then it reforms into register.
// Team: your team's colour, its faction's crosses (Jinrai's cut through the middle, NSF's long and fine). Neutral:
// the issued grid, plain crosses, orange highlights (small, less intense pops, as the style guide has them). Still
// motion: nothing travels or scatters; the grid is always registered and only its strength changes.

ConVar cl_neo_hud_grid("cl_neo_hud_grid", "1", FCVAR_ARCHIVE,
	"The registration grid on the cyberbrain HUD's backings: 0 = off, 1 = team (your team's colour and its faction's"
	" crosses), 2 = neutral (the issued grid, orange highlights).", true, 0, true, 2);

namespace NeoCyberbrain
{
constexpr int GRID_ROWS = 13, GRID_SUB = 3;			// the screen's rows; the backings show thirds of a cell
constexpr float GRID_REACH = 0.7f;					// how far past the backing's edge into its feather the grid runs
constexpr float LOOSE = 0.42f;						// the most a quiet group's grid sits off-register, in sub-cells
const Color GRID_NEUTRAL(208, 123, 43, 255);		// Grupo 6 orange (ART-DIRECTION.md)

// The comfort forms: how long the notching eases and an activation takes to reform; whether anything travels.
struct GridMotion { float notchFor, reformFor; bool bTravel; float breath; };
static GridMotion GridMotionOf()
{
	static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
	switch (cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1)
	{
	case 0:		return { 0.5f, 0.6f, false, 0.0f };
	case 2:		return { 0.12f, 0.45f, true, 1.0f };
	default:	return { 0.25f, 0.6f, true, 0.0f };
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

void PaintGrid(const Frame &f, int slot, const Vector2D &centre, const Vector2D &half, float feather, float strength)
{
	const int mode = cl_neo_hud_grid.GetInt();
	if (mode == 0 || slot >= GROUP__COUNT || strength <= 0.0f)
		return;
	const Place &p = f.pPlaces[slot];
	const GridMotion m = GridMotionOf();
	const int team = GetLocalPlayerTeam();
	const bool bTeam = mode == 1 && (team == TEAM_JINRAI || team == TEAM_NSF);
	const int hand = bTeam ? team : 0;
	const Color base = bTeam ? NeoHud::TeamColour(team) : f.color;
	const Color highlight = bTeam ? NeoHud::TeamColour(team) : GRID_NEUTRAL;

	// The screen's grid: whole rows, columns centred, pixel-snapped.
	const float pitch = static_cast<float>(Max(RoundFloatToInt(f.tall / static_cast<float>(GRID_ROWS)), GRID_SUB));
	const float sub = pitch / GRID_SUB;
	const Vector2D origin(fmodf(f.wide - floorf(f.wide / pitch) * pitch, pitch) * 0.5f, fmodf(f.tall - GRID_ROWS * pitch, pitch) * 0.5f);

	// Notched attention: four registrations, eased between (instantly registered when still).
	static float s_notch[GROUP__COUNT], s_notchVel[GROUP__COUNT];
	const float target = floorf(clamp(p.att, 0.0f, 1.0f) * 4.0f + 0.5f) / 4.0f;
	float &notch = s_notch[slot];
	if (!m.bTravel)
		notch = 1.0f;
	else
		NeoHudSpring(notch, s_notchVel[slot], target, 5.0f / m.notchFor, 1.0f, gpGlobals->frametime);
	const float loose = (1.0f - clamp(notch, 0.0f, 1.0f)) * LOOSE * sub;
	const Vector2D off(loose * (slot % 2 ? 1.0f : -1.0f), loose * 0.8f * (slot < 2 ? 1.0f : -1.0f));

	// An activation: the grid breaks up and reforms.
	const float age = f.now - p.layerChanged;
	const float broken = m.bTravel && p.layer > p.lastLayer && age < m.reformFor ? 1.0f - NeoSmoothStep(age / m.reformFor) : 0.0f;

	const float breath = 1.0f + 0.15f * m.breath * p.att * sinf(f.now * 1.7f + slot * 1.3f);
	const float alpha = strength * (0.14f + 0.22f * p.att) * breath;
	const float reachX = half.x + feather * GRID_REACH, reachY = half.y + feather * GRID_REACH;
	const int i0 = static_cast<int>(floorf((centre.x - reachX - origin.x) / sub)), i1 = static_cast<int>(ceilf((centre.x + reachX - origin.x) / sub));
	const int j0 = static_cast<int>(floorf((centre.y - reachY - origin.y) / sub)), j1 = static_cast<int>(ceilf((centre.y + reachY - origin.y) / sub));
	NeoGhostOutline(0.0f);	// texture, not linework: no dark rim (the next group's frame sets it again)
	for (int i = i0; i <= i1; ++i)
	{
		for (int j = j0; j <= j1; ++j)
		{
			Vector2D at(origin.x + i * sub, origin.y + j * sub);
			at += off;
			// Tight to the backing: full inside its outline, fading out across part of its feather.
			const float ex = (at.x - centre.x) / Max(half.x, 1.0f), ey = (at.y - centre.y) / Max(half.y, 1.0f);
			const float e = sqrtf(ex * ex + ey * ey);
			const float edge = 1.0f + feather * GRID_REACH / Max(Min(half.x, half.y), 1.0f);
			float a = alpha * (1.0f - NeoSmoothStep((e - 1.0f) / (edge - 1.0f)));
			if (a <= 0.01f)
				continue;
			Color c = base;
			if (broken > 0.0f)
			{
				if (GridHash(i, j, slot) < 0.35f * broken)
					continue;	// gone a moment
				at.x += (GridHash(i, j, slot + 7) - 0.5f) * 1.2f * sub * broken;
				at.y += (GridHash(i, j, slot + 13) - 0.5f) * 1.2f * sub * broken;
				c = highlight;
				a = Min(1.0f, a * (1.0f + 1.5f * broken));
			}
			const bool bMajor = (i % GRID_SUB == 0) && (j % GRID_SUB == 0);
			at.Init(roundf(at.x), roundf(at.y));
			GridCross(f, at, (bMajor ? 5.0f : 2.5f) * f.s, hand, c, bMajor ? Min(1.0f, a * 1.5f) : a);
		}
	}
}
} // namespace NeoCyberbrain
