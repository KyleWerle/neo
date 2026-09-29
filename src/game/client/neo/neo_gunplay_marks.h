#pragma once

// The crosshair's second layer (GUNPLAY-PLAN.md, three layers): impact marks. Where each of the local player's
// shots went, exactly (its seed, as the spread pivot works it out), traced to what it hit and kept as that point
// in the world, so a mark stays on its bullet hole as the player turns and moves. Subtle, the lightest layer; each
// family marks its own way and folds the mark into itself as it fades, never toward the centre. Shots already
// fired only: nothing about the next one.

class C_NEOBaseCombatWeapon;
class Vector;
struct NeoCrosshairFrame;

// A shot's direction in the world (aim plus its spread), from the spread pivot's recording.
void NeoGunplayMarkShot(C_NEOBaseCombatWeapon *pWeapon, const Vector &direction);

// A shotgun's pellets (one: the Supa 7's slug).
void NeoGunplayMarkPellets(C_NEOBaseCombatWeapon *pWeapon, const Vector *pDirections, int count);

// The marks, around frame.centre (the true aim point: never the aim crosshair's offset, never the scramble).
void NeoGunplayPaintMarks(const NeoCrosshairFrame &frame);
