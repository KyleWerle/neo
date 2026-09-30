#pragma once

// The cyberbrain HUD (HUD-REDESIGN.md): receptor groups at attention depths round the edge of your view (body,
// optics, weapon, link), each coming in toward the focus when what it senses matters and letting go slowly, and a
// surround ring hearing the full circle round you. One HUD element (ui/neo_hud_cyberbrain.*); the stock panels step
// aside for what it draws.

// The HUD style (cl_neo_hud_style, Settings > HUD).
enum NeoHudStyle
{
	NEO_HUD_STYLE_ORIGINAL = 0,		// the stock NT panels, for players who want the original HUD
	NEO_HUD_STYLE_COMPACT,			// the cyberbrain, its ring small at the bottom centre
	NEO_HUD_STYLE_BODY,				// the cyberbrain, its ring on the body group's ground disc
	NEO_HUD_STYLE_RACER,			// the quick info band (neo_quickinfo.h), until the cyberbrain covers it

	NEO_HUD_STYLE__COUNT
};
NeoHudStyle NeoHudStyleCurrent();

// A cyberbrain style, and drawn this frame or the last: the health / therm-optic / aux panel, the ammo panel and the
// compass give way to it. Off, or hidden (dead, spectating, the rules), they stay.
bool NeoCyberbrainShowing();

// The team's side of it (ui/neo_hud_cyberbrain_team.*): the score, the squad list and the kill feed in the cyberbrain's
// language, drawn this frame or the last. It stays up while you're dead (it's about the match, not your body); for a
// spectator, or in the other styles, the stock elements draw. The stock round state and death notice keep running (the
// spectator commands' player order, the countdown beep, the kills the scoreboard marks) and only stop drawing.
bool NeoCyberbrainTeamShowing();
