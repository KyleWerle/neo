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

ConVar cl_neo_ironsight_optic_cloak_alpha("cl_neo_ironsight_optic_cloak_alpha", "0.9", FCVAR_ARCHIVE,
	"Opacity of the optic's centre while cloaked.", true, 0, true, 1);
ConVar cl_neo_ironsight_optic_cloak_fade("cl_neo_ironsight_optic_cloak_fade", "0.55", FCVAR_ARCHIVE,
	"While cloaked, where the optic starts fading out, as a fraction of the lens radius.", true, 0, true, 1);

ConVar cl_neo_ironsight_optic_debug("cl_neo_ironsight_optic_debug", "0", 0,
	"Debug: 1 = print, once a second, how the optic's lens drawing went for the gun in view; 2 = also draw the"
	" lens shape in magenta over everything.");

static float s_flNextDebugPrint = 0.0f;

//-----------------------------------------------------------------------------
// Drawing the lens: the live view, then the reticle, through vertex alpha. While cloaked the gun is drawn
// with the cloak override, which takes a lens's live view with it, so the optic is drawn again here,
// fading out towards the rim to blend into the cloaked gun; the thermal override does the same, and there
// the optic is drawn whole. Disc lenses ("lens_disc") are drawn this way cloaked or not, fading in over
// their own lens on the sights; sight glass ("window") only while cloaked or in thermals. "one_pane" glass,
// hidden on the gun, always gets its reticle drawn here, on the pane nearer the eye.
// (Parked experiments: a glitchy pixelated view off the sights, branch optic-glitch; a gyro-levelled
// reticle, branch optic-gyro.)
//-----------------------------------------------------------------------------
static IMaterial *LiveViewMaterial()
{
	static CMaterialReference s_material;
	if (!s_material.IsValid())
	{
		KeyValues *pVMT = new KeyValues("UnlitGeneric");
		pVMT->SetString("$basetexture", NeoIronsightOpticTexture()->GetName());
		pVMT->SetInt("$translucent", 1);
		pVMT->SetInt("$vertexcolor", 1);
		pVMT->SetInt("$vertexalpha", 1);
		pVMT->SetInt("$nocull", 1);
		// Wins against the gun surface it sits on (a depth bias, like bullet decals).
		pVMT->SetInt("$decal", 1);
		s_material.Init("__neo_ironsight_optic_disc", TEXTURE_GROUP_OTHER, pVMT);
	}
	return s_material;
}

// The lens outline around its centre, for a superellipse of this exponent: LENS_SEGMENTS points at radius
// 1 (round at 2, squarer above). Worked out once per exponent.
static constexpr int LENS_SEGMENTS = 32;
static constexpr int LENS_RINGS = 6;	// enough for the rim fade, and for the sight-glass mapping at an angle
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
// ("lens_circle", "lens_shape") or the whole glass ("window_glass", else the lens), grown by grow.
struct LensArea
{
	float centreU, centreV, scaleU, scaleV;
	const Vector2D *pOutline;
	int points;
};

static LensArea LensAreaOf(const CNEOWeaponInfo &data, bool bWholeGlass, float grow = 1.0f)
{
	if (bWholeGlass && data.m_iIronOpticWindowGlassPoints >= 3)
	{
		const Vector &centre = data.m_vecIronOpticWindowCircle;
		return { centre.x, centre.y, grow, grow, data.m_vecIronOpticWindowGlass, data.m_iIronOpticWindowGlassPoints };
	}
	const Vector &circle = data.m_vecIronOpticLensCircle;
	return { circle.x, circle.y, circle.z * grow, data.m_flIronOpticLensRadiusV * grow,
		LensOutline(data.m_flIronOpticLensShape), LENS_SEGMENTS };
}

