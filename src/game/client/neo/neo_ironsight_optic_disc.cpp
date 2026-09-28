#include "cbase.h"
#include "neo_ironsight_optic_disc.h"
#include "neo_ironsight_lens.h"
#include "neo_ironsight_optic_gyro.h"
#include "neo_ironsight_optic.h"
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
// hidden on the gun, always gets its reticle drawn here, on the pane nearer the eye; so does "gyro" glass,
// its reticle kept level with the horizon (neo_ironsight_optic_gyro.cpp).
// (A glitchy pixelated view off the sights was tried and parked: branch optic-glitch.)
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

// The lens shape ("lens_circle", "lens_shape") in rings: centreAlpha inside the fade radius, easing to
// zero at the rim. The live view fills it (bLiveView); anything else (the reticle) uses the lens's UVs.
static void DrawLensShape(IMaterial *pMaterial, const NeoLensPane &pane, const CNEOWeaponInfo &data,
	float centreAlpha, float fadeStart, bool bLiveView)
{
	constexpr int RINGS = 8;
	constexpr int SEGMENTS = 32;
	const Vector &circle = data.m_vecIronOpticLensCircle;
	const float shape = data.m_flIronOpticLensShape;
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
	// A gyro reticle is laid out the same way, turned by the gyro's roll instead of the lens's.
	const bool bGyro = !bLiveView && data.m_bIronOpticGyro;
	float gyroCos = 1.0f, gyroSin = 0.0f;
	if (bGyro)
	{
		const float roll = NeoIronsightGyroRoll(data, atan2f(DotProduct(pane.u, lookUp), DotProduct(pane.u, lookRight)));
		gyroCos = cosf(roll);
		gyroSin = sinf(roll);
	}
	const auto alphaAt = [&](float fraction) {
		const float fade = NeoSmoothStep((fraction - fadeStart) / Max(1.0f - fadeStart, 0.001f));
		return static_cast<unsigned char>(255.0f * centreAlpha * (1.0f - fade));
	};

	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->Bind(pMaterial);
	IMesh *pMesh = pRenderContext->GetDynamicMesh();
	CMeshBuilder meshBuilder;
	meshBuilder.Begin(pMesh, MATERIAL_TRIANGLES, SEGMENTS * (2 * RINGS - 1));
	const auto vertex = [&](float fraction, int segment) {
		// A superellipse: round at shape 2, squarer above.
		const float angle = 2.0f * M_PI_F * segment / SEGMENTS;
		const float c = cosf(angle), s = sinf(angle);
		const float scale = fraction * powf(powf(fabsf(c), shape) + powf(fabsf(s), shape), -1.0f / shape);
		const float x = c * scale, y = s * scale;
		const float u = circle.x + circle.z * x;
		const float v = circle.y + data.m_flIronOpticLensRadiusV * y;
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
		else if (bGyro)
		{
			// Where this point sits on screen around the lens centre, turned back by the reticle's roll: the
			// point of the lens art shown here.
			const Vector ray = world - eye;
			const float depth = Max(DotProduct(ray, lookForward), 0.001f);
			const float screenX = (DotProduct(ray, lookRight) / depth) / tanRadius;
			const float screenY = (DotProduct(ray, lookUp) / depth) / tanRadius;
			texU = circle.x + circle.z * (gyroCos * screenX + gyroSin * screenY);
			texV = circle.y - data.m_flIronOpticLensRadiusV * (gyroCos * screenY - gyroSin * screenX);
		}
		// Lifted a hair toward the eye so it sits on the lens rather than in it.
		Vector lift = eye - world;
		VectorNormalize(lift);
		const Vector position = world + lift * 0.01f;
		meshBuilder.Color4ub(255, 255, 255, alphaAt(fraction));
		meshBuilder.TexCoord2f(0, texU, texV);
		meshBuilder.Position3fv(position.Base());
		meshBuilder.AdvanceVertex();
	};
	for (int seg = 0; seg < SEGMENTS; ++seg)
	{
		// Centre fan, then a quad (two triangles) per segment between each pair of rings.
		vertex(0.0f, seg);
		vertex(1.0f / RINGS, seg);
		vertex(1.0f / RINGS, seg + 1);
		for (int ring = 1; ring < RINGS; ++ring)
		{
			const float inner = static_cast<float>(ring) / RINGS, outer = static_cast<float>(ring + 1) / RINGS;
			vertex(inner, seg);
			vertex(outer, seg);
			vertex(outer, seg + 1);
			vertex(inner, seg);
			vertex(outer, seg + 1);
			vertex(inner, seg + 1);
		}
	}
	meshBuilder.End();
	pMesh->Draw();
}

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
		// Only with this frame's view of what lies behind the glass; clear to its edge, softened at the rim.
		state.bLiveView = state.bLiveView && NeoIronsightWindowReady();
		if (state.bLiveView)
		{
			state.fadeStart = 0.85f;
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
	// One-pane glass (hidden on the gun while ironsights apply) shows its reticle whatever happens to the
	// view behind it.
	state.bReticle = state.pReticle && (state.bLiveView
		|| ((data.m_bIronOpticOnePane || data.m_bIronOpticGyro) && NeoIronsightsActive(data)));
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
	DrawLensShape(s_debug, pane, data, 0.4f, 1.0f, false);
}

