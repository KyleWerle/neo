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