// An area of the lens (LensAreaOf) in rings of shared vertices: centreAlpha inside the fade radius, easing to
// zero at the rim. The live view fills it (bLiveView); anything else (the reticle) uses the lens's UVs.
static void DrawLensShape(IMaterial *pMaterial, const NeoLensPane &pane, const CNEOWeaponInfo &data,
	const LensArea &area, float centreAlpha, float fadeStart, bool bLiveView)
{
	const Vector &circle = data.m_vecIronOpticLensCircle;
	const Vector2D *pOutline = area.pOutline;
	const int segments = area.points;
	const Vector eye = CurrentViewOrigin();

	// The live view of a lens is laid out as the lens is seen on screen, level with the eye: the lens's
	// apparent radius spans the picture's half-width around where its centre appears. The gun's roll (a cant,
	// a viewmodel-only lean) then turns the lens and its reticle, but never the world seen through it.
	const Vector lensCentre = pane.Centre(data);
	Vector lookForward = lensCentre - eye;
	const float lensDistance = VectorNormalize(lookForward);
	Vector lookRight = CrossProduct(lookForward, CurrentViewUp());
	VectorNormalize(lookRight);
	const Vector lookUp = CrossProduct(lookRight, lookForward);
	const float lensRadius = pane.u.Length() * circle.z;
	const float tanRadius = lensRadius / sqrtf(Max(lensDistance * lensDistance - lensRadius * lensRadius, 0.0001f));

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
		float texU = u, texV = v;
		if (bLiveView && data.m_bIronOpticWindow)
		{
			NeoIronsightWindowTexCoord(world, texU, texV);
		}
		else if (bLiveView)
		{
			const Vector ray = world - eye;
			const float depth = Max(DotProduct(ray, lookForward), 0.001f);
			texU = 0.5f + 0.5f * (DotProduct(ray, lookRight) / depth) / tanRadius;
			texV = 0.5f - 0.5f * (DotProduct(ray, lookUp) / depth) / tanRadius;
		}
		// Lifted a hair toward the eye so it sits on the lens rather than in it.
		Vector lift = eye - world;
		VectorNormalize(lift);
		const Vector position = world + lift * 0.01f;
		const float fade = NeoSmoothStep((fraction - fadeStart) / Max(1.0f - fadeStart, 0.001f));
		meshBuilder.Color4ub(255, 255, 255, static_cast<unsigned char>(255.0f * centreAlpha * (1.0f - fade)));
		meshBuilder.TexCoord2f(0, texU, texV);
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

// What this frame's lens drawing shows for the gun in view.
struct LensState
{
	IMaterial *pReticle = nullptr;
	bool bLiveView = false;
	bool bReticle = false;
	float centreAlpha = 1.0f;
	float fadeStart = 1.0f;
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
	// Whether the gun is drawn with an override material (cloak, thermals) that covers its own lens.
	const bool bOverridden = bCloaked || bThermal;
	state.bLiveView = (bOverridden || data.m_bIronOpticLensDisc) && NeoGetIronsightOpticMode() == NEO_OPTIC_PIP
		&& NeoIronsightOpticTexture();
	if (data.m_bIronOpticWindow)
	{
		// Only with this frame's view of what lies behind the glass; clear to its edge, softened past it (the
		// view reaches NEO_IRONSIGHT_WINDOW_GROW past the outline).
		state.bLiveView = state.bLiveView && NeoIronsightWindowReady();
		if (state.bLiveView)
		{
			state.fadeStart = 1.0f / NEO_IRONSIGHT_WINDOW_GROW;
		}
	}
	else if (state.bLiveView)
	{
		// Disc lenses show only their own lens at the hip, unless it is drawn over (cloak, thermals); the live
		// view fades in over the second half of aiming.
		float visibility = 1.0f;
		if (data.m_bIronOpticLensDisc && !bOverridden)
		{
			visibility = NeoSmoothStep((ironsightBlend - 0.5f) / 0.5f);
		}
		state.centreAlpha = visibility * (bCloaked ? cl_neo_ironsight_optic_cloak_alpha.GetFloat() : 1.0f);
		state.fadeStart = bCloaked ? cl_neo_ironsight_optic_cloak_fade.GetFloat() : 1.0f;
		state.bLiveView = state.centreAlpha > 0.0f;
	}
	// Glass hidden on the gun while ironsights apply (one pane, a collimated dot) shows its art whatever
	// happens to the view behind it.
	state.bReticle = state.pReticle && (state.bLiveView
		|| ((data.m_bIronOpticOnePane || data.m_bIronOpticCollimated) && NeoIronsightsActive(data)));
	if (cl_neo_ironsight_optic_debug.GetBool() && gpGlobals->realtime >= s_flNextDebugPrint)
	{
		s_flNextDebugPrint = gpGlobals->realtime + 1.0f;
		const char *pszReason = NeoIronsightWindowDebugReason();
		Msg("[optic] %s: cloaked %d thermal %d mode %d window %d (ready %d) live %d reticle %d%s%s %s\n",
			data.szClassName, bCloaked, bThermal, NeoGetIronsightOpticMode(), data.m_bIronOpticWindow,
			NeoIronsightWindowReady(), state.bLiveView, state.bReticle,
			state.pReticle ? "" : " (no reticle material)", pszReason[0] ? " -" : "", pszReason);
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
	DrawLensShape(s_debug, pane, data, LensAreaOf(data, data.m_bIronOpticWindow), 0.4f, 1.0f, false);
}

void NeoIronsightDrawOpticDisc(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float ironsightBlend, NeoIronsightLensPart part)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_LENS, "NeoIronsightDrawOpticDisc");
	if (!pViewModel || !data.m_bHasIronOpticLensMap)
	{
		return;
	}
	const LensState state = GetLensState(data, bCloaked, bThermal, ironsightBlend);
	const bool bView = state.bLiveView && part != NEO_LENS_RETICLE;
	const bool bReticle = state.bReticle && part != NEO_LENS_VIEW;
	NeoLensPane pane;
	if ((!bView && !bReticle) || !NeoIronsightLensPane(pViewModel, data, CurrentViewOrigin(), pane))
	{
		return;
	}
	if (bView)
	{
		DrawLensShape(LiveViewMaterial(), pane, data, LensAreaOf(data, data.m_bIronOpticWindow), state.centreAlpha,
			state.fadeStart, true);
	}
	if (bReticle)
	{
		// Sight glass: its own art over the whole glass, as it is on the gun, unless its frame is dark and the
		// clear view is up (it then shows clear past a soft edge around the lens circle).
		const bool bWholeGlass = data.m_bIronOpticWindow && !(data.m_bIronOpticReticleInLens && state.bLiveView);
		const bool bFaded = state.bLiveView && !bWholeGlass;
		const float fadeStart = data.m_bIronOpticReticleInLens ? RETICLE_IN_LENS_FADE : state.fadeStart;
		const LensArea area = LensAreaOf(data, bWholeGlass);
		Vector2D outline[LENS_SEGMENTS];
		const int points = Min(area.points, LENS_SEGMENTS);
		for (int i = 0; i < points; ++i)
		{
			outline[i].Init(area.centreU + area.scaleU * area.pOutline[i].x, area.centreV + area.scaleV * area.pOutline[i].y);
		}
		if (!data.m_bIronOpticCollimated || !NeoIronsightDrawCollimatedArt(pViewModel, state.pReticle, pane, data, outline,
			points, bFaded ? state.centreAlpha : 1.0f, bFaded ? fadeStart : 1.0f))
		{
			DrawLensShape(state.pReticle, pane, data, area, bFaded ? state.centreAlpha : 1.0f, bFaded ? fadeStart : 1.0f, false);
		}
	}
	if (cl_neo_ironsight_optic_debug.GetInt() >= 2 && part != NEO_LENS_VIEW)
	{
		DrawDebugLensShape(pane, data);
	}
}

