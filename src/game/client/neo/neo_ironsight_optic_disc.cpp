#include "cbase.h"
#include "neo_ironsight_optic_disc.h"
#include "neo_ironsight_optic.h"
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

// One pane of the lens in world space: point(u, v) = origin + u * uAxis + v * vAxis, in lens UV.
struct LensPane
{
	Vector origin, u, v;
};

// The lens pane nearer the eye ("lens_map" or "lens_map2"), from the viewmodel's current bones.
static bool GetLensPane(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, const Vector &eye, LensPane &pane)
{
	const int bone = pViewModel->LookupBone(data.m_szIronOpticLensBone);
	if (bone < 0)
	{
		return false;
	}
	matrix3x4_t lensToWorld;
	pViewModel->GetBoneTransform(bone, lensToWorld);
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
			pane = second;
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
	return true;
}

//-----------------------------------------------------------------------------
// Drawing the lens: the live view, then the reticle, through vertex alpha. While cloaked the gun is drawn
// with the cloak override, which takes a lens's live view with it, so the optic is drawn again here,
// fading out towards the rim to blend into the cloaked gun. Disc lenses ("lens_disc") are drawn this way
// cloaked or not, fading in over their own lens on the sights; sight glass ("window") only while cloaked.
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

void NeoIronsightDrawOpticDisc(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, float ironsightBlend)
{
	if (!pViewModel || !data.m_bHasIronOpticLensMap || (!bCloaked && !data.m_bIronOpticLensDisc)
		|| NeoGetIronsightOpticMode() != NEO_OPTIC_PIP || !NeoIronsightOpticTexture())
	{
		return;
	}
	float centreAlpha = 1.0f, fadeStart = 1.0f;
	if (data.m_bIronOpticWindow)
	{
		// Only with this frame's view of what lies behind the glass; clear to its edge, softened at the rim.
		if (!s_window.valid || s_window.frame != gpGlobals->framecount)
		{
			return;
		}
		fadeStart = 0.85f;
	}
	else
	{
		// Disc lenses show only their own lens at the hip; the live view fades in over the second half of aiming.
		float visibility = 1.0f;
		if (data.m_bIronOpticLensDisc)
		{
			const float t = clamp((ironsightBlend - 0.5f) / 0.5f, 0.0f, 1.0f);
			visibility = t * t * (3.0f - 2.0f * t);
			if (visibility <= 0.0f)
			{
				return;
			}
		}
		centreAlpha = visibility * (bCloaked ? cl_neo_ironsight_optic_cloak_alpha.GetFloat() : 1.0f);
		fadeStart = bCloaked ? cl_neo_ironsight_optic_cloak_fade.GetFloat() : 1.0f;
	}
	LensPane pane;
	if (!GetLensPane(pViewModel, data, CurrentViewOrigin(), pane))
	{
		return;
	}
	DrawLensShape(LiveViewMaterial(), pane, data, centreAlpha, fadeStart, true);
	if (data.m_szIronOpticReticle[0])
	{
		IMaterial *pReticle = materials->FindMaterial(data.m_szIronOpticReticle, TEXTURE_GROUP_VGUI, false);
		if (pReticle && !pReticle->IsErrorMaterial())
		{
			DrawLensShape(pReticle, pane, data, centreAlpha, fadeStart, false);
		}
	}
}
