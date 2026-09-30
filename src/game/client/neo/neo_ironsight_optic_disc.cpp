#include "cbase.h"
#include "neo_ironsight_optic_disc.h"
#include "neo_ironsight_profile.h"
#include "neo_ironsight_lens.h"
#include "neo_ironsight_optic.h"
#include "neo_ironsight_collimator.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "view.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "materialsystem/MaterialSystemUtil.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_ironsight_optic_debug("cl_neo_ironsight_optic_debug", "0", 0,
	"Debug: 1 = print, once a second, how the glass drawing went for the gun in view; 2 = also draw the"
	" glass's outline in magenta over everything.");

static float s_flNextDebugPrint = 0.0f;

//-----------------------------------------------------------------------------
// The glass's art ("reticle"), drawn by us where the gun's own glass doesn't show it: glass hidden on the gun
// (one pane, a collimated dot), glass drawn over (cloak, thermals: the split below leaves it out of the gun),
// and a scope's lens on the sights (the hole below leaves it out).
// (Parked experiments: a glitchy pixelated view off the sights, branch optic-glitch; a gyro-levelled
// reticle, branch optic-gyro.)
//-----------------------------------------------------------------------------

// The lens outline around its centre, for a superellipse of this exponent: LENS_SEGMENTS points at radius
// 1 (round at 2, squarer above). Worked out once per exponent.
static constexpr int LENS_SEGMENTS = 32;
static constexpr int LENS_RINGS = 6;	// enough for the rim fade
static const Vector2D *LensOutline(float shape)
{
	static float s_shape = -1.0f;
	static Vector2D s_outline[LENS_SEGMENTS];
	if (shape != s_shape)
	{
		s_shape = shape;
		for (int i = 0; i < LENS_SEGMENTS; ++i)
		{
			const float angle = 2.0f * M_PI_F * i / LENS_SEGMENTS;
			const float c = cosf(angle), s = sinf(angle);
			const float scale = powf(powf(fabsf(c), shape) + powf(fabsf(s), shape), -1.0f / shape);
			s_outline[i].Init(c * scale, s * scale);
		}
	}
	return s_outline;
}

// An area of the lens surface in UV to draw over, as an outline around a centre, scaled: the lens itself
// ("lens_circle", "lens_shape") or the whole glass ("window_glass", else the lens), exactly: grown, it cut real
// parts of the gun out past the glass.
struct LensArea
{
	float centreU, centreV, scaleU, scaleV;
	const Vector2D *pOutline;
	int points;
};

static LensArea LensAreaOf(const CNEOWeaponInfo &data, bool bWholeGlass)
{
	if (bWholeGlass && data.m_iIronOpticWindowGlassPoints >= 3)
	{
		const Vector2D &centre = data.m_vecIronOpticWindowCentre;
		return { centre.x, centre.y, 1.0f, 1.0f, data.m_vecIronOpticWindowGlass, data.m_iIronOpticWindowGlassPoints };
	}
	const Vector &circle = data.m_vecIronOpticLensCircle;
	return { circle.x, circle.y, circle.z, data.m_flIronOpticLensRadiusV,
		LensOutline(data.m_flIronOpticLensShape), LENS_SEGMENTS };
}

