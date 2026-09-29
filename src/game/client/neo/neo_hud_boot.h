#pragma once

// The HUD boot (GUNPLAY-PLAN.md): when a HUD panel comes up (the player spawning, or the panel showing again after
// a while hidden) it boots over a moment: a scan line sweeps down it and reveals it, a loading ring spins in its
// corner, a tech line in the NT boot sequence's voice slides in from far off, and a small cellular automaton ticks
// beside it, then all of it clears. With Enable Gunplay; cl_neo_hud_boot turns it off.

namespace vgui
{
class Panel;
}

// A panel's boot state, kept by the panel (CNEOHud_ChildElement).
struct NeoHudBootState
{
	float bootStart = -100.0f;
	float lastPaint = -100.0f;
	int line = 0;	// which tech line it shows
};

// Paints the boot over the panel (in its own coordinates, after the panel drew itself), starting one when due.
void NeoHudBootPaint(vgui::Panel *pPanel, NeoHudBootState &state);
