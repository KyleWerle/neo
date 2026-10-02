#pragma once

#include "mathlib/vector.h"
#include "mathlib/vector2d.h"
#include "weapon_parse.h"

class KeyValues;

//--------------------------------------------------------------------------------------------------------
// A weapon's optic settings from its script, the "AdsOptic" block: glass the gun is seen through on the
// sights. See neo_ads_optic.h. CNEOWeaponInfo inherits them, so they read as its own members.
//--------------------------------------------------------------------------------------------------------
class CNEOAdsOpticInfo
{
public:
	void ParseAdsOptic(KeyValues *pKeyValuesData);

	// Optic on the sights ("AdsOptic" block): glass the gun is seen through, never a zoom (aiming zooms
	// as stock does, with the camera's field of view). See neo_ads_optic.h.
	bool	m_bHasAdsOptic = false;
	char	m_szAdsOpticLens[MAX_WEAPON_STRING] = "";	// the glass's material
	// The lens surface on its bone, for drawing the optic ourselves: point(u, v) = origin + u * uAxis +
	// v * vAxis in "lens_bone" space ("lens_map", extracted from the model).
	bool	m_bHasAdsOpticLensMap = false;
	char	m_szAdsOpticLensBone[MAX_WEAPON_STRING] = "";
	Vector	m_vecAdsOpticLensOrigin = Vector(0.0f, 0.0f, 0.0f);
	Vector	m_vecAdsOpticLensU = Vector(0.0f, 0.0f, 0.0f);
	Vector	m_vecAdsOpticLensV = Vector(0.0f, 0.0f, 0.0f);
	// A second pane of the same glass ("lens_map2", e.g. a holo sight's front and rear windows); the optic
	// is drawn on whichever is nearer the eye.
	bool	m_bHasAdsOpticLensMap2 = false;
	Vector	m_vecAdsOpticLens2Origin = Vector(0.0f, 0.0f, 0.0f);
	Vector	m_vecAdsOpticLens2U = Vector(0.0f, 0.0f, 0.0f);
	Vector	m_vecAdsOpticLens2V = Vector(0.0f, 0.0f, 0.0f);
	// The lens within that surface in UV ("lens_circle" "u v radius [vradius]", default the whole square):
	// centre (x, y), radius across (z) and down (m_flAdsOpticLensRadiusV). Its shape is a superellipse
	// ("lens_shape": 2 = round, higher = squarer).
	Vector	m_vecAdsOpticLensCircle = Vector(0.5f, 0.5f, 0.5f);
	float	m_flAdsOpticLensRadiusV = 0.5f;
	float	m_flAdsOpticLensShape = 2.0f;
	// "window": the gun's own glass, see-through in its own material (every optic is one). While the gun is
	// drawn over (cloak, thermals), the glass is left out of it, so the world already on screen shows through,
	// with the glass's own texture on top.
	bool	m_bAdsOpticWindow = false;
	// "scope": glass with a housing behind it (the Jittes' sight, the MX's eyepiece): on the sights, the gun
	// behind the glass is hidden inside its outline in every state, not only while drawn over, so the view
	// through the glass is clear.
	bool	m_bAdsOpticScope = false;
	// "eyepiece": a magnifying scope's eyepiece (the MX): seen through only on the sights. Off them the gun
	// draws whole in every state, the scope's inside included, as a real scope shows nothing off its axis
	// (without it, under the cloak or thermals, the hip saw the world through the whole tube).
	bool	m_bAdsOpticEyepiece = false;
	// "window_skip": how deep behind the glass, in viewmodel units, the gun is hidden inside its outline while
	// drawn over: sight parts there, see-through in their own material, would be solid. The gun further
	// back (its front, seen through the sight at the hip) shows. Without it, all of the gun behind the glass
	// is hidden there. Measure with art/optics/find-glass-plates.py; tune live with
	// cl_neo_ads_window_skip.
	float	m_flAdsOpticWindowSkip = -1.0f;
	// "window_glass" "u v u v ...": the whole glass's outline in UV, its mesh's vertices (from
	// art/optics/extract-lens-map.py --dump; their convex hull is used). While the gun is drawn over, the
	// clear view and the reticle cover exactly this, so none of the glass is left see-through to what is
	// behind it. Kept as its bounding box's centre and the hull's corners around that centre,
	// counter-clockwise. Without it, the lens: "lens_circle" and "lens_shape" (the centre is the lens's then).
	static constexpr int IRON_WINDOW_GLASS_MAX = 32;
	Vector2D m_vecAdsOpticWindowCentre = Vector2D(0.5f, 0.5f);
	int		m_iAdsOpticWindowGlassPoints = 0;
	Vector2D m_vecAdsOpticWindowGlass[IRON_WINDOW_GLASS_MAX];
	// "one_pane": for glass with two panes that both carry its art ("lens_map2"), the glass material ("lens")
	// is hidden and its reticle drawn once, on the pane nearer the eye, so the art doesn't show twice.
	bool	m_bAdsOpticOnePane = false;
	// "reticle_in_lens": sight glass whose art has a dark frame (ZR68 red dot) draws its reticle only in the
	// clear part ("lens_circle"), softened at the edge, rather than over the whole glass.
	bool	m_bAdsOpticReticleInLens = false;
	// The glass's art, drawn by us wherever the gun's own glass doesn't show it (hidden, or drawn over).
	char	m_szAdsOpticReticle[MAX_WEAPON_STRING] = "";

private:
	void ParseWindowGlass(const char *pszPoints);
};
