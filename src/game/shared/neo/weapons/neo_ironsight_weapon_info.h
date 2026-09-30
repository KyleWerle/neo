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

	// Optic on the sights ("IronsightOptic" block): glass the gun is seen through, never a zoom (aiming zooms
	// as stock does, with the camera's field of view). See neo_ironsight_optic.h.
	bool	m_bHasIronOptic = false;
	char	m_szIronOpticLens[MAX_WEAPON_STRING] = "";	// the glass's material
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
	// ("lens_shape": 2 = round, higher = squarer).
	Vector	m_vecIronOpticLensCircle = Vector(0.5f, 0.5f, 0.5f);
	float	m_flIronOpticLensRadiusV = 0.5f;
	float	m_flIronOpticLensShape = 2.0f;
	// "window": the gun's own glass, see-through in its own material (every optic is one). While the gun is
	// drawn over (cloak, thermals), the glass is left out of it, so the world already on screen shows through,
	// with the glass's own texture on top.
	bool	m_bIronOpticWindow = false;
	// "scope": glass with a housing behind it (the Jittes' sight, the MX's eyepiece): on the sights, the gun
	// behind the glass is hidden inside its outline in every state, not only while drawn over, so the view
	// through the glass is clear.
	bool	m_bIronOpticScope = false;
	// "eyepiece": a magnifying scope's eyepiece (the MX): seen through only on the sights. Off them the gun
	// draws whole in every state, the scope's inside included, as a real scope shows nothing off its axis
	// (without it, under the cloak or thermals, the hip saw the world through the whole tube).
	bool	m_bIronOpticEyepiece = false;
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
	// "reticle_in_lens": sight glass whose art has a dark frame (ZR68 red dot) draws its reticle only in the
	// clear part ("lens_circle"), softened at the edge, rather than over the whole glass.
	bool	m_bIronOpticReticleInLens = false;
	// "collimated_dot" "u v radius": sight glass whose dot is projected at infinity, as on a real red dot. The
	// dot (at u, v in the glass art, radius in u) is lifted out of the art and drawn where a line from the eye
	// along the sight's axis crosses the glass, so it marks where the gun points and slides off the glass when
	// seen from off axis. The glass material ("lens") is hidden and its art drawn by us in every state. See
	// neo_ironsight_collimator.h.
	bool	m_bIronOpticCollimated = false;
	Vector	m_vecIronOpticDot = Vector(0.5f, 0.5f, 0.02f);
	// "dot_color" "r g b": the dot's colour, for its afterimage (neo_ironsight_dot_trail.h); alpha 0 when unset,
	// for cl_neo_ironsight_dot_trail_color.
	Color	m_clrIronOpticDot = Color(0, 0, 0, 0);
	// The glass's art, drawn by us wherever the gun's own glass doesn't show it (hidden, or drawn over).
	char	m_szIronOpticReticle[MAX_WEAPON_STRING] = "";

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
