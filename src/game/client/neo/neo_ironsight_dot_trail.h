#pragma once

// The collimated dot's afterimage: a fading streak where the dot pointed, fixed to the world at infinity (the
// dot's own distance), like a glow stick swung in the dark. It spills past the glass, as an afterimage in the
// eye would. Only in the dark, only while the aim moves quickly, and cut by each shot (the muzzle flash puts it
// out), coming back a moment after the last one, so autofire never smears it.

class Color;
class Vector;

// The dot as drawn this frame: where it points (world direction from the eye), its angular radius (radians), how
// visible it is and its colour (the weapon's "dot_color"; alpha 0 for cl_neo_ironsight_dot_trail_color). Called by
// the collimator whenever it draws the dot.
void NeoIronsightRecordDotTrail(const Vector &direction, float angularRadius, float strength, const Color &colour);

// Paints the trail over the view (from the HUD).
void NeoIronsightPaintDotTrail();
