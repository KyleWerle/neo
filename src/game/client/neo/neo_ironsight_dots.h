#pragma once

// Glowing sight dots (like tritium night-sight inserts) for weapons with an "IronsightDots" block, shown
// while cloaked, when the thermoptic viewmodel makes the sights hard to read. They are pinned to the gun
// and drawn in the viewmodel pass with a normal additive material (never the cloak), depth tested, so
// they move and occlude like part of the gun.
//
// Placement is given relative to the sight line on the sights (the screen centre), in viewmodel units:
//   "front"	"depth drop"			one dot on the front post
//   "rear"		"depth drop halfgap"	two dots either side of the rear notch
// The first time each aim settles, those points are pinned to the gun's bone. Missing values fall back to
// placements derived from the muzzle depth.

class C_BaseAnimating;
class CNEOWeaponInfo;

// Draws the dots for this viewmodel draw, if the weapon has them and they are due (cloaked, or the
// cl_neo_ironsight_dots_always debug switch).
void NeoIronsightDrawDots(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, float ironsightBlend, bool bAiming, bool bCloaked);
