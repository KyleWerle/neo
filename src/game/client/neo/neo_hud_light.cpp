#include "cbase.h"
#include "neo_hud_light.h"
#include "neo_hud_profile.h"
#include "neo_ghost_stroke.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "view.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/itexture.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_hud_backing("cl_neo_hud_backing", "1", FCVAR_ARCHIVE,
	"The HUD's dark backings (the racer band and speed graph, the cyberbrain's groups and ring), darker the brighter the"
	" scene behind them. Their strength (0 = none).", true, 0, true, 2);
ConVar cl_neo_hud_backing_gpu("cl_neo_hud_backing_gpu", "1", FCVAR_ARCHIVE,
	"The HUD's backings drawn by a shader over a copy of the frame: the scene behind blurred, its bright pixels pulled"
	" down, with a glitching noise in it (0 = the flat dark blobs).", true, 0, true, 1);
ConVar cl_neo_hud_backing_glitch("cl_neo_hud_backing_glitch", "1", FCVAR_ARCHIVE,
	"How much the GPU backings glitch: dither, drifting cloud, scanlines, torn rows (0 = a clean blur).", true, 0, true, 2);
ConVar cl_neo_hud_backing_blur("cl_neo_hud_backing_blur", "1", FCVAR_ARCHIVE,
	"How wide the GPU backings' blur reaches (0 = barely).", true, 0, true, 2);
ConVar cl_neo_hud_backing_target("cl_neo_hud_backing_target", "0.3", FCVAR_ARCHIVE,
	"The brightness the GPU backings pull bright pixels down toward (0 black to 1 white).", true, 0, true, 1);

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
	if (NeoHudBackingGpu())
	{
		darkOpacity = 0.8f;
		brightOpacity = 1.0f;
	}
	return Lerp(NeoSmoothStep((brightness - HUD_LIGHT_DIM) / (HUD_LIGHT_BRIGHT - HUD_LIGHT_DIM)), darkOpacity, brightOpacity)
		* cl_neo_hud_backing.GetFloat();
}

// The unit superellipse round a backing, points round it, worked out once per count.
const Vector2D *NeoHudBackingShape(int points)
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

static void PaintBlob(const Vector2D &centre, const Vector2D &half, const Vector2D &feather, float alpha, int points)
{
	if (alpha <= 0.0f)
	{
		return;
	}
	points = clamp(points, 3, HUD_BACKING_MAX_POINTS);
	const Vector2D *pShape = NeoHudBackingShape(points);
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

static IMaterial *BackingMaterial()
{
	static CMaterialReference s_material;
	if (!s_material.IsValid())
	{
		KeyValues *pVMT = new KeyValues("Neo_HudBacking");
		pVMT->SetString("$basetexture", "_rt_SmallFB0");
		s_material.Init("__neo_hud_backing", pVMT);
	}
	return s_material;
}

bool NeoHudBackingGpu()
{
	if (!cl_neo_hud_backing_gpu.GetBool() || !g_pMaterialSystemHardwareConfig->SupportsShaderModel_3_0())
	{
		return false;
	}
	// A shader missing from the build (no .vcs deployed) comes back as another shader: the blobs then.
	IMaterial *pMaterial = BackingMaterial();
	return !IsErrorMaterial(pMaterial) && V_stricmp(pMaterial->GetShaderName(), "Neo_HudBacking") == 0;
}

// The quarter-size copy of the frame, once a frame. _rt_SmallFB0 is scratch by the time the HUD paints (bloom and the
// depth of field fill it before), as the pyro depth of field uses it.
static void CopyFrame()
{
	static int s_iFrame = -1;
	if (s_iFrame == gpGlobals->framecount)
	{
		return;
	}
	s_iFrame = gpGlobals->framecount;
	static CTextureReference s_copy;
	if (!s_copy.IsValid())
	{
		s_copy.Init(materials->FindTexture("_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET));
	}
	Rect_t dest = { 0, 0, s_copy->GetActualWidth(), s_copy->GetActualHeight() };
	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->CopyRenderTargetToTextureEx(s_copy, 0, nullptr, &dest);
}

void NeoHudPaintBacking(const Vector2D &centre, const Vector2D &half, const Vector2D &feather, float alpha, int points,
	const NeoHudBackingLook &look)
{
	if (alpha <= 0.0f)
	{
		return;
	}
	if (!NeoHudBackingGpu())
	{
		PaintBlob(centre, half, feather, alpha, points);
		return;
	}
	NeoGhostFlush();	// what's queued draws under the backing, as with the blobs
	CopyFrame();
	points = clamp(points, 3, HUD_BACKING_MAX_POINTS);
	const Vector2D *pShape = NeoHudBackingShape(points);
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	const float toU = 1.0f / Max(wide, 1), toV = 1.0f / Max(tall, 1);
	const float time = fmodf(gpGlobals->realtime, 1000.0f);
	const unsigned char target = clamp(RoundFloatToInt(255.0f * cl_neo_hud_backing_target.GetFloat()), 0, 255);
	const unsigned char glitch = clamp(RoundFloatToInt(255.0f * look.glitch * cl_neo_hud_backing_glitch.GetFloat()), 0, 255);
	const unsigned char blur = clamp(RoundFloatToInt(255.0f * look.blur * cl_neo_hud_backing_blur.GetFloat() * 0.5f), 0, 255);
	const unsigned char solid = clamp(RoundFloatToInt(255.0f * alpha), 0, 255);

	CMatRenderContextPtr pRenderContext(materials);
	IMesh *pMesh = pRenderContext->GetDynamicMesh(true, nullptr, nullptr, BackingMaterial());
	CMeshBuilder meshBuilder;
	meshBuilder.Begin(pMesh, MATERIAL_QUADS, points * 2);
	auto vertex = [&](const Vector2D &at, unsigned char a)
	{
		meshBuilder.Color4ub(target, glitch, blur, a);
		meshBuilder.TexCoord2f(0, at.x * toU, at.y * toV);
		meshBuilder.TexCoord2f(1, time, look.seed);
		meshBuilder.Position3f(at.x, at.y, 0.0f);
		meshBuilder.AdvanceVertex();
	};
	for (int i = 0; i < points; ++i)
	{
		const int j = (i + 1) % points;
		const Vector2D inI(centre.x + pShape[i].x * half.x, centre.y + pShape[i].y * half.y);
		const Vector2D inJ(centre.x + pShape[j].x * half.x, centre.y + pShape[j].y * half.y);
		const Vector2D outI(centre.x + pShape[i].x * (half.x + feather.x), centre.y + pShape[i].y * (half.y + feather.y));
		const Vector2D outJ(centre.x + pShape[j].x * (half.x + feather.x), centre.y + pShape[j].y * (half.y + feather.y));
		vertex(centre, solid); vertex(inI, solid); vertex(inJ, solid); vertex(inJ, solid);
		vertex(inI, solid); vertex(inJ, solid); vertex(outJ, 0); vertex(outI, 0);
	}
	meshBuilder.End();
	pMesh->Draw();
	NeoHudCount(NEO_HUD_COUNT_MESHES);
	NeoHudCount(NEO_HUD_COUNT_QUADS, points * 2);
}

void NeoHudPaintBacking(const Vector2D &centre, const Vector2D &half, const Vector2D &feather, float alpha, int points)
{
	NeoHudPaintBacking(centre, half, feather, alpha, points, NeoHudBackingLook());
}
