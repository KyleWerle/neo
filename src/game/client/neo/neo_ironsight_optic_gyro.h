#pragma once

// The gyro reticle ("gyro" in a weapon's IronsightOptic block, e.g. the Jitte's): the lens's own art is kept
// level with the horizon like a stabilised display. When the lens rolls on screen, the reticle is dragged
// along by part of the turn, then a damped spring brings it back to level with a little bounce.

class CNEOWeaponInfo;

// The reticle's roll on screen this frame, in radians (0 = level), given the lens's roll on screen. Steps
// the spring once per frame; later calls in the same frame return the same value.
float NeoIronsightGyroRoll(const CNEOWeaponInfo &data, float lensRoll);
