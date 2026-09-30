#pragma once

// Shared between the quick info's state (neo_quickinfo.cpp), its drawing helpers (neo_quickinfo_paint.cpp), the
// band (neo_quickinfo_draw.cpp), the ammo readout (neo_quickinfo_ammo.cpp) and the speed graph
// (neo_quickinfo_speed.cpp).

#include "neo_ghost_stroke.h"
#include "mathlib/vector2d.h"
#include "Color.h"

class C_NEO_Player;

namespace NeoQuickInfo
{
// The band, pixels at 1080p from the screen's centre (y down): under the chat box (whose bottom is 349 px below
// the centre at 1080p; the wing tips' tops are at 367) and above the compass's labels (from about 490). Top down:
// the ammo's header (weapon name, magazines), its round ticks (rounds left, fire mode), the integrity bar and the
// wings, integrity's number with the vision dots, the etched rail with the channel codes, the spawn labels.
constexpr float BAND_Y = 400.0f;					// the integrity bar's centre line
constexpr float BAR_HALF = 150.0f;
constexpr float BAR_H = 6.0f;
constexpr float WING_IN = 168.0f, WING_OUT = 380.0f;	// each wing's inner end and tip, either side
constexpr float WING_RISE = 24.0f;					// the tips sit this much higher than the inner ends
constexpr float WING_FRAME = 9.0f;					// the frame's half height around the wing's fill
constexpr float FILL_H = 8.0f;
constexpr float HEADER_Y = BAND_Y - 45.0f;			// the weapon's name and its magazines left
constexpr float TICKS_Y = BAND_Y - 19.0f;			// the round ticks
constexpr float TICK_H = 14.0f;
constexpr float TICK_HALF = 90.0f;					// the ammo's half width: clear of the wings' inner ends
constexpr int MAX_TICKS = 30;						// past this, a tick is several rounds
constexpr float NUMBER_Y = BAND_Y + 21.0f;			// integrity's number, under the bar
constexpr float DOT_NEAR = 34.0f, DOT_FAR = 46.0f;	// the vision dots, either side of the number
constexpr float RAIL_Y = BAND_Y + 44.0f;			// the etched rail
constexpr float CODES_Y = BAND_Y + 58.0f;			// the channel codes on it: at the left tip, centred, at the right tip
constexpr float LABELS_Y = BAND_Y + 82.0f;			// the spawn labels, each under its code
constexpr float MARK = 4.0f;						// a registration cross's half size
constexpr float JUMP_COST = SUPER_JMP_COST;		// a recon's jump cell

// The layers, far to near: each sways on its own spring by its depth.
enum Layer { LAYER_DETAIL, LAYER_FRAME, LAYER_BAR, LAYER_LABELS, LAYER_DOTS, LAYER__COUNT };

// What each class's wings hold. Assault (and the VIP): therm-optic left, sprint right. Recon: therm-optic left,
// two jump cells right (its aux only pays for super jumps). Support: armour, integrity along both wings.
// Juggernaut: sprint both sides.
enum Kind { KIND_RECON, KIND_ASSAULT, KIND_SUPPORT, KIND_JUGGERNAUT };

// The parts that fade on their own: each comes up when it's used or changes, holds, and settles back to the floor
// on its own (the rest, the chassis: frames, rail, codes, graduations, crosses, stays steady at the floor).
enum Part { PART_INTEGRITY, PART_LEFT, PART_RIGHT, PART_AMMO, PART_VISION, PART__COUNT };

// The dark backings (neo_quickinfo_backing.cpp): one behind the band, one behind the speed graph.
enum Backing { BACKING_BAND, BACKING_SPEED, BACKING__COUNT };
constexpr int MAX_BACKING_SPOTS = 5;

// The HUD's OCR faces, at 1080p: 17, 20 and 26 px tall.
enum Font { FONT_SMALLER, FONT_SMALL, FONT_LARGE };

struct Chip { float from, to, time; };	// a hit's afterimage on the bar, as bar fractions
constexpr int MAX_CHIPS = 4;

// The active weapon, as the ammo panel shows it (the band replaces that panel too).
struct Ammo
{
	bool bShown = false;			// a weapon, and the rules don't hide the ammo
	wchar_t name[48] = L"";
	const wchar_t *pMode = nullptr;	// AUTO, SEMI, BUCK, SLUG, THROW; none for the ghost and melee
	int rounds = 0, maxRounds = 0;	// maxRounds 0: the name alone
	wchar_t bullet = 0;				// the stock panel's glyph for a round (NHudBullets, NOCR); 0: none (ticks)
	bool bHeat = false;				// the BALC: a heat meter in the ticks' row
	float heat = 0.0f;				// 0 to 1
	bool bOverheated = false;
	wchar_t mags[16] = L"";			// magazines left, or the Supa 7's shells + slugs; empty for none
	bool bMagsOut = false;
};

struct QuickFrame
{
	Kind kind;
	Color color;
	float s;						// the screen's height over 1080
	NeoGhostPen pen;				// the boot's trace-in
	Vector2D centre;				// the screen's
	Vector2D sway[LAYER__COUNT];	// each layer's offset, pixels at 1080p
	float alpha;					// what's drawing now: the chassis's fade (or a part's) times the boot's reveal
	float parts[PART__COUNT];		// each part's fade times the boot's reveal
	float reveal;					// the boot's reveal alone
	float now;
	bool bDetail;

