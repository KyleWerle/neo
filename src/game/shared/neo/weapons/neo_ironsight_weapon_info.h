#pragma once

#include "mathlib/vector.h"
#include "mathlib/vector2d.h"
#include "Color.h"
#include "weapon_parse.h"

class KeyValues;

// Sight ghost designs ("IronsightGhost" block; see neo_ironsight_sight_ghost.h).
enum NeoGhostRear
{
	NEO_GHOST_REAR_BRACKETS,
	NEO_GHOST_REAR_TICKS,
	NEO_GHOST_REAR_GATE,
	NEO_GHOST_REAR_CORNERS,
};
enum NeoGhostFront
{
	NEO_GHOST_FRONT_CHEVRON,
	NEO_GHOST_FRONT_POST,
	NEO_GHOST_FRONT_DIAMOND,
	NEO_GHOST_FRONT_SPLIT,
};

//--------------------------------------------------------------------------------------------------------
// A weapon's ironsight settings from its script: the "AimOffset" pose and the Ironsight* blocks. See
// neo_ironsights.h. CNEOWeaponInfo inherits them, so they read as its own members.
//--------------------------------------------------------------------------------------------------------
class CNEOIronsightWeaponInfo
{
public:
	void ParseIronsights(KeyValues *pKeyValuesData);

	// The pose on the sights ("AimOffset"; the classic aim is "ZoomOffset"). "IronsightDisabled" keeps a
	// weapon on the classic zoom (and its crosshair) with ironsights on.
	bool	m_bHasIronsight = false;
	float	m_flVMIronFov = 0.0f;
	Vector	m_vecVMIronPosOffset = Vector(0.0f, 0.0f, 0.0f);
	QAngle	m_angVMIronAngOffset = QAngle(0.0f, 0.0f, 0.0f);

	// Per-weapon multipliers on the cl_neo_ironsight_recoil_* scales ("IronsightRecoil" block).
	float	m_flIronRecoilVertical = 1.0f;
	float	m_flIronRecoilSide = 1.0f;
	float	m_flIronRecoilBack = 1.0f;
	float	m_flIronRecoilMaxDist = 0.0f;	// <= 0: use cl_neo_ironsight_recoil_max_dist
	float	m_flIronRecoilMaxAngle = 0.0f;	// <= 0: use cl_neo_ironsight_recoil_max_angle

	// Materials hidden while on the sights, e.g. an optic's lens (";"-separated, "IronsightHideMaterials").
	char	m_szIronHideMaterials[256] = "";