// An area of the lens (LensAreaOf) in rings of shared vertices, textured with the lens's UVs: centreAlpha
// inside the fade radius, easing to zero at the rim.
static void DrawLensShape(IMaterial *pMaterial, const NeoLensPane &pane, const LensArea &area, float centreAlpha,
	float fadeStart)
{
	const Vector2D *pOutline = area.pOutline;
	const int segments = area.points;
	const Vector eye = CurrentViewOrigin();

	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->Bind(pMaterial);
	IMesh *pMesh = pRenderContext->GetDynamicMesh();
	CMeshBuilder meshBuilder;
	// The centre, then LENS_RINGS rings of the outline's points; a fan to the first ring, quads between rings.
	const int vertices = 1 + LENS_RINGS * segments;
	const int indices = segments * 3 + (LENS_RINGS - 1) * segments * 6;
	meshBuilder.Begin(pMesh, MATERIAL_TRIANGLES, vertices, indices);
	const auto vertex = [&](float x, float y, float fraction) {
		const float u = area.centreU + area.scaleU * x;
		const float v = area.centreV + area.scaleV * y;
		const Vector world = pane.At(u, v);
		// Lifted a hair toward the eye so it sits on the lens rather than in it.
		Vector lift = eye - world;
		VectorNormalize(lift);
		const Vector position = world + lift * 0.01f;
		const float fade = NeoSmoothStep((fraction - fadeStart) / Max(1.0f - fadeStart, 0.001f));
		meshBuilder.Color4ub(255, 255, 255, static_cast<unsigned char>(255.0f * centreAlpha * (1.0f - fade)));
		meshBuilder.TexCoord2f(0, u, v);
		meshBuilder.Position3fv(position.Base());
		meshBuilder.AdvanceVertex();
	};
	vertex(0.0f, 0.0f, 0.0f);
	for (int ring = 1; ring <= LENS_RINGS; ++ring)
	{
		const float fraction = static_cast<float>(ring) / LENS_RINGS;
		for (int seg = 0; seg < segments; ++seg)
		{
			vertex(pOutline[seg].x * fraction, pOutline[seg].y * fraction, fraction);
		}
	}
	// Vertex index of ring r (1-based), segment s (wrapping).
	const auto at = [segments](int ring, int seg) { return 1 + (ring - 1) * segments + (seg % segments); };
	for (int seg = 0; seg < segments; ++seg)
	{
		meshBuilder.FastIndex(0);
		meshBuilder.FastIndex(at(1, seg));
		meshBuilder.FastIndex(at(1, seg + 1));
		for (int ring = 1; ring < LENS_RINGS; ++ring)
		{
			meshBuilder.FastIndex(at(ring, seg));
			meshBuilder.FastIndex(at(ring + 1, seg));
			meshBuilder.FastIndex(at(ring + 1, seg + 1));
			meshBuilder.FastIndex(at(ring, seg));
			meshBuilder.FastIndex(at(ring + 1, seg + 1));
			meshBuilder.FastIndex(at(ring, seg + 1));
		}
	}
	meshBuilder.End();
	pMesh->Draw();
}

// "reticle_in_lens" art fades out from this fraction of the lens circle to its edge.
static constexpr float RETICLE_IN_LENS_FADE = 0.85f;

// What this frame's glass drawing does for the gun in view.
struct LensState
{
	IMaterial *pReticle = nullptr;
	bool bOverridden = false;		// drawn over (cloak, thermals): the split leaves the glass out of the gun
								// (an eyepiece only on the sights)
	bool bScopeOnSights = false;	// a scope on the sights: the hole leaves its lens out
	bool bReticle = false;
};

// The weapon's reticle material, or null; found by name only when the weapon changes.
static IMaterial *ReticleMaterial(const CNEOWeaponInfo &data)
{
	static const CNEOWeaponInfo *s_pData = nullptr;
	static IMaterial *s_pReticle = nullptr;
	if (&data != s_pData)
	{
		s_pData = &data;
		s_pReticle = data.m_szIronOpticReticle[0]
			? materials->FindMaterial(data.m_szIronOpticReticle, TEXTURE_GROUP_VGUI, false) : nullptr;
		if (s_pReticle && s_pReticle->IsErrorMaterial())
		{
			s_pReticle = nullptr;
		}
	}
	return s_pReticle;
}

static LensState GetLensState(const CNEOWeaponInfo &data, bool bCloaked, bool bThermal, float ironsightBlend)
{
	LensState state;
	state.pReticle = ReticleMaterial(data);
	const bool bActive = NeoIronsightsActive(data);
	state.bOverridden = (bCloaked || bThermal) && data.m_bIronOpticWindow && bActive
		&& (!data.m_bIronOpticEyepiece || ironsightBlend >= NEO_IRONSIGHT_ON_SIGHTS);
	state.bScopeOnSights = data.m_bIronOpticScope && !state.bOverridden && ironsightBlend >= NEO_IRONSIGHT_ON_SIGHTS
		&& bActive;
	// Glass hidden on the gun (one pane, a collimated dot) shows its art whatever else happens to it.
	const bool bHidden = (data.m_bIronOpticOnePane || data.m_bIronOpticCollimated) && bActive;
	state.bReticle = state.pReticle && (state.bOverridden || state.bScopeOnSights || bHidden);
	if (cl_neo_ironsight_optic_debug.GetBool() && gpGlobals->realtime >= s_flNextDebugPrint)
	{
		s_flNextDebugPrint = gpGlobals->realtime + 1.0f;
		Msg("[optic] %s: cloaked %d thermal %d drawn over %d scope on sights %d art %d%s\n", data.szClassName,
			bCloaked, bThermal, state.bOverridden, state.bScopeOnSights, state.bReticle,
			state.pReticle ? "" : " (no reticle material)");
	}
	return state;
}