	float hp;						// integrity, 0 to 1 of the class's maximum, eased
	int hpNumber;					// as the HUD displays it (cl_neo_hud_health_mode)
	float hpChangeTime;			// the number flickers a moment after a change
	float glitchTime;				// a hit's two-frame slice hitch
	const Chip *pChips; int chips;
	float cloak;					// therm-optic, 0 to 1, eased
	bool bCloaked;
	float aux;						// 0 to 100, eased
	bool bSprinting;
	bool bVision, bHasVision;
	float jumpReady[2], jumpSpent[2];	// times a recon's cell locked and was spent
	float labels;					// seconds since the spawn labels began (negative: none)
	const char *pVisionName;
	Ammo ammo;
};

// Drawing helpers (neo_quickinfo_paint.cpp). Positions are pixels at 1080p from f.centre, on a layer that sways.
Vector2D At(const QuickFrame &f, Layer layer, float x, float y);
int Alpha(const QuickFrame &f, float a);
void Line(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, NeoGhostWeight weight, const Color &c, float a);
void Box(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, const Color &c, float a);
// A filled strip h tall along a slant from (x0, y0) to (x1, y1), its ends upright.
void Strip(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, float h, const Color &c, float a);
// Text with its vertical middle at y, aligned by align (-1 ending at x, 0 centred, 1 starting at x), shadowed so it
// reads on anything; count characters of it. Returns its width, pixels at 1080p.
float Text(const QuickFrame &f, Layer layer, const wchar_t *pText, int count, float x, float y, int align, Font font,
	const Color &c, float a);
// A registration mark: a small cross.
void Cross(const QuickFrame &f, Layer layer, float x, float y, float a);
// The frame drawing at another strength (a part's).
QuickFrame WithAlpha(const QuickFrame &f, float alpha);

extern const Color WARN;

// A backing's opacity for how bright the scene is behind it, measured through its spots (screen pixels); call once
// a frame each, in order (they share one ray a frame). And the backing: a feathered rounded blob, 1080p units.
float BackingAlpha(Backing backing, C_NEO_Player *pPlayer, const Vector2D *pSpots, int spots, float dt, bool bBoot);
void PaintBacking(const QuickFrame &f, Layer layer, const Vector2D &centre, const Vector2D &inner, const Vector2D &feather, float alpha);

void ReadAmmo(C_NEO_Player *pPlayer, Ammo &ammo);
void PaintBand(const QuickFrame &frame);
void PaintAmmo(const QuickFrame &frame);
// The speed graph, bottom left where the health panel was: it keeps its own samples, so call it every frame the
// band draws (bBoot clears them).
void PaintSpeed(const QuickFrame &frame, C_NEO_Player *pPlayer, float dt, bool bBoot, float floorAlpha);
void PaintLabels(const QuickFrame &frame);
} // namespace NeoQuickInfo
