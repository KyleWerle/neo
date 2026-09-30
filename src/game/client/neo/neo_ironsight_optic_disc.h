#pragma once

// Seeing through a weapon's glass where the gun would cover it (neo_ironsight_optic_disc.cpp). Nothing is
// rendered for it: the world is already on screen when the gun is drawn, so leaving the gun out in front of it
// is enough.
//   "window" glass while the gun is drawn over (cloak, thermals): the gun in slices around the glass, so the
//     override never draws the glass itself (NeoIronsightBeginGlassSplit);
//   "scope" glass on the sights otherwise: the housing behind the glass left out (NeoIronsightDrawScopeHole).
// The glass's own art goes back on after the gun (NeoIronsightDrawGlassArt, in neo_ironsight_optic.h). Where
// the lens is comes from neo_ironsight_lens.cpp.

class CNEOWeaponInfo;
class C_BaseAnimating;

// Sight glass ("window") while the gun is drawn over (cloak, thermals): draw the gun once per slice, with that
// slice's clip planes pushed (PushCustomClipPlane), so the panes themselves are left out, and the glass's depth
// (NeoIronsightDrawGlassDepth) just before slice depthBefore. Then draw the art (NeoIronsightDrawGlassArt).
// False when this doesn't apply: draw as usual.
struct NeoIronsightGlassSplit
{
	int slices = 0;
	int depthBefore = 0;
	int planeCount[4] = {};
	float planes[4][2][4] = {};
};
bool NeoIronsightBeginGlassSplit(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	NeoIronsightGlassSplit &split);
// The glass's outline for this frame's split into depth, once a frame (the gun can be drawn more than once a
// frame). The gun far enough behind the glass ("window_skip") goes down before it; no other part of the gun
// behind the glass draws inside it.
void NeoIronsightDrawGlassDepth(const CNEOWeaponInfo &data);

// A "scope" on the sights and not drawn over: its lens's outline into depth, a hair in front of the lens, so
// neither the lens nor the housing behind it draws there and the world shows through, with the art drawn back
// on after the gun. Call before each draw of the gun (depth only, so drawing it again changes nothing).
void NeoIronsightDrawScopeHole(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float ironsightBlend);