//-----------------------------------------------------------------------------
// Seeing through the gun's glass: while cloaked or in thermals the whole gun, glass included, is drawn with
// one override material, so the glass can't be left out by material. Instead the clear view goes down first,
// on the pane nearer the eye, and the gun is drawn in slices, clipped just short of each pane, so the panes
// themselves (in the thin gaps) never draw. The gun far behind the glass ("window_skip") goes down next,
// over the view (the front of the gun, seen through the sight at the hip); then the view's outline goes into
// depth, so nothing nearer behind the glass shows inside it: under the override, sight parts see-through in
// their own material (plates, the housing's inside, the tube between two panes) would be solid there. One
// pane: behind it and in front of it. Two panes: behind the far one, the housing between them, and in front
// of the near one.
//-----------------------------------------------------------------------------
// Half the gap around each pane's plane, in viewmodel units. Every window sight's glass is flat to within
// 0.0005 of its lens map (art/optics/glass-flatness.py); anything else crossing the plane (the housing) loses
// a sliver this thin, which shimmers if it is wide enough to see.
static constexpr float GLASS_SPLIT_GAP = 0.002f;

ConVar cl_neo_ironsight_window_skip("cl_neo_ironsight_window_skip", "", FCVAR_NONE,
	"Tuning: how deep behind sight glass the gun is hidden while cloaked or in thermals (the weapon's"
	" \"window_skip\"), in viewmodel units; negative = all of it; empty = the weapon's own.");

