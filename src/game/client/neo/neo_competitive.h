#pragma once

// The Competitive HUD (cl_neo_hud_style 4; HUD-REDESIGN.md, "Competitive"): the original layout, pared down to
// lowercase text in NT's NOCR, with nothing else: no boxes, bars, textures or animation, so it costs next to nothing
// to draw and reads the same on every map. It shows everything the stock panels show, in their places: the vitals
// bottom left, the ammo bottom right, the compass bottom centre (with the objective, the ghost's callouts and the
// rangefinder), the round top centre, the squad top left, the kill feed top right (its weapon marks stay NT's killfeed
// glyphs, a font too). Players' names keep their case; everything else is lowercase.

#include "mathlib/vector2d.h"
#include "Color.h"

namespace NeoCompetitive
{
constexpr float EDGE = 24.0f;	// pixels at 1080p from the screen's edges, as the stock panels sit

enum Face { FACE_TEXT, FACE_LARGE, FACE_ICONS, FACE__COUNT };

struct Pen
{
	float s;		// the screen's height over 1080
	int wide, tall;
};

// Text with a one-pixel shadow, its top at y: align -1 ending at x, 0 centred, 1 starting at x. Lowercased unless
// bKeepCase (names). Returns its width.
float Print(const Pen &pen, const wchar_t *pText, float x, float y, int align, Face face, const Color &c, bool bKeepCase = false);
float Width(const wchar_t *pText, Face face, bool bKeepCase = false);
float Height(Face face);

// The stock colours: text, and the panels' boxes (the grey the stock panels sit on; the feed's dark entries).
extern const Color WHITE, FADED, RED, BOX, FEED_BOX;
constexpr float BOX_PAD = 8.0f;	// pixels at 1080p round the text

// The stock panels' grey rounded box, pixels (flush: the top corners square, as the round's box at the screen's top).
void Box(float x0, float y0, float x1, float y1, const Color &c = BOX, bool bFlushTop = false);

// The vitals, ammo, compass and rangefinder (neo_competitive_vitals.cpp), while you're alive.
void PaintVitals(const Pen &pen, bool bHealth, bool bAmmo, bool bCompass);
// The round, the squad and the kill feed (neo_competitive_team.cpp), while you're on a team.
void PaintRound(const Pen &pen);
void PaintSquad(const Pen &pen);
void PaintFeed(const Pen &pen);
} // namespace NeoCompetitive
