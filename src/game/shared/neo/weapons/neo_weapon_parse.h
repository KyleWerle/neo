//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#ifndef NEO_WEAPON_PARSE_H
#define NEO_WEAPON_PARSE_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2mp_weapon_parse.h"

//--------------------------------------------------------------------------------------------------------
class CNEOWeaponInfo : public CHL2MPSWeaponInfo
{
public:
	DECLARE_CLASS_GAMEROOT( CNEOWeaponInfo, CHL2MPSWeaponInfo );

	CNEOWeaponInfo();

	virtual void Parse( ::KeyValues *pKeyValuesData, const char *szWeaponName );

	int		m_iBullets;
	float	m_flCycleTime;
	char	szViewModel2[MAX_WEAPON_STRING];		// Team 2 (NSF) wep vm
	char	szBulletCharacter[MAX_BULLET_CHARACTER];// character used to display ammunition in current clip
	char	szDeathIcon[MAX_BULLET_CHARACTER];
	float	m_flPenetration;
	bool	m_bDropOnDeath;
	int		iAimFOV;

	float	m_flVMFov;
	Vector	m_vecVMPosOffset;
	QAngle	m_angVMAngOffset;

	float	m_flVMAimFov;
	Vector	m_vecVMAimPosOffset;
	QAngle	m_angVMAimAngOffset;

	bool	m_bHasIronsight;
	float	m_flVMIronFov;
	Vector	m_vecVMIronPosOffset;
	QAngle	m_angVMIronAngOffset;

	// Per-weapon multipliers on the cl_neo_ironsight_recoil_* scales ("IronsightRecoil" block).
	float	m_flIronRecoilVertical;
	float	m_flIronRecoilSide;
	float	m_flIronRecoilBack;
	float	m_flIronRecoilMaxDist;	// <= 0: use cl_neo_ironsight_recoil_max_dist
	float	m_flIronRecoilMaxAngle;	// <= 0: use cl_neo_ironsight_recoil_max_angle

	// Materials hidden while on the sights, e.g. an optic's lens (";"-separated, "IronsightHideMaterials").
	char	m_szIronHideMaterials[256];

	// Optic on the sights ("IronsightOptic" block); m_flIronOpticFov <= 0 means none. See neo_ironsight_optic.h.
	float	m_flIronOpticFov;		// field of view of the magnified live view
	float	m_flIronOpticRadius;	// lens radius on screen, as a fraction of screen height
	char	m_szIronOpticOverlay[MAX_WEAPON_STRING];	// full-screen scope texture when the live view is off
	char	m_szIronOpticReticle[MAX_WEAPON_STRING];	// reticle texture over the live view; empty = red dot
};


#endif // NEO_WEAPON_PARSE_H