	// Optic on the sights ("IronsightOptic" block); m_flIronOpticFov <= 0 means none. See neo_ironsight_optic.h.
	float	m_flIronOpticFov = 0.0f;		// field of view of the live view, when no magnification can be used
	float	m_flIronOpticMagnification = 0.0f;	// zoom relative to the lens's size on screen (1 = like empty glass); needs "lens_map"
	char	m_szIronOpticLens[MAX_WEAPON_STRING] = "";	// the lens's material; empty = overlay only
	// The lens surface on its bone, for drawing the optic ourselves: point(u, v) = origin + u * uAxis +
	// v * vAxis in "lens_bone" space ("lens_map", extracted from the model).
	bool	m_bHasIronOpticLensMap = false;
	char	m_szIronOpticLensBone[MAX_WEAPON_STRING] = "";
	Vector	m_vecIronOpticLensOrigin = Vector(0.0f, 0.0f, 0.0f);
	Vector	m_vecIronOpticLensU = Vector(0.0f, 0.0f, 0.0f);
	Vector	m_vecIronOpticLensV = Vector(0.0f, 0.0f, 0.0f);
	// A second pane of the same glass ("lens_map2", e.g. a holo sight's front and rear windows); the optic
	// is drawn on whichever is nearer the eye.
	bool	m_bHasIronOpticLensMap2 = false;
	Vector	m_vecIronOpticLens2Origin = Vector(0.0f, 0.0f, 0.0f);
	Vector	m_vecIronOpticLens2U = Vector(0.0f, 0.0f, 0.0f);
	Vector	m_vecIronOpticLens2V = Vector(0.0f, 0.0f, 0.0f);
	// The lens within that surface in UV ("lens_circle" "u v radius [vradius]", default the whole square):
	// centre (x, y), radius across (z) and down (m_flIronOpticLensRadiusV). Its shape is a superellipse
	// ("lens_shape": 2 = round, higher = squarer). With "lens_disc", the lens keeps its own material and the
	// optic is drawn as this shape over it, fading in on the sights, with the reticle on top.
	Vector	m_vecIronOpticLensCircle = Vector(0.5f, 0.5f, 0.5f);
	float	m_flIronOpticLensRadiusV = 0.5f;
	float	m_flIronOpticLensShape = 2.0f;
	bool	m_bIronOpticLensDisc = false;
	// "window": clear sight glass (red dots, holo sights). While the gun is drawn over (cloak, thermals), the
	// glass shows the world behind it exactly (no zoom), with the glass's own texture on top.
	bool	m_bIronOpticWindow = false;
	// "window_skip": how deep behind the glass, in viewmodel units, the gun is hidden inside its outline while
	// drawn over: sight parts there, see-through in their own material, would be solid. The gun further
	// back (its front, seen through the sight at the hip) shows. Without it, all of the gun behind the glass
	// is hidden there. Measure with art/optics/find-glass-plates.py; tune live with
	// cl_neo_ironsight_window_skip.
	float	m_flIronOpticWindowSkip = -1.0f;
	// "window_glass" "u v u v ...": the whole glass's outline in UV, its mesh's vertices (from
	// art/optics/extract-lens-map.py --dump; their convex hull is used). While the gun is drawn over, the
	// clear view and the reticle cover exactly this, so none of the glass is left see-through to what is
	// behind it. Kept as its bounding box (centre, half-width, half-height) and the hull's corners around
	// that centre, counter-clockwise. Without it, the lens: "lens_circle" and "lens_shape".
	static constexpr int IRON_WINDOW_GLASS_MAX = 32;
	Vector	m_vecIronOpticWindowCircle = Vector(0.5f, 0.5f, 0.5f);
	float	m_flIronOpticWindowRadiusV = 0.5f;
	int		m_iIronOpticWindowGlassPoints = 0;
	Vector2D m_vecIronOpticWindowGlass[IRON_WINDOW_GLASS_MAX];
	// "one_pane": for glass with two panes that both carry its art ("lens_map2"), the glass material ("lens")
	// is hidden and its reticle drawn once, on the pane nearer the eye, so the art doesn't show twice.
	bool	m_bIronOpticOnePane = false;
	char	m_szIronOpticOverlay[MAX_WEAPON_STRING] = "";	// full-screen scope texture when the live view is off
	char	m_szIronOpticReticle[MAX_WEAPON_STRING] = "";	// reticle material over the live view

	// Augmented aim ("IronsightAugment" block), for weapons kept on the classic zoom: while aiming, a
	// magnified window around the crosshair. See neo_ironsight_augment.h.
	float	m_flIronAugmentMagnification = 0.0f;	// <= 0: none; zoom on top of the aim's own
	float	m_flIronAugmentSize = 0.0f;		// window width and height, as a fraction of the screen's
	float	m_flIronAugmentEdge = 0.0f;		// how far in from the rim it fades to translucent, fraction of its half-size
	float	m_flIronAugmentAlpha = 0.0f;		// opacity inside the fade

	// Glowing sight dots while cloaked ("IronsightDots" block). See neo_ironsight_dots.h.
	bool	m_bHasIronDots = false;
	Vector	m_vecIronDotFront = Vector(0.0f, 0.0f, 0.0f);	// x = depth along the sight line, z = drop (0 depth = auto)
	Vector	m_vecIronDotRear = Vector(0.0f, 0.0f, 0.0f);	// x = depth, y = half the gap between the two dots, z = drop
	float	m_flIronDotSize = 0.0f;	// dot radius in viewmodel units
	Color	m_clrIronDotFront;
	Color	m_clrIronDotRear;

	// Sight ghost design ("IronsightGhost" block), drawn over the IronsightDots sight points. See
	// neo_ironsight_sight_ghost.h.
	int		m_iIronGhostRear = NEO_GHOST_REAR_BRACKETS;
	int		m_iIronGhostFront = NEO_GHOST_FRONT_CHEVRON;
	float	m_flIronGhostScale = 1.0f;

private:
	void ParseWindowGlass(const char *pszPoints);
};