static void DrawDebugLensShape(const NeoLensPane &pane, const CNEOWeaponInfo &data)
{
	static CMaterialReference s_debug;
	if (!s_debug.IsValid())
	{
		KeyValues *pVMT = new KeyValues("UnlitGeneric");
		pVMT->SetString("$basetexture", "white");
		pVMT->SetString("$color", "[1 0 1]");
		pVMT->SetInt("$translucent", 1);
		pVMT->SetInt("$vertexalpha", 1);
		pVMT->SetInt("$ignorez", 1);
		pVMT->SetInt("$nocull", 1);
		s_debug.Init("__neo_ironsight_optic_debug", TEXTURE_GROUP_OTHER, pVMT);
	}
	DrawLensShape(s_debug, pane, LensAreaOf(data, true), 0.4f, 1.0f);
}

void NeoIronsightDrawGlassArt(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float ironsightBlend)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_LENS, "NeoIronsightDrawGlassArt");
	if (!pViewModel || !data.m_bHasIronOpticLensMap)
	{
		return;
	}
	const LensState state = GetLensState(data, bCloaked, bThermal, ironsightBlend);
	NeoLensPane pane;
	if (!state.bReticle || !NeoIronsightLensPane(pViewModel, data, CurrentViewOrigin(), pane))
	{
		return;
	}
	// Its own art over the whole glass, as it is on the gun, unless its frame is dark and the gun is drawn over
	// (it then shows only in the clear part, past a soft edge around the lens circle).
	const bool bInLens = data.m_bIronOpticReticleInLens && state.bOverridden;
	const LensArea area = LensAreaOf(data, !bInLens);
	const float fadeStart = bInLens ? RETICLE_IN_LENS_FADE : 1.0f;
	Vector2D outline[LENS_SEGMENTS];
	const int points = Min(area.points, LENS_SEGMENTS);
	for (int i = 0; i < points; ++i)
	{
		outline[i].Init(area.centreU + area.scaleU * area.pOutline[i].x, area.centreV + area.scaleV * area.pOutline[i].y);
	}
	if (!data.m_bIronOpticCollimated || !NeoIronsightDrawCollimatedArt(pViewModel, state.pReticle, pane, data, outline,
		points, 1.0f, fadeStart))
	{
		DrawLensShape(state.pReticle, pane, area, 1.0f, fadeStart);
	}
	if (cl_neo_ironsight_optic_debug.GetInt() >= 2)
	{
		DrawDebugLensShape(pane, data);
	}
}

// Draws nothing itself: only its depth is written (see DrawDepthOnly).
static IMaterial *GlassDepthMaterial()
{
	static CMaterialReference s_material;
	if (!s_material.IsValid())
	{
		KeyValues *pVMT = new KeyValues("UnlitGeneric");
		pVMT->SetString("$basetexture", "white");
		pVMT->SetInt("$nocull", 1);
		s_material.Init("__neo_ironsight_glass_depth", TEXTURE_GROUP_OTHER, pVMT);
	}
	return s_material;
}

// An area of the glass into depth only, a hair in front of the glass, so the gun behind it (the glass included)
// fails the depth test there in every later draw.
static void DrawDepthOnly(const NeoLensPane &pane, const LensArea &area)
{
	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->OverrideColorWriteEnable(true, false);
	pRenderContext->OverrideAlphaWriteEnable(true, false);
	pRenderContext->OverrideDepthEnable(true, true);
	DrawLensShape(GlassDepthMaterial(), pane, area, 1.0f, 1.0f);
	pRenderContext->OverrideDepthEnable(false, true);
	pRenderContext->OverrideAlphaWriteEnable(false, true);
	pRenderContext->OverrideColorWriteEnable(false, true);
}

