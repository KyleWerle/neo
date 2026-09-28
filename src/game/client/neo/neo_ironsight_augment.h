#pragma once

// Augmented aim for weapons kept on the classic NT zoom with ironsights on ("IronsightAugment" block,
// e.g. the MPN45): while aiming, a window around the crosshair shows the view magnified again on top of
// the aim's zoom, opaque, fading to translucent towards its rim. Growing dotted squares and a readout line
// frame it in the crosshair's colour; the crosshair becomes a centre dot and a ring for the weapon's
// spread, scaled to the window's zoom. Client-only; ironsights off, nothing changes.

class C_NEOBaseCombatWeapon;
class CViewSetup;
class Color;

// If the augment is up (or fading), sets up its view of the scene into the optic render target and
// returns true; the caller renders the scene with it.
bool NeoIronsightAugmentView(const CViewSetup &mainView, CViewSetup &augmentView);
// Draws the window into the scene; call just before the viewmodels (they draw over it) and post-processing.
void NeoIronsightDrawAugmentWindow(const CViewSetup &mainView);

// Draws the augment for the crosshair at (x, y) in its colour, and returns true if it replaces the
// crosshair this frame: always for augment weapons, which show no crosshair at the hip.
bool NeoIronsightPaintAugment(C_NEOBaseCombatWeapon *pWeapon, const Color &color, int x, int y);
