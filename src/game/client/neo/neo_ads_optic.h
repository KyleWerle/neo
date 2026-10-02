#pragma once

// Optics for weapons whose script has an "AdsOptic" block: glass on the gun that it is seen through (red
// dots, holo sights, scope eyepieces). Never a zoom: aiming zooms as stock does, with
// the camera's field of view, so nothing is rendered for the glass. The world is on screen before the gun is
// drawn, and wherever the glass lets it through, it shows; our part is only where the gun would cover it:
//   under the cloak or thermals the whole gun, glass included, is drawn with one override material, so the
//     glass is left out of it and its art drawn back over it (neo_ads_optic_disc.cpp);
//   a scope's housing behind the glass is left out of it on the sights ("scope").
// Everything here is client-only; weapons without the block, or ADS off, are untouched.

class CNEOWeaponInfo;
class C_BaseAnimating;
class C_NEO_Player;

// True while this player sees in thermals (a support in vision mode): their gun is drawn with the opaque
// thermal material then, which covers the glass just as the cloak does.
bool NeoAdsInThermals(const C_NEO_Player *pPlayer);

// Draws the glass's art (the weapon's "reticle") on the glass ("lens_map" with "window_glass", else
// "lens_circle") wherever the gun's own glass doesn't show it: while the glass is hidden (one pane) or drawn over (cloak,
// thermals). Call after the gun. In thermals the filter later colours it like the rest of the screen.
void NeoAdsDrawGlassArt(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float adsBlend);
