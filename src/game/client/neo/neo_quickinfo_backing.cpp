#include "cbase.h"
#include "neo_quickinfo_internal.h"
#include "neo_hud_light.h"
#include "c_neo_player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The dark backings (QUICKINFO.md): a feathered blob behind the band, another behind the speed graph, so the linework
// reads on bright maps. How dark follows how bright the scene is behind each (the HUD's light, neo_hud_light.h),
// measured through the blob's spots on screen. One ray a frame, round the spots, eased; nothing in the dark, up to
// BACK_MAX in daylight.

ConVar cl_neo_hud_quickinfo_backing_debug("cl_neo_hud_quickinfo_backing_debug", "0", FCVAR_NONE,
	"Debug: print each backing's measured brightness (0 dark to 1 bright) and opacity once a second.");

namespace NeoQuickInfo
{
constexpr float BACK_MIN = 0.08f;			// the backing's opacity in the dark
constexpr float BACK_MAX = 0.5f;			// and in daylight
constexpr int BACKING_POINTS = 32;			// points round a blob

static struct
{
	float brightness[BACKING__COUNT] = { 0.5f, 0.5f };
	float samples[BACKING__COUNT][MAX_BACKING_SPOTS] = {};
	int next = 0;							// the spot the next ray goes through, round both backings
	float lastPrint = 0.0f;
} s_back;

float BackingAlpha(Backing backing, C_NEO_Player *pPlayer, const Vector2D *pSpots, int spots, float dt, bool bBoot)
{
	spots = clamp(spots, 1, MAX_BACKING_SPOTS);
	float *samples = s_back.samples[backing];
	if (bBoot)
	{
		// All its spots at once, so it starts right.
		for (int i = 0; i < spots; ++i)
		{
			samples[i] = NeoHudSceneBrightness(pPlayer, pSpots[i]);
		}
	}
	else if (s_back.next % BACKING__COUNT == backing)
	{
		const int i = (s_back.next / BACKING__COUNT) % spots;
		samples[i] = NeoHudSceneBrightness(pPlayer, pSpots[i]);
	}
	if (backing == BACKING__COUNT - 1)
	{
		s_back.next = (s_back.next + 1) % (BACKING__COUNT * MAX_BACKING_SPOTS);
	}
	float &brightness = s_back.brightness[backing];
	NeoHudEaseBrightness(brightness, samples, spots, dt, bBoot);
	const float alpha = NeoHudBackingOpacity(brightness, BACK_MIN, BACK_MAX);
	if (cl_neo_hud_quickinfo_backing_debug.GetBool() && backing == BACKING_BAND && gpGlobals->realtime - s_back.lastPrint > 1.0f)
	{
		s_back.lastPrint = gpGlobals->realtime;
		Msg("[quickinfo] backing: band %.2f bright, speed graph %.2f bright; band opacity %.2f\n", s_back.brightness[BACKING_BAND],
			s_back.brightness[BACKING_SPEED], alpha);
	}
	return alpha;
}

void PaintBacking(const QuickFrame &f, Layer layer, const Vector2D &centre, const Vector2D &inner, const Vector2D &feather, float alpha)
{
	// The band's units map to the screen by one scale and offset (At), so the blob maps whole.
	NeoHudPaintBacking(At(f, layer, centre.x, centre.y), inner * f.s, feather * f.s, alpha, BACKING_POINTS);
}
} // namespace NeoQuickInfo
