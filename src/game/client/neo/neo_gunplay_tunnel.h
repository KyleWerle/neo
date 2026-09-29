#pragma once

// The projected shooting space (GUNPLAY-PLAN.md): thin outlines sliding out from the gun's muzzle along the aim,
// fading as they go, each the spread cone's size where it is: where the shots can land at every range. The cone
// is the eye's, so seen down the aim line every slice of it is the same size on screen; starting at the muzzle,
// beside the gun and small, is what gives the tunnel its depth. The muzzle moves with the gun's knock and pivot,
// so the tunnel bends with them. Part of the crosshair layer, aimed; the MPN45's window reuses it.

class C_NEO_Player;
class Color;

// Paints the tunnel in the HUD pass. spreadTangent: the cone's half-angle as a tangent (a weapon's bullet spread);
// opacity 0 to 1.
void NeoGunplayPaintTunnel(C_NEO_Player *pPlayer, float spreadTangent, const Color &color, float opacity);
