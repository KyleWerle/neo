#include "cbase.h"
#include "neo_ironsight_optic_disc.h"
#include "neo_ironsight_optic.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "view.h"
#include "view_shared.h"
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

// Why the lens drawing stopped this frame, for cl_neo_ironsight_optic_debug.
static const char *s_pszDebugReason = "";
static float s_flNextDebugPrint = 0.0f;

// One pane of the lens in world space: point(u, v) = origin + u * uAxis + v * vAxis, in lens UV.
struct LensPane
{
	Vector origin, u, v;
};

// The lens pane nearer the eye ("lens_map" or "lens_map2"), from the viewmodel's current bones, and the
// other pane in pFarPane if there are two (else it is left alone).
static bool GetLensPane(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, const Vector &eye, LensPane &pane,
	LensPane *pFarPane = nullptr)
{
	const int bone = pViewModel->LookupBone(data.m_szIronOpticLensBone);
	if (bone < 0)
	{
		return false;
	}
	matrix3x4_t lensToWorld;
	// From the drawn pose, not GetBoneTransform: its cache holds only hitbox bones, and lens bones that are not
	// (the MX-S's sight_glass) would come back as the viewmodel's origin.
	MatrixCopy(pViewModel->GetBone(bone), lensToWorld);
	const auto toWorld = [&](const Vector &origin, const Vector &u, const Vector &v, LensPane &out) {
		VectorTransform(origin, lensToWorld, out.origin);
		VectorRotate(u, lensToWorld, out.u);
		VectorRotate(v, lensToWorld, out.v);
	};
	toWorld(data.m_vecIronOpticLensOrigin, data.m_vecIronOpticLensU, data.m_vecIronOpticLensV, pane);
	if (data.m_bHasIronOpticLensMap2)
	{
		LensPane second;
		toWorld(data.m_vecIronOpticLens2Origin, data.m_vecIronOpticLens2U, data.m_vecIronOpticLens2V, second);
		const Vector &circle = data.m_vecIronOpticLensCircle;
		const auto centre = [&](const LensPane &p) { return p.origin + p.u * circle.x + p.v * circle.y; };
		if (centre(second).DistToSqr(eye) < centre(pane).DistToSqr(eye))
		{
			V_swap(pane, second);
		}
		if (pFarPane)
		{
			*pFarPane = second;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Sight glass ("window"): the optic camera looks from the eye at the glass, and each point of the glass
// shows the spot of that view lying behind it, so the glass reads as clear however it sits on screen.
//-----------------------------------------------------------------------------
static struct
{
	bool valid = false;
	int frame = -1;
	matrix3x4_t worldToEye;
	matrix3x4_t eyeToCamera;	// rotation only
	float fovScale = 1.0f;		// the viewmodel's field of view to the world's
	float tanHalf = 1.0f;		// the optic camera's half field of view
} s_window;

// Where the world behind a point of the glass lies in the optic view, in texture coordinates.
static void WindowTexCoord(const Vector &world, float &u, float &v)
{
	Vector eye;
	VectorTransform(world, s_window.worldToEye, eye);
	const Vector ray(eye.x, eye.y * s_window.fovScale, eye.z * s_window.fovScale);
	Vector camera;
	VectorRotate(ray, s_window.eyeToCamera, camera);
	const float forward = Max(camera.x, 0.001f);
	u = 0.5f - 0.5f * (camera.y / forward) / s_window.tanHalf;
	v = 0.5f - 0.5f * (camera.z / forward) / s_window.tanHalf;
}

bool NeoIronsightWindowCamera(const CViewSetup &mainView, const CNEOWeaponInfo &data, QAngle &angles, float &fov)
{
	s_window.valid = false;
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	C_BaseAnimating *pViewModel = pPlayer ? pPlayer->GetNEOViewModel() : nullptr;
	LensPane pane;
	if (!pViewModel || !GetLensPane(pViewModel, data, mainView.origin, pane))
	{
		s_pszDebugReason = pViewModel ? "window camera: lens bone not found" : "window camera: no viewmodel";
		return false;
	}

	matrix3x4_t eyeToWorld;
	AngleMatrix(mainView.angles, mainView.origin, eyeToWorld);
	MatrixInvert(eyeToWorld, s_window.worldToEye);
	// The viewmodel is drawn with its own field of view: a point of the glass appears on screen where the
	// world along this scaled ray does.
	s_window.fovScale = tanf(DEG2RAD(mainView.fov * 0.5f)) / tanf(DEG2RAD(mainView.fovViewmodel * 0.5f));

	// The rays through the corners of the glass's bounding box, in eye space.
	const Vector &circle = data.m_vecIronOpticLensCircle;
	Vector rays[4];
	Vector middle(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < 4; ++i)
	{
		const float u = circle.x + ((i & 1) ? circle.z : -circle.z);
		const float v = circle.y + ((i & 2) ? data.m_flIronOpticLensRadiusV : -data.m_flIronOpticLensRadiusV);
		Vector eye;
		VectorTransform(pane.origin + pane.u * u + pane.v * v, s_window.worldToEye, eye);
		if (eye.x <= 0.1f)
		{
			s_pszDebugReason = "window camera: a glass corner is behind the eye";
			return false;
		}
		rays[i].Init(eye.x, eye.y * s_window.fovScale, eye.z * s_window.fovScale);
		middle += rays[i] / rays[i].Length();
	}

	// Look at the middle of the glass, upright like the eye, just wide enough to cover it.
	QAngle cameraInEye;
	VectorAngles(middle, Vector(0.0f, 0.0f, 1.0f), cameraInEye);
	matrix3x4_t cameraToEye, cameraToWorld;
	AngleMatrix(cameraInEye, cameraToEye);
	MatrixInvert(cameraToEye, s_window.eyeToCamera);
	ConcatTransforms(eyeToWorld, cameraToEye, cameraToWorld);
	MatrixAngles(cameraToWorld, angles);

	float tanHalf = 0.0f;
	for (const Vector &ray : rays)
	{
		Vector camera;
		VectorRotate(ray, s_window.eyeToCamera, camera);
		tanHalf = Max(tanHalf, Max(fabsf(camera.y), fabsf(camera.z)) / Max(camera.x, 0.001f));
	}
	s_window.tanHalf = tanHalf * 1.05f + 0.001f;
	fov = RAD2DEG(2.0f * atanf(s_window.tanHalf));
	s_window.valid = true;
	s_window.frame = gpGlobals->framecount;
	s_pszDebugReason = "";
	return true;
}

//-----------------------------------------------------------------------------
// Drawing the lens: the live view, then the reticle, through vertex alpha. While cloaked the gun is drawn
// with the cloak override, which takes a lens's live view with it, so the optic is drawn again here,
// fading out towards the rim to blend into the cloaked gun; the thermal override does the same, and there
// the optic is drawn whole. Disc lenses ("lens_disc") are drawn this way cloaked or not, fading in over
// their own lens on the sights; sight glass ("window") only while cloaked or in thermals. "one_pane" glass,
// hidden on the gun, always gets its reticle drawn here, on the pane nearer the eye.
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
		s_material.Init("__neo_ironsight_optic_disc", TEXTURE_GROUP_OTHER, pVMT);
	}
	return s_material;
}

// The lens shape ("lens_circle", "lens_shape") in rings: centreAlpha inside the fade radius, easing to
// zero at the rim. The live view fills it (bLiveView); anything else (the reticle) uses the lens's UVs.
static void DrawLensShape(IMaterial *pMaterial, const LensPane &pane, const CNEOWeaponInfo &data,
	float centreAlpha, float fadeStart, bool bLiveView)
{
	constexpr int RINGS = 8;
	constexpr int SEGMENTS = 32;
	const Vector &circle = data.m_vecIronOpticLensCircle;
	const float shape = data.m_flIronOpticLensShape;
	const Vector eye = CurrentViewOrigin();
	const auto alphaAt = [&](float fraction) {
		const float t = clamp((fraction - fadeStart) / Max(1.0f - fadeStart, 0.001f), 0.0f, 1.0f);
		return static_cast<unsigned char>(255.0f * centreAlpha * (1.0f - t * t * (3.0f - 2.0f * t)));
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
		const Vector world = pane.origin + pane.u * u + pane.v * v;
		float texU = u, texV = v;
		if (bLiveView && data.m_bIronOpticWindow)
		{
			WindowTexCoord(world, texU, texV);
		}
		else if (bLiveView)
		{
			texU = 0.5f + 0.5f * x;
			texV = 0.5f + 0.5f * y;
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

static LensState GetLensState(const CNEOWeaponInfo &data, bool bCloaked, bool bThermal, float ironsightBlend)
{
	LensState state;
	if (data.m_szIronOpticReticle[0])
	{
		state.pReticle = materials->FindMaterial(data.m_szIronOpticReticle, TEXTURE_GROUP_VGUI, false);
		if (state.pReticle && state.pReticle->IsErrorMaterial())
		{
			state.pReticle = nullptr;
		}
	}
	// Whether the gun is drawn with an override material (cloak, thermals) that covers its own lens.
	const bool bOverridden = bCloaked || bThermal;
	state.bLiveView = (bOverridden || data.m_bIronOpticLensDisc) && NeoGetIronsightOpticMode() == NEO_OPTIC_PIP
		&& NeoIronsightOpticTexture();
	if (data.m_bIronOpticWindow)
	{
		// Only with this frame's view of what lies behind the glass; clear to its edge, softened at the rim.
		state.bLiveView = state.bLiveView && s_window.valid && s_window.frame == gpGlobals->framecount;
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
			const float t = clamp((ironsightBlend - 0.5f) / 0.5f, 0.0f, 1.0f);
			visibility = t * t * (3.0f - 2.0f * t);
		}
		state.centreAlpha = visibility * (bCloaked ? cl_neo_ironsight_optic_cloak_alpha.GetFloat() : 1.0f);
		state.fadeStart = bCloaked ? cl_neo_ironsight_optic_cloak_fade.GetFloat() : 1.0f;
		state.bLiveView = state.centreAlpha > 0.0f;
	}
	// One-pane glass (hidden on the gun while ironsights apply) shows its reticle whatever happens to the
	// view behind it.
	state.bReticle = state.pReticle && (state.bLiveView || (data.m_bIronOpticOnePane && NeoIronsightsActive(data)));
	if (cl_neo_ironsight_optic_debug.GetBool() && gpGlobals->realtime >= s_flNextDebugPrint)
	{
		s_flNextDebugPrint = gpGlobals->realtime + 1.0f;
		Msg("[optic] %s: cloaked %d thermal %d mode %d window %d (valid %d, frame %d/%d) live %d reticle %d%s%s %s\n",
			data.szClassName, bCloaked, bThermal, NeoGetIronsightOpticMode(), data.m_bIronOpticWindow, s_window.valid,
			s_window.frame, gpGlobals->framecount, state.bLiveView, state.bReticle,
			state.pReticle ? "" : " (no reticle material)", s_pszDebugReason[0] ? " -" : "", s_pszDebugReason);
	}
	return state;
}

static void DrawDebugLensShape(const LensPane &pane, const CNEOWeaponInfo &data)
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
	LensPane pane;
	if ((!bView && !bReticle) || !GetLensPane(pViewModel, data, CurrentViewOrigin(), pane))
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
	LensPane pane, farPane;
	if (!state.bLiveView || !GetLensPane(pViewModel, data, CurrentViewOrigin(), pane, &farPane))
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
