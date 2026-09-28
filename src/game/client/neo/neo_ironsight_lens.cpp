#include "cbase.h"
#include "neo_ironsight_lens.h"
#include "neo_ironsight_optic.h"
#include "c_neo_player.h"
#include "neo_predicted_viewmodel.h"
#include "weapon_neobasecombatweapon.h"
#include "view_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

Vector NeoLensPane::Centre(const CNEOWeaponInfo &data) const
{
	return At(data.m_vecIronOpticLensCircle.x, data.m_vecIronOpticLensCircle.y);
}

// The lens bone's index on the viewmodel, looked up by name only when the model or the weapon changes.
static int LensBone(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data)
{
	static const studiohdr_t *s_pModel = nullptr;
	static const CNEOWeaponInfo *s_pData = nullptr;
	static int s_bone = -1;
	const CStudioHdr *pHdr = pViewModel ? pViewModel->GetModelPtr() : nullptr;
	const studiohdr_t *pModel = pHdr ? pHdr->GetRenderHdr() : nullptr;
	if (!pModel)
	{
		return -1;
	}
	if (pModel != s_pModel || &data != s_pData)
	{
		s_pModel = pModel;
		s_pData = &data;
		s_bone = pViewModel->LookupBone(data.m_szIronOpticLensBone);
	}
	return s_bone;
}

bool NeoIronsightLensPane(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, const Vector &eye, NeoLensPane &pane,
	NeoLensPane *pFarPane)
{
	const int bone = LensBone(pViewModel, data);
	if (bone < 0)
	{
		return false;
	}
	matrix3x4_t lensToWorld;
	// From the drawn pose, not GetBoneTransform: its cache holds only hitbox bones, and lens bones that are not
	// (the MX-S's sight_glass) would come back as the viewmodel's origin.
	MatrixCopy(pViewModel->GetBone(bone), lensToWorld);
	const auto toWorld = [&](const Vector &origin, const Vector &u, const Vector &v, NeoLensPane &out) {
		VectorTransform(origin, lensToWorld, out.origin);
		VectorRotate(u, lensToWorld, out.u);
		VectorRotate(v, lensToWorld, out.v);
	};
	toWorld(data.m_vecIronOpticLensOrigin, data.m_vecIronOpticLensU, data.m_vecIronOpticLensV, pane);
	if (data.m_bHasIronOpticLensMap2)
	{
		NeoLensPane second;
		toWorld(data.m_vecIronOpticLens2Origin, data.m_vecIronOpticLens2U, data.m_vecIronOpticLens2V, second);
		if (second.Centre(data).DistToSqr(eye) < pane.Centre(data).DistToSqr(eye))
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

float NeoIronsightFovScale(const CViewSetup &view)
{
	return tanf(DEG2RAD(view.fov * 0.5f)) / tanf(DEG2RAD(view.fovViewmodel * 0.5f));
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
	float fovScale = 1.0f;		// see NeoIronsightFovScale
	float tanHalf = 1.0f;		// the optic camera's half field of view
	const char *pszDebugReason = "";
} s_window;

bool NeoIronsightWindowReady()
{
	return s_window.valid && s_window.frame == gpGlobals->framecount;
}

const char *NeoIronsightWindowDebugReason()
{
	return s_window.pszDebugReason;
}

void NeoIronsightWindowTexCoord(const Vector &world, float &u, float &v)
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
	NeoLensPane pane;
	if (!NeoIronsightLensPane(pViewModel, data, mainView.origin, pane))
	{
		s_window.pszDebugReason = pViewModel ? "window camera: lens bone not found" : "window camera: no viewmodel";
		return false;
	}

	matrix3x4_t eyeToWorld;
	AngleMatrix(mainView.angles, mainView.origin, eyeToWorld);
	MatrixInvert(eyeToWorld, s_window.worldToEye);
	s_window.fovScale = NeoIronsightFovScale(mainView);

	// The rays through the corners of the glass's bounding box, in eye space.
	const Vector &circle = data.m_vecIronOpticLensCircle;
	Vector rays[4];
	Vector middle(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < 4; ++i)
	{
		const float u = circle.x + ((i & 1) ? circle.z : -circle.z);
		const float v = circle.y + ((i & 2) ? data.m_flIronOpticLensRadiusV : -data.m_flIronOpticLensRadiusV);
		Vector eye;
		VectorTransform(pane.At(u, v), s_window.worldToEye, eye);
		if (eye.x <= 0.1f)
		{
			s_window.pszDebugReason = "window camera: a glass corner is behind the eye";
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
	s_window.pszDebugReason = "";
	return true;
}
