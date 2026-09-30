#pragma once

#include <vgui/VGUI.h>

class Color;

// The HUD's shared drawing, under every style (HUD-SYSTEM.md, the core layer): the ghost's batched strokes
// (neo_ghost_stroke.h), and here fonts and text. The styles keep their own font tables and layouts; the scheme lookup
// and the printing live here, once.

// The client scheme's face pName, looked up into cache the first time (pFallback if the scheme has no pName). The
// HUD's faces live in the client scheme: the default scheme is the engine's, without them, and nothing would draw.
vgui::HFont NeoHudSchemeFont(vgui::HFont &cache, const char *pName, const char *pFallback = nullptr);

enum NeoHudTextEdge
{
	NEO_HUD_TEXT_PLAIN,		// no edge
	NEO_HUD_TEXT_SHADOW,	// a dark copy 1 px down and right
	NEO_HUD_TEXT_EDGED,		// a dark edge all round: the shadow and the four sides
};

// count characters of pText with its top left at (x, y), whole pixels, in c (its alpha included), over a dark edge
// at edgeAlpha (0 to 255). Queued strokes stay queued: flush first to put them under the text.
void NeoHudPrintText(vgui::HFont font, const wchar_t *pText, int count, int x, int y, const Color &c, NeoHudTextEdge edge,
	int edgeAlpha);