void NeoIronsightDrawOpticDisc(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float ironsightBlend, NeoIronsightLensPart part)
{
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
		DrawLensShape(LiveViewMaterial(), pane, data, state.centreAlpha, state.fadeStart, true);
	}
	if (bReticle)
	{
		DrawLensShape(state.pReticle, pane, data, state.bLiveView ? state.centreAlpha : 1.0f,
			state.bLiveView ? state.fadeStart : 1.0f, false);
	}
	if (cl_neo_ironsight_optic_debug.GetInt() >= 2 && part != NEO_LENS_VIEW)
	{
		DrawDebugLensShape(pane, data);
	}
}

//-----------------------------------------------------------------------------
// Seeing the gun through its glass: while cloaked or in thermals the whole gun, glass included, is drawn
// with one override material, so the glass can't be left out by material. Instead the clear view goes down
// first and the gun is drawn in slices, clipped just short of each pane: the gun behind the glass lands on
// the view, and the panes themselves (in the thin gaps) never draw. One pane: in front of it and behind it.
// Two panes: in front of the near one, the housing between them, and behind the far one.
//-----------------------------------------------------------------------------
static constexpr float GLASS_SPLIT_GAP = 0.02f;	// half the gap around each pane's plane, in viewmodel units

static void SetPlane(float plane[4], const Vector &normal, float dist)
{
	plane[0] = normal.x;
	plane[1] = normal.y;
	plane[2] = normal.z;
	plane[3] = dist;
}

bool NeoIronsightBeginGlassSplit(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	NeoIronsightGlassSplit &split)
{
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
	split = NeoIronsightGlassSplit();
	SetPlane(split.planes[0][0], -normal, -nearDist + GLASS_SPLIT_GAP);	// in front of the (near) pane
	split.planeCount[0] = 1;
	SetPlane(split.planes[1][0], normal, farDist + GLASS_SPLIT_GAP);	// behind the (far) pane
	split.planeCount[1] = 1;
	split.slices = 2;
	if (bTwoPanes)
	{
		// Between the panes: the sight housing, which frames the view through the near pane.
		SetPlane(split.planes[2][0], normal, nearDist + GLASS_SPLIT_GAP);
		SetPlane(split.planes[2][1], -normal, -farDist + GLASS_SPLIT_GAP);
		split.planeCount[2] = 2;
		split.slices = 3;
	}

	// The view once a frame: a second (translucent) pass of the gun must not cover what the first drew on it.
	static int s_viewFrame = -1;
	if (s_viewFrame != gpGlobals->framecount)
	{
		s_viewFrame = gpGlobals->framecount;
		DrawLensShape(LiveViewMaterial(), pane, data, state.centreAlpha, state.fadeStart, true);
	}
	return true;
}
