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
	m_flIronOpticRadius = 0.f;
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
	if (KeyValues* pIronOffset = pKeyValuesData->FindKey("AimOffset"))
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
	m_flIronOpticRadius = pOptic ? pOptic->GetFloat("radius", 0.1f) : 0.f;
	V_strncpy(m_szIronOpticOverlay, pOptic ? pOptic->GetString("overlay", "vgui/hud/scopes/scope03") : "", sizeof(m_szIronOpticOverlay));
	V_strncpy(m_szIronOpticReticle, pOptic ? pOptic->GetString("reticle", "") : "", sizeof(m_szIronOpticReticle));
}


