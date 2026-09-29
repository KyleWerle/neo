#pragma once

// The magazines left, on every weapon's crosshair (Kyle: the magazine counts move off the quick info onto the
// crosshairs; larger than the families' own magazine readouts and out of the way). The ammo panel's own count:
// magazines left, or a shotgun's shells and slugs. Below the crosshair, clear of it: past the quick info's
// deadzone and between its brackets. Rides the aim crosshair, so it stays calm.

struct NeoCrosshairFrame;

void NeoGunplayPaintReserve(const NeoCrosshairFrame &frame);
