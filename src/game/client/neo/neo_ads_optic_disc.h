#pragma once

// Seeing through a weapon's glass where the gun would cover it (neo_ads_optic_disc.cpp). Nothing is
// rendered for it: the world is already on screen when the gun is drawn, so leaving the gun out in front of it
// is enough. The glass's exact outline goes into depth before the gun, so neither the glass nor the gun behind
// it draws there:
//   "window" glass while the gun is drawn over (cloak, thermals), where the override would draw the glass solid;
//   "scope" glass on the sights otherwise, where the housing behind the glass would show.
// The glass's own art goes back on after the gun (NeoAdsDrawGlassArt, in neo_ads_optic.h). Where
// the lens is comes from neo_ads_lens.cpp.

class CNEOWeaponInfo;
class C_BaseAnimating;

// How to draw the gun round its clear glass this frame. Usually: the depth (NeoAdsDrawGlassClearDepth),
// then the gun. With bFarFirst ("window_skip", drawn over): the gun with farPlane pushed (PushCustomClipPlane),
// the depth, then the gun with nearPlane pushed, so the gun far behind the glass shows through it.
struct NeoAdsGlassClear
{
	bool bFarFirst = false;
	float farPlane[4] = {};
	float nearPlane[4] = {};
};
// False when nothing is cleared (no glass to see through, an "eyepiece" off the sights): draw as usual.
// Worked out once a frame.
bool NeoAdsBeginGlassClear(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float adsBlend, NeoAdsGlassClear &clear);
// This frame's glass outline into depth, a hair in front of the glass. Depth only: call before each draw of the gun.
void NeoAdsDrawGlassClearDepth(const CNEOWeaponInfo &data);
