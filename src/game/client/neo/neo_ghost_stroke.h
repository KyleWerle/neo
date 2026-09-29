#pragma once

// The ghost's linework (the sight ghost, the gunplay crosshair layer, the quick info): thin quads in the HUD pass,
// batched, in three weights, each tracing out from its middle as the drawing comes online. Any angle, so strokes turn with
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

// A filled quad (screen pixels, in order round it), or a rectangle, in the colour from NeoGhostBegin; no outline.
void NeoGhostFill(const Vector2D corners[4]);
void NeoGhostFillRect(float x0, float y0, float x1, float y1);

// Draws everything queued since the last flush: strokes and fills wait so they go down as one batch (one draw call;
// cl_neo_hud_batch). Call before drawing anything else over them (text) and when a drawing is done.
void NeoGhostFlush();

// For drawing in a panel's own coordinates (the HUD boot): flushes are drawn a call each through vgui, which moves
// them by the panel's position. Batches are in screen pixels. Flushes what's queued first.
void NeoGhostPanelLocal(bool bPanelLocal);
