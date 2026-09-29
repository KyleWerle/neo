#pragma once

// Shared between the quick info's state (neo_quickinfo.cpp) and its drawing (neo_quickinfo_draw.cpp).

#include "neo_ghost_stroke.h"
#include "mathlib/vector2d.h"
#include "Color.h"

namespace NeoQuickInfo
{
// The housing, pixels at 1080p from the crosshair (y down). Outside the deadzone (DEADZONE): the brackets' feet
// start at x 88 and y 70, the bar spans x within its half-width at y -100.
constexpr float DEADZONE = 85.0f;
constexpr float BAR_Y = -100.0f;
constexpr float BAR_H = 4.0f;
constexpr float BX = 100.0f;			// the brackets' spines
constexpr float TOP = -70.0f, BOTTOM = 70.0f;
constexpr float FOOT = 12.0f;
constexpr float FILL_W = 5.0f;			// the fill inside each bracket
constexpr float FILL_GAP = 3.0f;
constexpr float DOT = 80.0f;			// the vision dots, on the diagonals
constexpr float JUMP_COST = 45.0f;		// SUPER_JMP_COST: a recon's jump cell

// The layers, far to near: each sways on its own spring by its depth.
enum Layer { LAYER_DETAIL, LAYER_BRACKET, LAYER_BAR, LAYER_LABELS, LAYER_DOTS, LAYER__COUNT };

// What each class's housing holds. Assault (and the VIP): therm-optic left, sprint right. Recon: therm-optic left,
// two jump cells right (its aux only pays for super jumps). Support: armour, integrity down both brackets.
// Juggernaut: sprint both sides.
enum Kind { KIND_RECON, KIND_ASSAULT, KIND_SUPPORT, KIND_JUGGERNAUT };

struct Chip { float from, to, time; };	// a hit's afterimage on the bar, as bar fractions
constexpr int MAX_CHIPS = 4;

struct QuickFrame
{
	Kind kind;
	Color color;
	float s;						// the screen's height over 1080
	NeoGhostPen pen;				// the boot's trace-in
	Vector2D centre;
	Vector2D sway[LAYER__COUNT];	// each layer's offset, pixels at 1080p
	float alpha;					// the fade (floor to full) times the boot's reveal
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
};

float BarHalf(Kind kind);
void PaintHousing(const QuickFrame &frame);
void PaintLabels(const QuickFrame &frame);
} // namespace NeoQuickInfo
