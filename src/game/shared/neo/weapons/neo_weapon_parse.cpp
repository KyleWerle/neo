//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include <KeyValues.h>
#include "neo_weapon_parse.h"


FileWeaponInfo_t* CreateWeaponInfo()
{
	return new CNEOWeaponInfo;
}


CNEOWeaponInfo::CNEOWeaponInfo()
{
	m_iBullets = 0;
	m_flCycleTime = 0.f;
	szViewModel2[0] = 0;
	szBulletCharacter[0] = 0;
	szDeathIcon[0] = 0;
	m_flPenetration = 0.f;
	m_bDropOnDeath = true;
	iAimFOV = 0;

	m_flVMFov = m_flVMAimFov = m_flVMIronFov = 0.f;
	m_bHasIronsight = false;
	m_flIronRecoilVertical = m_flIronRecoilSide = m_flIronRecoilBack = 1.f;
	m_flIronRecoilMaxDist = m_flIronRecoilMaxAngle = 0.f;
	m_szIronHideMaterials[0] = 0;
	m_flIronOpticFov = 0.f;
	m_szIronOpticLens[0] = 0;
	m_flIronOpticMagnification = 0.f;
	m_bHasIronOpticLensMap = false;
	m_szIronOpticLensBone[0] = 0;
	m_bHasIronDots = false;
	m_vecIronDotFront.Init();
	m_vecIronDotRear.Init();
	m_flIronDotSize = 0.f;
	m_szIronOpticOverlay[0] = 0;
	m_szIronOpticReticle[0] = 0;
	m_vecVMPosOffset = m_vecVMAimPosOffset = m_vecVMIronPosOffset = vec3_origin;
	m_angVMAngOffset = m_angVMAimAngOffset = vec3_angle;
}


void CNEOWeaponInfo::Parse( KeyValues *pKeyValuesData, const char *szWeaponName )
{
	BaseClass::Parse( pKeyValuesData, szWeaponName );

	m_iPlayerDamage = pKeyValuesData->GetInt( "Damage", 42 ); // Douglas Adams 1952 - 2001
	m_iBullets = pKeyValuesData->GetInt( "Bullets", 1 );
	m_flCycleTime = pKeyValuesData->GetFloat( "CycleTime", 0.15 );

	const char *notFoundStr = "notfound";
	Q_strncpy(szViewModel2, pKeyValuesData->GetString("team2viewmodel", notFoundStr), MAX_WEAPON_STRING);
	// If there was no NSF viewmodel specified, fall back to Source's default "viewmodel" to ensure we have something sensible available.
	// This might happen when attempting to equip a non-NT weapon.
	if (Q_strcmp(szViewModel2, notFoundStr) == 0)
	{
		Q_strncpy(szViewModel2, pKeyValuesData->GetString("viewmodel"), MAX_WEAPON_STRING);
	}

	Q_strncpy( szBulletCharacter, pKeyValuesData->GetString("BulletCharacter", "a"), MAX_BULLET_CHARACTER);
	Q_strncpy( szDeathIcon, pKeyValuesData->GetString("iDeathIcon", ""), MAX_BULLET_CHARACTER);
	m_flPenetration = pKeyValuesData->GetFloat("Penetration", 0);
	m_bDropOnDeath = pKeyValuesData->GetBool("DropOnDeath", true);
	iAimFOV = pKeyValuesData->GetInt("AimFov", 45);

	KeyValues *pViewModel = pKeyValuesData->FindKey("ViewModelOffset");
	if (pViewModel)
	{
		m_flVMFov = pKeyValuesData->GetFloat("VMFov", 60);

		m_vecVMPosOffset.x = pViewModel->GetFloat("forward", 0);
		m_vecVMPosOffset.y = pViewModel->GetFloat("right", 0);
		m_vecVMPosOffset.z = pViewModel->GetFloat("up", 0);

		m_angVMAngOffset[PITCH] = pViewModel->GetFloat("pitch", 0);
		m_angVMAngOffset[YAW] = pViewModel->GetFloat("yaw", 0);
		m_angVMAngOffset[ROLL] = pViewModel->GetFloat("roll", 0);
	}

	// ZoomOffset = Traditional NT aim offset
	// AimOffset = Ironsight offset, used instead when cl_neo_ironsights is enabled (see neo_ironsights.h)
	if (KeyValues* pZoomOffset = pKeyValuesData->FindKey("ZoomOffset"))
	{
		m_flVMAimFov = pZoomOffset->GetFloat("fov", 55);

		m_vecVMAimPosOffset.x = pZoomOffset->GetFloat("forward", 0);
		m_vecVMAimPosOffset.y = pZoomOffset->GetFloat("right", 0);
		m_vecVMAimPosOffset.z = pZoomOffset->GetFloat("up", 0);

		m_angVMAimAngOffset[PITCH] = pZoomOffset->GetFloat("pitch", 0);
		m_angVMAimAngOffset[YAW] = pZoomOffset->GetFloat("yaw", 0);
		m_angVMAimAngOffset[ROLL] = pZoomOffset->GetFloat("roll", 0);
	}

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


