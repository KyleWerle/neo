#pragma once

// The cyberbrain's weapon on the gun (HUD-REDESIGN.md, "Weapon on the gun"): the viewmodel drawn a second time as a
// wireframe, clipped to bands across the gun (a scan from the muzzle back on reload, a flash at the muzzle on each
// shot, the BALC's heat creeping back from the muzzle), and where the gun's callout points are on screen, for the
// weapon group's leaders. Idle, the gun stays clean. Only while the cyberbrain HUD shows.

#include "mathlib/vector2d.h"
#include "Color.h"

class C_BaseAnimating;

constexpr int NEO_CYBER_GUN_BANDS = 3;

// One band: two clip planes (a plane (n, d) keeps the points with n.p >= d) and its colour.
struct NeoCyberGunBand { float planes[2][4]; Color color; float alpha; };
struct NeoCyberGunPass { NeoCyberGunBand bands[NEO_CYBER_GUN_BANDS]; int count = 0; };

// Called by the viewmodel after the gun is drawn, with the overlays: reads the gun's callout points as drawn (this
// render view's own matrices, so they sit on the gun at the viewmodel's field of view) and returns the bands to draw.
bool NeoCyberGunPrepare(C_BaseAnimating *pViewModel, NeoCyberGunPass &pass);
// Brackets one band's draw of the gun: the wireframe override and the band's planes on, then off.
void NeoCyberGunBandBegin(const NeoCyberGunPass &pass, int band);
void NeoCyberGunBandEnd();

enum NeoCyberGunPoint { NEO_GUN_MUZZLE, NEO_GUN_MAG, NEO_GUN_EJECT, NEO_GUN__COUNT };
// Where a point on the gun was drawn this frame or the last, screen pixels; false if it wasn't.
bool NeoCyberGunPointOnScreen(NeoCyberGunPoint point, Vector2D &out);
