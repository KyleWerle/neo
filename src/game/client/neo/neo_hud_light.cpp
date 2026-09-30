#include "cbase.h"
#include "neo_hud_light.h"
#include "neo_hud_profile.h"
#include "neo_ghost_stroke.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "view.h"
#include "materialsystem/imaterialsystem.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_hud_backing("cl_neo_hud_backing", "1", FCVAR_ARCHIVE,
	"The HUD's dark backings (the racer band and speed graph, the cyberbrain's groups and ring), darker the brighter the"
	" scene behind them. Their strength (0 = none).", true, 0, true, 2);

namespace
{
constexpr float HUD_LIGHT_DIM = 0.2f, HUD_LIGHT_BRIGHT = 0.75f;	// the brightness a backing ramps over (0 black to 1 white)
constexpr float HUD_LIGHT_EASE = 0.4f;		// seconds: the brightness settles this slowly
constexpr float HUD_LIGHT_ALBEDO = 0.5f;	// what surfaces are taken to reflect (the light alone says nothing of it)
constexpr float HUD_LIGHT_OPEN = 0.85f;		// a ray that hits nothing (open air, far off)
constexpr float HUD_LIGHT_REACH = 4096.0f;	// units a ray goes
constexpr int HUD_BACKING_MAX_POINTS = 64;
}

bool NeoHudBackingsOn()
{
	return cl_neo_hud_backing.GetFloat() > 0.0f;
}

float NeoHudSceneBrightness(C_NEO_Player *pPlayer, const Vector2D &pixel)
{
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	const float halfFov = DEG2RAD(ScaleFOVByWidthRatio(pPlayer->GetFOV(), engine->GetScreenAspectRatio() * 0.75f)) * 0.5f;
	const float pixelsPerTangent = (wide * 0.5f) / tanf(halfFov);
	const Vector dir = MainViewForward() + MainViewRight() * ((pixel.x - wide * 0.5f) / pixelsPerTangent)
		- MainViewUp() * ((pixel.y - tall * 0.5f) / pixelsPerTangent);
	NeoHudCount(NEO_HUD_COUNT_RAYS);
	trace_t tr;
	UTIL_TraceLine(MainViewOrigin(), MainViewOrigin() + dir.Normalized() * HUD_LIGHT_REACH, MASK_OPAQUE, pPlayer,
		COLLISION_GROUP_NONE, &tr);
	if (tr.surface.flags & SURF_SKY)
	{
		return 1.0f;
	}
	if (tr.fraction >= 1.0f)
	{
		return HUD_LIGHT_OPEN;
	}
	const Vector light = engine->GetLightForPoint(tr.endpos + tr.plane.normal * 4.0f, true);
	const float luminance = 0.3f * light.x + 0.59f * light.y + 0.11f * light.z;
	CMatRenderContextPtr pRenderContext(materials);
	const float exposure = pRenderContext->GetToneMappingScaleLinear().x;	// 1 without HDR
	return sqrtf(clamp(HUD_LIGHT_ALBEDO * luminance * exposure, 0.0f, 1.0f));	// about the screen's gamma
}

void NeoHudEaseBrightness(float &brightness, const float *pSamples, int count, float dt, bool bBoot)
{
	float mean = 0.0f, most = 0.0f;
	for (int i = 0; i < count; ++i)
	{
		mean += pSamples[i];
		most = Max(most, pSamples[i]);
	}
	const float target = 0.5f * (mean / Max(count, 1)) + 0.5f * most;
	brightness = bBoot ? target : brightness + (target - brightness) * Min(1.0f, dt / HUD_LIGHT_EASE);
}

float NeoHudBackingOpacity(float brightness, float darkOpacity, float brightOpacity)
{
	return Lerp(NeoSmoothStep((brightness - HUD_LIGHT_DIM) / (HUD_LIGHT_BRIGHT - HUD_LIGHT_DIM)), darkOpacity, brightOpacity)
		* cl_neo_hud_backing.GetFloat();
}

// The unit superellipse round a backing, points round it, worked out once per count.
static const Vector2D *BackingShape(int points)
{
	static struct
	{
		int points = 0;
		Vector2D shape[HUD_BACKING_MAX_POINTS];
	} s_shapes[2];
	int slot = 0;
	for (; slot < static_cast<int>(ARRAYSIZE(s_shapes)); ++slot)
	{
		if (s_shapes[slot].points == points)
		{
			return s_shapes[slot].shape;
		}
		if (s_shapes[slot].points == 0)
		{
			break;
		}
	}
	slot = Min(slot, static_cast<int>(ARRAYSIZE(s_shapes)) - 1);
	for (int i = 0; i < points; ++i)
	{
		const float angle = 2.0f * M_PI_F * i / points, c = cosf(angle), s = sinf(angle);
		s_shapes[slot].shape[i].Init((c < 0.0f ? -1.0f : 1.0f) * sqrtf(fabsf(c)), (s < 0.0f ? -1.0f : 1.0f) * sqrtf(fabsf(s)));
	}
	s_shapes[slot].points = points;
	return s_shapes[slot].shape;
}

void NeoHudPaintBacking(const Vector2D &centre, const Vector2D &half, const Vector2D &feather, float alpha, int points)
{
	if (alpha <= 0.0f)
	{
		return;
	}
	points = clamp(points, 3, HUD_BACKING_MAX_POINTS);
	const Vector2D *pShape = BackingShape(points);
	Vector2D in[HUD_BACKING_MAX_POINTS], out[HUD_BACKING_MAX_POINTS];
	for (int i = 0; i < points; ++i)
	{
		const Vector2D &shape = pShape[i];
		in[i].Init(centre.x + shape.x * half.x, centre.y + shape.y * half.y);
		out[i].Init(centre.x + shape.x * (half.x + feather.x), centre.y + shape.y * (half.y + feather.y));
	}
	NeoGhostBegin(Color(0, 0, 0, 255), clamp(RoundFloatToInt(255.0f * alpha), 0, 255));
	static const float s_solid[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, s_edge[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	for (int i = 0; i < points; ++i)
	{
		const int j = (i + 1) % points;
		const Vector2D fan[4] = { centre, in[i], in[j], in[j] };
		NeoGhostFillShaded(fan, s_solid);
		const Vector2D edge[4] = { in[i], in[j], out[j], out[i] };
		NeoGhostFillShaded(edge, s_edge);
	}
}
