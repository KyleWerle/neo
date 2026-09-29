#pragma once

// Quick info (QUICKINFO.md): the corner panels' integrity, therm-optic, aux and ammo brought in to the edge of your
// view as a low band below the crosshair: the ammo (name, magazines, round ticks, fire mode) over an integrity bar,
// its number and four vision dots under it, a wing either side holding each class's fill, fine instrument detail on
// an etched rail, layered depth with an organic sway; and a speed graph bottom left. Fixed to the screen at a fixed
// size, well clear of every crosshair and of the chat. Its own setting (Settings > HUD), not part of Enable
// Gunplay; on, it replaces the health / therm-optic / aux and ammo panels and nothing else.

class C_NEO_Player;
class Color;

// The setting (cl_neo_hud_quickinfo).
bool NeoQuickInfoOn();

// On and drawn (or held back only by a screen fade) this frame or the last: the ammo panel gives way to it. Off, or
// hidden (dead, spectating, the rules), the stock panels stay.
bool NeoQuickInfoShowing();

// Paints the band in the crosshair's HUD pass, in the crosshair's colour.
void NeoQuickInfoPaint(C_NEO_Player *pPlayer, const Color &color);
