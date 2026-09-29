#pragma once

// Collimated sight glass ("collimated_dot" in a weapon's IronsightOptic block): as on a real red dot, the dot
// is projected at infinity. It is lifted out of the glass art and drawn where a line from the eye along the
// sight's axis crosses the glass, so it sits on the target while the glass moves around it, and slides off the
// glass (fading at its edge) when the sight is seen from off axis, e.g. from the hip. The sight's axis is the
// eye's forward the first time the gun settles on the sights, kept in the glass's own frame, so the gun's
// kick from each shot (fire animation, spread pivot, recoil knock) carries the dot, but not its bob, sway lag
// or view shake, which the bullets don't share. The glass material is hidden (neo_ironsights.cpp) and its
// art drawn by us in every state.

class C_BaseAnimating;
class CNEOWeaponInfo;
class IMaterial;
class Vector2D;
struct NeoLensPane;

// Draws the collimated sight's art on its pane over the area outline (a convex polygon of UV points, in order):
// the art with its dot lifted out (the hole filled from the art around it), then the dot at its floating place.
// alpha is the art's opacity, fading to the rim from fadeStart (a fraction of the way out). False if the dot
// isn't inside the area; the caller then draws the art as usual.
bool NeoIronsightDrawCollimatedArt(C_BaseAnimating *pViewModel, IMaterial *pArt, const NeoLensPane &pane,
	const CNEOWeaponInfo &data, const Vector2D *pOutline, int points, float alpha, float fadeStart);