//-----------------------------------------------------------------------------
// Seeing through the gun's glass: its exact outline goes into depth a hair in front of it before the gun is
// drawn, so neither the glass nor anything of the gun behind it draws inside the outline, and the world already
// on screen shows through. Under the cloak or thermals the whole gun, glass included, is drawn with one override
// material, so this is the only way to leave the glass out; under it, sight parts see-through in their own
// material (plates, the housing's inside, the tube between two panes) would be solid there too. A scope on the
// sights needs the same for its housing. Depth only, so it goes down before each draw of the gun (a two-pass
// model is drawn twice a frame) at no harm.
// "window_skip": the gun further behind the glass than that (the MX-S's front sight, seen through its glass) is
// drawn first, clipped to beyond it, and the rest after the outline, clipped to this side, so each part draws once.
// (Until 2026-09-30 the gun was drawn in three or four slices, clipped just short of each pane: the outlines were
// then hand-fitted circles that didn't cover the glass exactly.)
//-----------------------------------------------------------------------------
ConVar cl_neo_ironsight_window_skip("cl_neo_ironsight_window_skip", "", FCVAR_NONE,
	"Tuning: how deep behind sight glass the gun is hidden while cloaked or in thermals (the weapon's"
	" \"window_skip\"), in viewmodel units; negative = all of it; empty = the weapon's own.");

// This frame's clear view, worked out once a frame (a two-pass model asks again for its translucent pass).
static struct
{
	int frame = -1;
	const CNEOWeaponInfo *pData = nullptr;
	bool bClear = false;
	NeoLensPane pane;
	NeoIronsightGlassClear clear;
} s_clearView;

static void SetPlane(float plane[4], const Vector &normal, float dist)
{
	plane[0] = normal.x;
	plane[1] = normal.y;
	plane[2] = normal.z;
	plane[3] = dist;
}

static bool ComputeGlassClear(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float ironsightBlend, NeoLensPane &pane, NeoIronsightGlassClear &clear)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_LENS, "NeoIronsightBeginGlassClear");
	const LensState state = GetLensState(data, bCloaked, bThermal, ironsightBlend);
	if (!pViewModel || !(state.bOverridden || state.bScopeOnSights))
	{
		return false;
	}
	// This frame's pose, before the gun sets it up itself, so the depth sits where the gun is drawn.
	pViewModel->SetupBones(nullptr, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime);
	NeoLensPane farPane;
	if (!NeoIronsightLensPane(pViewModel, data, CurrentViewOrigin(), pane, &farPane))
	{
		return false;
	}
	clear = NeoIronsightGlassClear();
	const char *pszTunedSkip = cl_neo_ironsight_window_skip.GetString();
	const float skip = pszTunedSkip[0] ? cl_neo_ironsight_window_skip.GetFloat() : data.m_flIronOpticWindowSkip;
	// Custom clip planes can't change mid-scene under fast clipping (depth problems): all of the gun behind the
	// glass stays hidden then.
	if (!state.bOverridden || skip < 0.0f || materials->UsingFastClipping())
	{
		return true;
	}
	// The glass plane, the normal pointing away from the eye; a plane (n, d) keeps the points with n.p >= d.
	Vector normal = CrossProduct(pane.u, pane.v);
	if (VectorNormalize(normal) <= 0.0f)
	{
		return true;
	}
	if (DotProduct(normal, CurrentViewOrigin() - pane.origin) > 0.0f)
	{
		normal = -normal;
	}
	// Measured from the far pane of two (they are parallel).
	const float glassDist = DotProduct(normal, data.m_bHasIronOpticLensMap2 ? farPane.origin : pane.origin);
	const float skipDist = Max(glassDist, DotProduct(normal, pane.origin)) + skip;
	clear.bFarFirst = true;
	SetPlane(clear.farPlane, normal, skipDist);
	SetPlane(clear.nearPlane, -normal, -skipDist);
	return true;
}

bool NeoIronsightBeginGlassClear(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float ironsightBlend, NeoIronsightGlassClear &clear)
{
	if (s_clearView.frame != gpGlobals->framecount || s_clearView.pData != &data)
	{
		s_clearView.frame = gpGlobals->framecount;
		s_clearView.pData = &data;
		s_clearView.bClear = ComputeGlassClear(pViewModel, data, bCloaked, bThermal, ironsightBlend, s_clearView.pane,
			s_clearView.clear);
	}
	clear = s_clearView.clear;
	return s_clearView.bClear;
}

void NeoIronsightDrawGlassClearDepth(const CNEOWeaponInfo &data)
{
	if (s_clearView.frame != gpGlobals->framecount || s_clearView.pData != &data || !s_clearView.bClear)
	{
		return;
	}
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_LENS, "NeoIronsightDrawGlassClearDepth");
	DrawDepthOnly(s_clearView.pane, LensAreaOf(data, true));
}
