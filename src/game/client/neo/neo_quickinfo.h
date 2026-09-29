#pragma once

// Quick info around the crosshair (QUICKINFO.md): the edge panel's integrity, therm-optic and aux brought to the
// centre as a housing: an integrity bar on top, a mirrored bracket either side holding each class's fill, four
// vision dots, fine instrument detail, layered depth with an organic sway. Static: a fixed size outside a fixed
// crosshair deadzone that the gunplay crosshair clamps to. Its own setting (Settings > HUD), not part of Enable
// Gunplay; on, it replaces the edge health / therm-optic / aux panel.

class C_NEO_Player;
class Color;

// The setting (cl_neo_hud_quickinfo).
bool NeoQuickInfoOn();

// The crosshair deadzone's size, pixels at 1080p from the aim (a circle; the SMG's box a square of the same half
// side), or 0 while the quick info is off: nothing of the gunplay crosshair leaves it.
float NeoQuickInfoDeadzone();

// Paints the housing around the crosshair at (x, y) in the crosshair's HUD pass, in the crosshair's colour.
void NeoQuickInfoPaint(C_NEO_Player *pPlayer, const Color &color, int x, int y);
