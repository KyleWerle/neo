#pragma once

// The crosshair's first layer (GUNPLAY-PLAN.md, three layers): the aim crosshair. Small, steady and the brightest
// of the three: it follows a small share of the gun's turn (knock and pivot, so it leans toward each bullet) on a
// slow spring, and never leaves the inner part of the spread cone, so the true aim point is always inside what it
// shows. Also the link: how settled the layers are, to lock them together.

#include "mathlib/vector2d.h"

struct NeoCrosshairFrame;

// Before the families paint: the aim crosshair's offset (frame.aimOffset) and the link (frame.link,
// frame.sinceLock), from frame.deviation, frame.spread, frame.ready and the gun's accuracy penalty.
void NeoGunplayAimUpdate(NeoCrosshairFrame &frame);

// The aim crosshair itself, when cl_neo_gunplay_crosshair_aim is on: after the families, on top.
void NeoGunplayPaintAim(const NeoCrosshairFrame &frame);
