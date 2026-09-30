#pragma once

// The ghost's enemy callouts (HUD-SYSTEM.md, the models layer). The stock compass keeps them (neo_hud_compass.cpp) but
// hides in the new styles, so the HUD keeps its own copy from the same game event, listened for here in every style:
// one per spotted player, restarted each time the ghost calls them out again, for as long as the compass would show
// it (cl_neo_ghost_callout_compass_time).

namespace NeoHud
{
struct Callout { float yaw; float metres; float age; float life; };
constexpr int MAX_CALLOUTS = 32;

// The live callouts as seen from the view now; returns how many, newest the youngest's index (-1: none).
int ReadCallouts(float now, Callout out[MAX_CALLOUTS], int &newest);
void ResetCallouts();
} // namespace NeoHud
