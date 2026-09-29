#pragma once

// The spread ghost (GUNPLAY-PLAN.md, three layers): a faint outline at exactly the spread's edge, for reading the
// spread when the spread view (layer 3) is thrown around by the knock and pivot and popped by each shot. No gap,
// only a little pop a shot, its size on a gentle spring; it rides the aim crosshair's damped, capped offset. A
// shape per family.

struct NeoCrosshairFrame;

// Behind the families' linework. Its size is eased every frame, drawn or not.
void NeoGunplayPaintSpreadGhost(const NeoCrosshairFrame &frame);

// The ghost's edge this frame (its eased, popped size), in pixels from the aim crosshair: where a family's bridges
// can land (the SMG's).
float NeoGunplaySpreadGhostEdge();
