#pragma once

// The cyberbrain HUD (HUD-REDESIGN.md): receptor groups at attention depths round the edge of your view (body,
// optics, weapon, link), each coming in toward the focus when what it senses matters and letting go slowly, and a
// surround ring hearing the full circle round you. One HUD element (ui/neo_hud_cyberbrain.*); the stock panels step
// aside for what it draws.

#include "neo_hud_style.h"

// A cyberbrain style, you alive in your own eyes: its vitals draw (the stock panels ask neo_hud_style.h).
bool NeoCyberbrainShowing();

// Keeps the chat in its cyberbrain place (up under the squad list), called each frame the cyberbrain draws. The team's
// side (ui/neo_hud_cyberbrain_team.*): the score, the squad list and the kill feed in the cyberbrain's language; it
// stays up while you're dead (it's about the match, not your body).
void NeoCyberbrainPlaceChat();