// This frame's split, for drawing its view.
static struct
{
	int frame = -1;
	int drawnFrame = -1;
	int depthFrame = -1;
	const CNEOWeaponInfo *pData = nullptr;
	NeoLensPane pane;
	float centreAlpha = 1.0f;
	float fadeStart = 1.0f;
} s_glassView;

// Draws nothing itself: only its depth is written (see NeoIronsightDrawGlassDepth).
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

static void SetPlane(float plane[4], const Vector &normal, float dist)
{
	plane[0] = normal.x;
	plane[1] = normal.y;
	plane[2] = normal.z;
	plane[3] = dist;
}

static bool ComputeGlassSplit(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	NeoIronsightGlassSplit &split)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_LENS, "NeoIronsightBeginGlassSplit");
	// Custom clip planes can't change mid-scene under fast clipping (depth problems), so not then.
	if (!pViewModel || !data.m_bIronOpticWindow || !(bCloaked || bThermal) || materials->UsingFastClipping())
	{
		return false;
	}
	// This frame's pose, before the gun sets it up itself, so the view sits where the gun is drawn.
	pViewModel->SetupBones(nullptr, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime);
	const LensState state = GetLensState(data, bCloaked, bThermal, 1.0f);
	NeoLensPane pane, farPane;
	if (!state.bLiveView || !NeoIronsightLensPane(pViewModel, data, CurrentViewOrigin(), pane, &farPane))
	{
		return false;
	}
	// The glass planes, the normal pointing away from the eye; a plane (n, d) keeps the points with n.p >= d.
	Vector normal = CrossProduct(pane.u, pane.v);
	if (VectorNormalize(normal) <= 0.0f)
	{
		return false;
	}
	if (DotProduct(normal, CurrentViewOrigin() - pane.origin) > 0.0f)
	{
		normal = -normal;
	}
	const float nearDist = DotProduct(normal, pane.origin);
	// The panes are parallel; a second one closer than two gaps behind the first counts as the same plane.
	const float farDist = data.m_bHasIronOpticLensMap2 ? Max(DotProduct(normal, farPane.origin), nearDist) : nearDist;
	const bool bTwoPanes = farDist - nearDist > 4.0f * GLASS_SPLIT_GAP;
	const char *pszTunedSkip = cl_neo_ironsight_window_skip.GetString();
	const float skip = pszTunedSkip[0] ? cl_neo_ironsight_window_skip.GetFloat() : data.m_flIronOpticWindowSkip;
	split = NeoIronsightGlassSplit();
	const auto addSlice = [&split](int planeCount) { split.planeCount[split.slices] = planeCount; return split.slices++; };
	if (skip >= 0.0f)
	{
		// Far behind the (far) pane, over the view.
		SetPlane(split.planes[addSlice(1)][0], normal, farDist + GLASS_SPLIT_GAP + skip);
	}
	split.depthBefore = split.slices;
	// Behind the (far) pane, as far as that.
	const int behind = addSlice((skip >= 0.0f) ? 2 : 1);
	SetPlane(split.planes[behind][0], normal, farDist + GLASS_SPLIT_GAP);
	if (skip >= 0.0f)
	{
		SetPlane(split.planes[behind][1], -normal, -(farDist + GLASS_SPLIT_GAP + skip));
	}
	if (bTwoPanes)
	{
		// Between the panes: the sight's tube (hidden inside the glass's outline: drawn over the view, its walls
		// flicker at the glass's edge).
		const int slice = addSlice(2);
		SetPlane(split.planes[slice][0], normal, nearDist + GLASS_SPLIT_GAP);
		SetPlane(split.planes[slice][1], -normal, -farDist + GLASS_SPLIT_GAP);
	}
	// In front of the (near) pane.
	SetPlane(split.planes[addSlice(1)][0], -normal, -nearDist + GLASS_SPLIT_GAP);

	s_glassView.frame = gpGlobals->framecount;
	s_glassView.pData = &data;
	s_glassView.pane = pane;
	s_glassView.centreAlpha = state.centreAlpha;
	s_glassView.fadeStart = state.fadeStart;
	return true;
}

