#pragma once

// The sight ghost: while aiming, thin HUD linework in the crosshair colour traced over the real sights,
// like a smartlink projecting the weapon's sight picture. It is pinned to the sight points (the weapon's
// "IronsightDots" placement, computed by neo_ironsight_dots.cpp), so it moves only with the gun's own sway
// and cants with it. Each weapon picks its own design in an "IronsightGhost" block:
//   "rear"	"brackets" | "ticks" | "gate" | "corners"	the rear notch
//   "front"	"chevron" | "post" | "diamond" | "split"	the front post tip
//   "scale"	size multiplier (1 = the default size at 1080p)
//   "label"	a short designation drawn small beside the rear sight (optional)
// cl_neo_ironsight_sight_ghost: 0 = the glowing dots instead, 1 = while cloaked, 2 = whenever aiming.

class CNEOWeaponInfo;
class Color;

// Whether the ghost replaces the dots, and whether it shows now.
bool NeoIronsightSightGhostEnabled();
bool NeoIronsightSightGhostShown(bool bCloaked);

// Records this frame's sight points (world space: front post, rear left, rear right) from the viewmodel
// draw, fading in over the second half of aiming.
void NeoIronsightRecordSightGhost(const CNEOWeaponInfo &data, const Vector points[3], float ironsightBlend);

// Draws the ghost in the HUD pass, if recorded this frame.
void NeoIronsightPaintSightGhost(const Color &color);
