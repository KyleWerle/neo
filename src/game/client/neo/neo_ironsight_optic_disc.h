#pragma once

// Optics drawn by us over a weapon's lens rather than on its mesh (neo_ironsight_optic_disc.cpp):
//   any optic while cloaked, since the cloak override replaces the lens material;
//   "lens_disc" lenses (e.g. the Jitte's), fading in on the sights over their untouched lens;
//   "window" sight glass (red dots, holo sights), only while cloaked: the glass shows exactly the world
//     behind it, so the cloak doesn't smear the view through the sight.
// The view itself is rendered by neo_ironsight_optic.cpp; where the lens is, by neo_ironsight_lens.cpp.
// NeoIronsightDrawOpticDisc, the per-frame drawing call, is declared in neo_ironsight_optic.h.

class CNEOWeaponInfo;
class C_BaseAnimating;

// Sight glass ("window") while the gun is drawn over (cloak, thermals): draw the clear view first
// (NeoIronsightDrawGlassView), then the gun once per slice, with that slice's clip planes pushed
// (PushCustomClipPlane), so the panes themselves are left out, and the view's depth
// (NeoIronsightDrawGlassDepth) just before slice depthBefore. Then draw the reticle
// (NeoIronsightDrawOpticDisc with NEO_LENS_RETICLE). False when this doesn't apply: draw as usual.
struct NeoIronsightGlassSplit
{
	int slices = 0;
	int depthBefore = 0;
	int planeCount[4] = {};
	float planes[4][2][4] = {};
};
bool NeoIronsightBeginGlassSplit(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	NeoIronsightGlassSplit &split);
// The clear view on the glass for this frame's split, and its depth, each drawn once a frame (the gun can be
// drawn more than once a frame). The gun far enough behind the glass ("window_skip") goes down between them,
// over the view; no other part of it behind the glass draws there.
void NeoIronsightDrawGlassView(const CNEOWeaponInfo &data);
void NeoIronsightDrawGlassDepth(const CNEOWeaponInfo &data);
