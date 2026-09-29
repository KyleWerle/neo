#pragma once

// The gunplay crosshair layer (GUNPLAY-PLAN.md): ghost linework around the player's own crosshair, which stays the
// primary crosshair. A nod to the original NT crosshairs: at the hip their minimal "Alt" (square end caps on the
// horizontal, a post below), aimed their heavier "Default" (long pill caps, a tick ladder above). Its parts ride
// the spread cone, eased, and sit at depths along the aim: the caps and post near the gun, so the recoil knock and
// spread pivot shift them against the steady centre. The ladder is the magazine, a tick a round (or a group).
// With Enable Gunplay only; cl_neo_gunplay_crosshair turns it off.

class C_NEOBaseCombatWeapon;
class Color;

// Paints the layer around the crosshair at (x, y) in the HUD pass, in the crosshair's colour.
void NeoGunplayPaintCrosshairLayer(C_NEOBaseCombatWeapon *pWeapon, const Color &color, int x, int y);
