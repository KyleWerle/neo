#pragma once

// The HUD's style and what it takes over (HUD-SYSTEM.md, the core layer). One switch, cl_neo_hud_style (Settings >
// HUD), picks who draws; the stock panels ask here whether a style draws their part now, and step aside if it does.

enum NeoHudStyle
{
	NEO_HUD_STYLE_ORIGINAL = 0,		// the stock NT panels, for players who want the original HUD
	NEO_HUD_STYLE_COMPACT,			// the cyberbrain (neo_cyberbrain.h), its ring small at the bottom centre
	NEO_HUD_STYLE_BODY,				// the cyberbrain, its ring on the body group's ground disc
	NEO_HUD_STYLE_RACER,			// the quick info band (neo_quickinfo.h)
	NEO_HUD_STYLE_COMPETITIVE,		// the original layout pared down: lowercase Neuropol2 text only (ui/neo_hud_competitive.*)

	NEO_HUD_STYLE__COUNT
};
NeoHudStyle NeoHudStyleCurrent();
// The cyberbrain's two layouts (Compact and On the body).
bool NeoHudCyberbrainStyle(NeoHudStyle style);

// Whether a style draws a stock panel's part now. Asked directly each time (the style, and you alive in your own
// eyes or on a team), not "did it draw last frame", which let the stock panels flash through after a spawn.
// The vitals: the health / therm-optic / aux panel and the ammo panel.
bool NeoHudVitalsReplaced();
// The compass (the cyberbrain's ring, Competitive's heading).
bool NeoHudCompassReplaced();
// The ghost's uplink state (the cyberbrain's uplink).
bool NeoHudUplinkReplaced();
// The team side, dead included: the round state and the death notice keep running (the spectator commands' player
// order, the countdown beep, the kills the scoreboard marks) and only stop drawing. A spectator keeps them.
bool NeoHudTeamReplaced();

// Calls pfnChanged whenever the style changes (a style tidying what it moved, as the cyberbrain's chat). Register at
// startup; a few listeners at most.
void NeoHudOnStyleChange(void (*pfnChanged)(NeoHudStyle style));

// How much of the view a screen fade leaves showing (1 none, 0 black): the spawn's fade in, a map's env_fade (the
// firing range fades to black and back), a map's black screen overlay. The HUD paints over it, so our parts fade with
// it. Worked out once a frame.
float NeoHudFadeVisible();
// Faded all but black (visible under NEO_HUD_FADE_HIDDEN): the crosshair layer, the sight ghost, the styles wait.
constexpr float NEO_HUD_FADE_HIDDEN = 0.05f;
bool NeoHudFadedOut();
