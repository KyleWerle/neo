#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "c_neo_player.h"
#include "neo_ironsights.h"
#include "neo_hud_light.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Readability on bright scenes (the firing range, daylight maps). How bright the scene is behind each group and the
// ring comes from the HUD's light (neo_hud_light.h), one ray a frame round the slots, eased. Behind each: a feathered
// dark backing, stronger the brighter it is (cl_neo_hud_backing); over it, the strokes get a dark outline and text a
// dark edge as the scene brightens.

namespace NeoCyberbrain
{
constexpr int SLOTS = GROUP__COUNT + 1, SPOTS = 3;
constexpr float BACK_MIN = 0.1f, BACK_MAX = 0.75f;	// the backing's opacity in the dark, and in daylight (Kyle: more on bright scenes)
constexpr float FEATHER = 40.0f;
constexpr int BACKING_POINTS = 28;

static struct
{
	float samples[SLOTS][SPOTS] = {};
	float bright[SLOTS] = { 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f };
	int next = 0;
} s_bright;

void MeasureBrightness(C_NEO_Player *pPlayer, Frame &f, float dt, bool bBoot)
{
	for (int slot = 0; slot < SLOTS; ++slot)
	{
		const bool bSample = bBoot || s_bright.next % SLOTS == slot;
		if (bSample)
		{
			Vector2D centre, half;
			GroupExtent(f, slot, centre, half);
			for (int spot = 0; spot < SPOTS; ++spot)
			{
				if (bBoot || (s_bright.next / SLOTS) % SPOTS == spot)
				{
					const Vector2D at = centre + Vector2D((spot - 1) * half.x * 0.6f, 0.0f);
					s_bright.samples[slot][spot] = NeoHudSceneBrightness(pPlayer, at);
				}
			}
		}
		NeoHudEaseBrightness(s_bright.bright[slot], s_bright.samples[slot], SPOTS, dt, bBoot);
		f.bright[slot] = s_bright.bright[slot];
	}
	s_bright.next = (s_bright.next + 1) % (SLOTS * SPOTS);
}

void PaintBackings(const Frame &f)
{
	if (!NeoHudBackingsOn())
	{
		return;
	}
	for (int slot = 0; slot < SLOTS; ++slot)
	{
		// The ring on the body shares the body's backing.
		if (slot == BRIGHT_RING && f.style == NEO_HUD_STYLE_BODY)
		{
			continue;
		}
		if (slot == GROUP_WEAPON && !f.pSenses->ammo.bShown)
		{
			continue;
		}
		Vector2D centre, half;
		GroupExtent(f, slot, centre, half);
		const float att = slot == BRIGHT_RING ? 0.5f : f.pPlaces[slot].att;
		const float alpha = NeoHudBackingOpacity(f.bright[slot], BACK_MIN, BACK_MAX) * (0.75f + 0.25f * att) * f.alpha;
		NeoHudPaintBacking(centre, half, Vector2D(FEATHER * f.s, FEATHER * f.s), alpha, BACKING_POINTS);
	}
}

Frame ForGroup(const Frame &f, int slot)
{
	Frame g = f;
	g.contrast = NeoSmoothStep((f.bright[slot] - 0.3f) / 0.45f);
	NeoGhostOutline(0.75f * g.contrast);
	return g;
}
} // namespace NeoCyberbrain
