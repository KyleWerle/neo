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

// Sight glass ("window") while the gun is drawn over (cloak, thermals): the gun must be drawn once per
// slice, with that slice's clip planes pushed (PushCustomClipPlane), and the clear view drawn
// (NeoIronsightDrawGlassView) just before slice viewBefore. The panes themselves are left out. The gun
// just behind the glass ("window_skip" deep) goes down before the view, which covers it inside the glass's
// outline (sight parts see-through in their own material but solid under the override); the gun further
// back goes down after it and shows through. Then draw the reticle (NeoIronsightDrawOpticDisc with
// NEO_LENS_RETICLE). False when this doesn't apply: draw as usual.
struct NeoIronsightGlassSplit
{
	int slices = 0;
	int viewBefore = 0;
	int planeCount[4] = {};
	float planes[4][2][4] = {};
};
bool NeoIronsightBeginGlassSplit(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	NeoIronsightGlassSplit &split);
// The clear view on the glass for this frame's split, drawn once a frame (a two-pass model asks twice).
void NeoIronsightDrawGlassView(const CNEOWeaponInfo &data);
