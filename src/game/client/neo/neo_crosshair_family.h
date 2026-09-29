#pragma once

// The gunplay crosshair's families (GUNPLAY-PLAN.md): each draws its own animated linework around the player's
// crosshair from one shared frame of inputs, built by neo_gunplay_crosshair.cpp. A family's painter lives in its
// own neo_crosshair_<family>.cpp; one not designed yet draws the rifle's.

#include "neo_ghost_stroke.h"
#include "mathlib/vector2d.h"
#include "Color.h"

class C_NEO_Player;
class C_NEOBaseCombatWeapon;

enum NeoCrosshairFamily
{
	NEO_CROSSHAIR_RIFLE,	// ZR68C/S, MX, MX-S, M41, M41S
	NEO_CROSSHAIR_SMG,		// MPN, MPN-S, SRM, SRM-S, Jitte, Jitte-S, SMAC
	NEO_CROSSHAIR_MG,		// PZ, BALC, PBK56S
	NEO_CROSSHAIR_SHOTGUN,	// Supa 7, AA13
	NEO_CROSSHAIR_PISTOL,	// Tachi, Milso, Kyla
	NEO_CROSSHAIR_SCOPED,	// ZR68L, M41L, SRS

	NEO_CROSSHAIR_FAMILY__TOTAL
};

NeoCrosshairFamily NeoCrosshairFamilyOf(const C_NEOBaseCombatWeapon *pWeapon);

// One frame of what the crosshair shows, in screen pixels.
struct NeoCrosshairFrame
{
	C_NEO_Player *pPlayer;
	C_NEOBaseCombatWeapon *pWeapon;
	Color color;
	NeoGhostPen pen;		// scale (screen height / 1080) and the trace-in (booting)
	float s;				// pen.scale, for sizes given at 1080p
	Vector2D centre;		// the aim point, steady
	Vector2D deviation;		// the gun's turn from the aim (recoil knock, spread pivot), for the parts near the gun
	Vector2D jitter;		// this frame's shot scramble
	float spread;			// the spread cone's edge, eased
	float aim;				// 0 hip to 1 aimed, eased
	float alpha;			// the layer's opacity, 0 to 1 (the scramble's flicker included)
	float sinceBoot;		// seconds since the layer came online (a weapon switch, or it was hidden)
	float sinceShot;		// seconds since the last shot
	float scramble;			// 0 to 1: the last shot's scramble, fading
	int clip, maxClip;		// rounds (-1 if the gun has no clip)
	float cycle;			// seconds between shots
	float ready;			// 0 to 1: how far the gun has cycled toward its next shot
	float dt;				// seconds since the last frame drawn (0 on the first)
	bool bBoot;				// the first frame after coming online: a family resets its own animation
	bool bShot;				// a shot this frame

	int Alpha(float opacity) const;	// 0-255, of the layer's opacity
};

// A dot: a stroke about its own width long.
inline void NeoCrosshairDot(const NeoCrosshairFrame &frame, const Vector2D &at, NeoGhostWeight weight)
{
	const Vector2D half(0.6f * frame.s, 0.0f);
	NeoGhostStroke(frame.pen, at - half, at + half, weight);
}

// Typed in behind a cursor, as the sight ghost's readouts: text at (x, y) (its left and vertical middle), in the
// frame's colour at opacity (0 to 1), with typed (0 to 1) of it shown.
void NeoCrosshairReadout(const NeoCrosshairFrame &frame, const Vector2D &at, const wchar_t *pText, float typed,
	float opacity);
int NeoCrosshairReadoutTall();

// The families' painters.
void NeoCrosshairPaintRifle(const NeoCrosshairFrame &frame);
void NeoCrosshairPaintSmg(const NeoCrosshairFrame &frame);
void NeoCrosshairPaintMg(const NeoCrosshairFrame &frame);
void NeoCrosshairPaintShotgun(const NeoCrosshairFrame &frame);
void NeoCrosshairPaintPistol(const NeoCrosshairFrame &frame);
void NeoCrosshairPaintScoped(const NeoCrosshairFrame &frame);
