#pragma once

// The ghost's linework (the sight ghost, the gunplay crosshair layer): thin quads in the HUD pass, in three
// weights, each tracing out from its middle as the drawing comes online. Any angle, so strokes turn with
// whatever they follow.

class Color;
class Vector2D;

// Stroke weights, in pixels at 1080p: heavy for what the eye aligns, medium for structure, light for accents.
enum NeoGhostWeight
{
	NEO_GHOST_HEAVY,
	NEO_GHOST_MEDIUM,
	NEO_GHOST_LIGHT,
};

struct NeoGhostPen
{
	float scale = 1.0f;	// the screen's height over 1080
	float trace = 1.0f;	// 0 to 1: how far each stroke has traced out from its middle
};

// Starts drawing strokes in this colour and opacity (0-255).
void NeoGhostBegin(const Color &color, int alpha);

// One stroke from a to b, in screen pixels, with square caps (strokes meeting at a corner join solid).
void NeoGhostStroke(const NeoGhostPen &pen, const Vector2D &a, const Vector2D &b, NeoGhostWeight weight);

// Draws the strokes waiting on their outlines (cl_neo_gunplay_outline): call when a drawing is done.
void NeoGhostFlush();
