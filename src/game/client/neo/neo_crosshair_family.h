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
	Vector2D aimOffset;		// the aim crosshair's damped, capped offset from the centre (neo_gunplay_aim.h)
	float link;				// 0 to 1: how locked together the layers are (close, spread recovered, gun ready), eased
	float sinceLock;		// seconds since the layers locked (-1 while not locked)
	float pixelsPerTangent;	// screen pixels per tangent off the aim (the view's field of view, a window's zoom)
	Vector2D jitter;		// this frame's shot scramble
	float spread;			// the spread cone's edge, eased (and popped by each shot)
	float spreadExact;		// the spread cone's edge as it is, in pixels (the spread ghost's)
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

// A critically damped spring's step toward target (omega: 1 over its time constant), in substeps: stepped once a
// frame, a stiff spring runs away on long frames (alt-tabbed, the engine sleeps each frame) and the linework flew
// off and vanished.
template <typename T>
inline void NeoCrosshairSpring(T &value, T &velocity, const T &target, float omega, float dt)
{
	constexpr float STEP = 1.0f / 240.0f;
	for (float left = dt; left > 0.0f; left -= STEP)
	{
		const float step = Min(left, STEP);
		velocity += ((target - value) * (omega * omega) - velocity * (2.0f * omega)) * step;
		value += velocity * step;
	}
}

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

// The settled form (GUNPLAY-PLAN.md, three layers): a family's aim crosshair (layer 1) at `at`, and the bridges
// from its docking points to the spread view's (layer 3). bGlyph false: the aim crosshair is off, and the spread
// view locks onto the centre (the bridges still draw, from just off it). Each family's is in its own file.
void NeoCrosshairAimRifle(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph);
void NeoCrosshairAimSmg(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph);
void NeoCrosshairAimMg(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph);
void NeoCrosshairAimShotgun(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph);
void NeoCrosshairAimPistol(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph);
void NeoCrosshairAimScoped(const NeoCrosshairFrame &frame, const Vector2D &at, bool bGlyph);

// A bridge between a docking point on the aim crosshair and one on the spread view: traced in from both ends as
// the layers link, meeting in the middle when locked; heavy for a moment when they lock.
void NeoCrosshairBridge(const NeoCrosshairFrame &frame, const Vector2D &inner, const Vector2D &outer,
	NeoGhostWeight weight = NEO_GHOST_MEDIUM);

// Where the spread view's parts near the gun sit this frame (the centre, the gun's turn, the shot's scramble).
inline Vector2D NeoCrosshairNear(const NeoCrosshairFrame &frame)
{
	return frame.centre + frame.deviation + frame.jitter;
}
