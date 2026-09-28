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
	float	m_flIronOpticFov;		// field of view of the live view, when no magnification can be used
	float	m_flIronOpticMagnification;	// zoom relative to the lens's size on screen (1 = like empty glass); needs "lens_map"
	char	m_szIronOpticLens[MAX_WEAPON_STRING];	// lens material the live view is drawn on; empty = overlay only
	// The lens surface on its bone, for drawing the optic ourselves while cloaked: point(u, v) =
	// origin + u * uAxis + v * vAxis in "lens_bone" space ("lens_map", extracted from the model).
	bool	m_bHasIronOpticLensMap;
	char	m_szIronOpticLensBone[MAX_WEAPON_STRING];
	Vector	m_vecIronOpticLensOrigin;
	Vector	m_vecIronOpticLensU;
	Vector	m_vecIronOpticLensV;
	// The round lens within that surface in UV: centre (x, y) and radius (z) ("lens_circle", default the
	// whole square). With "lens_disc", the lens keeps its own material and the optic is drawn as this
	// disc over it, fading in on the sights, with the reticle on top (for square lens meshes, e.g. the Jitte's).
	Vector	m_vecIronOpticLensCircle;
	bool	m_bIronOpticLensDisc;
	char	m_szIronOpticOverlay[MAX_WEAPON_STRING];	// full-screen scope texture when the live view is off
	char	m_szIronOpticReticle[MAX_WEAPON_STRING];	// reticle texture over the live view; empty = red dot

	// Glowing sight dots while cloaked ("IronsightDots" block). See neo_ironsight_dots.h.
	bool	m_bHasIronDots;
	Vector	m_vecIronDotFront;	// x = depth along the sight line, z = drop (0 depth = auto)
	Vector	m_vecIronDotRear;	// x = depth, y = half the gap between the two dots, z = drop
	float	m_flIronDotSize;	// dot radius in viewmodel units
	Color	m_clrIronDotFront;
	Color	m_clrIronDotRear;
};


#endif // NEO_WEAPON_PARSE_H
