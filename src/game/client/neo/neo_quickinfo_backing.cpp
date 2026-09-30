#include "cbase.h"
#include "neo_quickinfo_internal.h"
#include "c_neo_player.h"
#include "view.h"
#include "materialsystem/imaterialsystem.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The dark backings (QUICKINFO.md): a feathered blob behind the band, another behind the speed graph, so the linework
// reads on bright maps. How dark follows how bright the scene is behind each: a few rays out through the blob's
// spots on screen, the world's light where they land (sky counts as bright), times the view's auto exposure on HDR
// maps. One ray a frame, round the spots, eased; nothing in the dark, up to BACK_MAX in daylight.

ConVar cl_neo_hud_backing("cl_neo_hud_backing", "1", FCVAR_ARCHIVE,
	"The HUD's dark backings (the racer band and speed graph, the cyberbrain's groups and ring), darker the brighter the"
	" scene behind them. Their strength (0 = none).", true, 0, true, 2);
ConVar cl_neo_hud_quickinfo_backing_debug("cl_neo_hud_quickinfo_backing_debug", "0", FCVAR_NONE,
	"Debug: print each backing's measured brightness (0 dark to 1 bright) and opacity once a second.");

namespace NeoQuickInfo
{
constexpr float BACK_MIN = 0.08f;			// the backing's opacity in the dark
constexpr float BACK_MAX = 0.5f;			// and in daylight
constexpr float DIM = 0.2f, BRIGHT = 0.75f;	// the brightness range it ramps over (0 black to 1 white on screen)
constexpr float ALBEDO = 0.5f;				// what the surfaces are taken to reflect (the light alone says nothing of it)
constexpr float OPEN = 0.85f;				// a ray that hits nothing (open air, far off)
constexpr float EASE = 0.4f;				// seconds: the brightness settles this slowly
constexpr float REACH = 4096.0f;			// units a ray goes
constexpr int RING = 32;					// points round a blob

static struct
{
	float brightness[BACKING__COUNT] = { 0.5f, 0.5f };
	float samples[BACKING__COUNT][MAX_BACKING_SPOTS] = {};
	int next = 0;							// the spot the next ray goes through, round both backings
	float lastPrint = 0.0f;
} s_back;

// How bright the scene looks through a screen pixel (0 black, 1 white): the world's light where a ray lands.
static float Sample(C_NEO_Player *pPlayer, const Vector2D &pixel, int wide, int tall, float pixelsPerTangent)
{
	const Vector dir = MainViewForward() + MainViewRight() * ((pixel.x - wide * 0.5f) / pixelsPerTangent)
		- MainViewUp() * ((pixel.y - tall * 0.5f) / pixelsPerTangent);
	trace_t tr;
	UTIL_TraceLine(MainViewOrigin(), MainViewOrigin() + dir.Normalized() * REACH, MASK_OPAQUE, pPlayer, COLLISION_GROUP_NONE, &tr);
	if (tr.surface.flags & SURF_SKY)
	{
		return 1.0f;
	}
	if (tr.fraction >= 1.0f)
	{
		return OPEN;
	}
	const Vector light = engine->GetLightForPoint(tr.endpos + tr.plane.normal * 4.0f, true);
	const float luminance = 0.3f * light.x + 0.59f * light.y + 0.11f * light.z;
	CMatRenderContextPtr pRenderContext(materials);
	const float exposure = pRenderContext->GetToneMappingScaleLinear().x;	// 1 without HDR
	return sqrtf(clamp(ALBEDO * luminance * exposure, 0.0f, 1.0f));	// about the screen's gamma
}

float SceneBrightness(C_NEO_Player *pPlayer, const Vector2D &pixel)
{
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	const float halfFov = DEG2RAD(ScaleFOVByWidthRatio(pPlayer->GetFOV(), engine->GetScreenAspectRatio() * 0.75f)) * 0.5f;
	return Sample(pPlayer, pixel, wide, tall, (wide * 0.5f) / tanf(halfFov));
}

float BackingAlpha(Backing backing, C_NEO_Player *pPlayer, const Vector2D *pSpots, int spots, float dt, bool bBoot)
{
	spots = clamp(spots, 1, MAX_BACKING_SPOTS);
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	const float halfFov = DEG2RAD(ScaleFOVByWidthRatio(pPlayer->GetFOV(), engine->GetScreenAspectRatio() * 0.75f)) * 0.5f;
	const float pixelsPerTangent = (wide * 0.5f) / tanf(halfFov);
	float *samples = s_back.samples[backing];
	if (bBoot)
	{
		// All its spots at once, so it starts right.
		for (int i = 0; i < spots; ++i)
		{
			samples[i] = Sample(pPlayer, pSpots[i], wide, tall, pixelsPerTangent);
		}
	}
	else if (s_back.next % BACKING__COUNT == backing)
	{
		const int i = (s_back.next / BACKING__COUNT) % spots;
		samples[i] = Sample(pPlayer, pSpots[i], wide, tall, pixelsPerTangent);
	}
	if (backing == BACKING__COUNT - 1)
	{
		s_back.next = (s_back.next + 1) % (BACKING__COUNT * MAX_BACKING_SPOTS);
	}
	// The bright spots count most: a patch of sky behind half the band still needs the backing.
	float mean = 0.0f, most = 0.0f;
	for (int i = 0; i < spots; ++i)
	{
		mean += samples[i];
		most = Max(most, samples[i]);
	}
	const float target = 0.5f * (mean / spots) + 0.5f * most;
	float &brightness = s_back.brightness[backing];
	brightness = bBoot ? target : brightness + (target - brightness) * Min(1.0f, dt / EASE);
	const float alpha = Lerp(NeoSmoothStep((brightness - DIM) / (BRIGHT - DIM)), BACK_MIN, BACK_MAX)
		* cl_neo_hud_backing.GetFloat();
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
	if (alpha <= 0.0f)
	{
		return;
	}
	// A rounded rectangle (a superellipse), solid inside, fading to nothing over the feather.
	Vector2D in[RING], out[RING];
	for (int i = 0; i < RING; ++i)
	{
		const float angle = 2.0f * M_PI_F * i / RING, c = cosf(angle), s = sinf(angle);
		const Vector2D shape((c < 0.0f ? -1.0f : 1.0f) * sqrtf(fabsf(c)), (s < 0.0f ? -1.0f : 1.0f) * sqrtf(fabsf(s)));
		in[i] = At(f, layer, centre.x + shape.x * inner.x, centre.y + shape.y * inner.y);
		out[i] = At(f, layer, centre.x + shape.x * (inner.x + feather.x), centre.y + shape.y * (inner.y + feather.y));
	}
	const Vector2D middle = At(f, layer, centre.x, centre.y);
	NeoGhostBegin(Color(0, 0, 0, 255), clamp(RoundFloatToInt(255.0f * alpha), 0, 255));
	static const float s_solid[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, s_edge[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	for (int i = 0; i < RING; ++i)
	{
		const int j = (i + 1) % RING;
		const Vector2D fan[4] = { middle, in[i], in[j], in[j] };
		NeoGhostFillShaded(fan, s_solid);
		const Vector2D ring[4] = { in[i], in[j], out[j], out[i] };
		NeoGhostFillShaded(ring, s_edge);
	}
}
} // namespace NeoQuickInfo