void NeoIronsightDrawGlassView(const CNEOWeaponInfo &data)
{
	// Once a frame: a later draw of the gun must not cover what an earlier one put in front of the glass.
	if (s_glassView.frame != gpGlobals->framecount || s_glassView.pData != &data || s_glassView.drawnFrame == gpGlobals->framecount)
	{
		return;
	}
	s_glassView.drawnFrame = gpGlobals->framecount;
	DrawLensShape(LiveViewMaterial(), s_glassView.pane, data, LensAreaOf(data, true, NEO_IRONSIGHT_WINDOW_GROW),
		s_glassView.centreAlpha, s_glassView.fadeStart, true);
}

void NeoIronsightDrawGlassDepth(const CNEOWeaponInfo &data)
{
	// Its outline into depth only, so the gun behind the glass fails the depth test there in every later draw.
	if (s_glassView.frame != gpGlobals->framecount || s_glassView.pData != &data || s_glassView.depthFrame == gpGlobals->framecount)
	{
		return;
	}
	s_glassView.depthFrame = gpGlobals->framecount;
	const LensArea area = LensAreaOf(data, true, NEO_IRONSIGHT_WINDOW_GROW);
	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->OverrideColorWriteEnable(true, false);
	pRenderContext->OverrideAlphaWriteEnable(true, false);
	pRenderContext->OverrideDepthEnable(true, true);
	DrawLensShape(GlassDepthMaterial(), s_glassView.pane, data, area, 1.0f, 1.0f, false);
	pRenderContext->OverrideDepthEnable(false, true);
	pRenderContext->OverrideAlphaWriteEnable(false, true);
	pRenderContext->OverrideColorWriteEnable(false, true);
}

bool NeoIronsightBeginGlassSplit(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	NeoIronsightGlassSplit &split)
{
	// Worked out once a frame: a two-pass model asks again for its translucent pass.
	static struct
	{
		int frame = -1;
		const CNEOWeaponInfo *pData = nullptr;
		bool bSplit = false;
		NeoIronsightGlassSplit split;
	} s_cache;
	if (s_cache.frame != gpGlobals->framecount || s_cache.pData != &data)
	{
		s_cache.frame = gpGlobals->framecount;
		s_cache.pData = &data;
		s_cache.bSplit = ComputeGlassSplit(pViewModel, data, bCloaked, bThermal, s_cache.split);
	}
	split = s_cache.split;
	return s_cache.bSplit;
}
