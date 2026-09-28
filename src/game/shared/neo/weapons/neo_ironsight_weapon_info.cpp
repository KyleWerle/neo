#include <KeyValues.h>
#include "neo_weapon_parse.h"

void CNEOIronsightWeaponInfo::ParseIronsights(KeyValues *pKeyValuesData)
{
	m_bHasIronsight = false;
	// "IronsightDisabled" keeps a weapon on the classic zoom pose (and its crosshair) with ironsights on.
	KeyValues* pIronOffset = pKeyValuesData->GetBool("IronsightDisabled") ? nullptr : pKeyValuesData->FindKey("AimOffset");
	if (pIronOffset)
	{
		m_bHasIronsight = true;
		m_flVMIronFov = pIronOffset->GetFloat("fov", 55);

		m_vecVMIronPosOffset.x = pIronOffset->GetFloat("forward", 0);
		m_vecVMIronPosOffset.y = pIronOffset->GetFloat("right", 0);
		m_vecVMIronPosOffset.z = pIronOffset->GetFloat("up", 0);

		m_angVMIronAngOffset[PITCH] = pIronOffset->GetFloat("pitch", 0);
		m_angVMIronAngOffset[YAW] = pIronOffset->GetFloat("yaw", 0);
		m_angVMIronAngOffset[ROLL] = pIronOffset->GetFloat("roll", 0);
	}

	// Optional per-weapon multipliers for the fire animation's kick while on the sights.
	KeyValues* pIronRecoil = pKeyValuesData->FindKey("IronsightRecoil");
	m_flIronRecoilVertical = pIronRecoil ? pIronRecoil->GetFloat("vertical", 1) : 1.f;
	m_flIronRecoilSide = pIronRecoil ? pIronRecoil->GetFloat("side", 1) : 1.f;
	m_flIronRecoilBack = pIronRecoil ? pIronRecoil->GetFloat("back", 1) : 1.f;
	m_flIronRecoilMaxDist = pIronRecoil ? pIronRecoil->GetFloat("max_dist", 0) : 0.f;
	m_flIronRecoilMaxAngle = pIronRecoil ? pIronRecoil->GetFloat("max_angle", 0) : 0.f;
	V_strncpy(m_szIronHideMaterials, pKeyValuesData->GetString("IronsightHideMaterials", ""), sizeof(m_szIronHideMaterials));

	KeyValues* pOptic = pKeyValuesData->FindKey("IronsightOptic");
	m_flIronOpticFov = pOptic ? pOptic->GetFloat("fov", 15) : 0.f;
	m_flIronOpticMagnification = pOptic ? pOptic->GetFloat("magnification", 0) : 0.f;
	V_strncpy(m_szIronOpticLens, pOptic ? pOptic->GetString("lens", "") : "", sizeof(m_szIronOpticLens));
	if (pOptic)
	{
		V_strncpy(m_szIronOpticLensBone, pOptic->GetString("lens_bone", ""), sizeof(m_szIronOpticLensBone));
		Vector &o = m_vecIronOpticLensOrigin, &u = m_vecIronOpticLensU, &v = m_vecIronOpticLensV;
		m_bHasIronOpticLensMap = m_szIronOpticLensBone[0] && sscanf(pOptic->GetString("lens_map", ""), "%f %f %f %f %f %f %f %f %f",
			&o.x, &o.y, &o.z, &u.x, &u.y, &u.z, &v.x, &v.y, &v.z) == 9;
	}
	m_bHasIronOpticLensMap2 = false;
	m_vecIronOpticLensCircle.Init(0.5f, 0.5f, 0.5f);
	m_flIronOpticLensRadiusV = 0.5f;
	m_flIronOpticLensShape = 2.0f;
	if (pOptic)
	{
		Vector &o = m_vecIronOpticLens2Origin, &u = m_vecIronOpticLens2U, &v = m_vecIronOpticLens2V;
		m_bHasIronOpticLensMap2 = m_bHasIronOpticLensMap && sscanf(pOptic->GetString("lens_map2", ""), "%f %f %f %f %f %f %f %f %f",
			&o.x, &o.y, &o.z, &u.x, &u.y, &u.z, &v.x, &v.y, &v.z) == 9;
		Vector &circle = m_vecIronOpticLensCircle;
		const int count = sscanf(pOptic->GetString("lens_circle", "0.5 0.5 0.5"), "%f %f %f %f",
			&circle.x, &circle.y, &circle.z, &m_flIronOpticLensRadiusV);
		if (count < 4)
		{
			m_flIronOpticLensRadiusV = circle.z;
		}
		m_flIronOpticLensShape = Max(1.0f, pOptic->GetFloat("lens_shape", 2.0f));
	}
	m_bIronOpticWindow = pOptic && pOptic->GetBool("window") && m_bHasIronOpticLensMap;
	m_bIronOpticLensDisc = pOptic && (pOptic->GetBool("lens_disc") || m_bIronOpticWindow) && m_bHasIronOpticLensMap;
	m_bIronOpticOnePane = pOptic && pOptic->GetBool("one_pane") && m_bHasIronOpticLensMap2 && m_szIronOpticLens[0];
	m_bIronOpticGyro = pOptic && pOptic->GetBool("gyro") && m_bHasIronOpticLensMap && m_szIronOpticLens[0];
	m_vecIronOpticGyroSpring.Init(19.0f, 0.5f, 0.6f);
	if (pOptic)
	{
		Vector &spring = m_vecIronOpticGyroSpring;
		sscanf(pOptic->GetString("gyro_spring", "19 0.5 0.6"), "%f %f %f", &spring.x, &spring.y, &spring.z);
	}

	KeyValues* pAugment = pKeyValuesData->FindKey("IronsightAugment");
	m_flIronAugmentMagnification = pAugment ? pAugment->GetFloat("magnification", 1.5f) : 0.f;
	m_flIronAugmentSize = pAugment ? clamp(pAugment->GetFloat("size", 0.4f), 0.05f, 1.0f) : 0.f;
	m_flIronAugmentEdge = pAugment ? clamp(pAugment->GetFloat("edge", 0.35f), 0.01f, 1.0f) : 0.f;
	m_flIronAugmentAlpha = pAugment ? clamp(pAugment->GetFloat("alpha", 0.85f), 0.0f, 1.0f) : 0.f;

	KeyValues* pDots = pKeyValuesData->FindKey("IronsightDots");
	m_bHasIronDots = pDots != nullptr;
	if (pDots)
	{
		sscanf(pDots->GetString("front", "0 0"), "%f %f", &m_vecIronDotFront.x, &m_vecIronDotFront.z);
		sscanf(pDots->GetString("rear", "0 0 0"), "%f %f %f", &m_vecIronDotRear.x, &m_vecIronDotRear.z, &m_vecIronDotRear.y);
		m_flIronDotSize = pDots->GetFloat("size", 0.12f);
		// Tritium-style: green front post, orange rear notch.
		m_clrIronDotFront = pDots->FindKey("front_color") ? pDots->GetColor("front_color") : Color(60, 255, 60, 255);
		m_clrIronDotRear = pDots->FindKey("rear_color") ? pDots->GetColor("rear_color") : Color(255, 110, 20, 255);
	}
	V_strncpy(m_szIronOpticOverlay, pOptic ? pOptic->GetString("overlay", "vgui/hud/scopes/scope03") : "", sizeof(m_szIronOpticOverlay));
	V_strncpy(m_szIronOpticReticle, pOptic ? pOptic->GetString("reticle", "") : "", sizeof(m_szIronOpticReticle));
}
