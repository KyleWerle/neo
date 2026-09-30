#pragma once

// The gunplay crosshair layer (GUNPLAY-PLAN.md): ghost linework around the player's own crosshair, which stays the
// primary crosshair. A nod to the original NT crosshairs: at the hip their minimal "Alt" (square end caps on the
// horizontal, a post below), aimed their heavier "Default" (long pill caps, a tick ladder above). Its parts ride
// the spread cone, eased, and sit at depths along the aim: the caps and post near the gun, so the recoil knock and
// spread pivot shift them against the steady centre. The ladder is the magazine, a tick a round (or a group).
// With Enable Gunplay only; cl_neo_gunplay_crosshair turns it off.

class C_NEOBaseCombatWeapon;
class Color;

// Whether the player's crosshair gives way to the layer: the crosshair editor's Default and Alt styles (those
// original shapes are what the layer echoes); a Custom one stays, dead centre. The aim crosshair
// (cl_neo_gunplay_crosshair_aim, neo_gunplay_aim.h) is the player's choice with either.
bool NeoGunplayReplacesCrosshair(C_NEOBaseCombatWeapon *pWeapon, int crosshairStyle);

// Whether the layer draws for this weapon at all (Enable Gunplay, cl_neo_gunplay_crosshair, a firearm).
bool NeoGunplayCrosshairLayerOn(C_NEOBaseCombatWeapon *pWeapon);

// How much of the view a screen fade leaves showing (1 none, 0 black): the spawn's fade in, a map's env_fade (the
// firing range fades to black and back). The HUD paints over it, so our parts fade with it.
float NeoHudFadeVisible();
// Faded all but black: the layer, the sight ghost, the quick info and (with Enable Gunplay) the crosshair wait.
bool NeoHudFadedOut();

// Paints the layer around the crosshair at (x, y) in the HUD pass, in the crosshair's colour; bCentre: the
// player's crosshair gave way to it (see NeoGunplayReplacesCrosshair).
void NeoGunplayPaintCrosshairLayer(C_NEOBaseCombatWeapon *pWeapon, const Color &color, int x, int y, bool bCentre);
