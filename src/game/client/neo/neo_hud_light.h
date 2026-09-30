#pragma once

class C_NEO_Player;
class Vector2D;

// Light for the HUD (HUD-SYSTEM.md, the core layer): how bright the scene is behind the HUD, and the feathered dark
// backings that keep the linework readable on bright scenes. The brightness is one ray at a time into the world for
// now; the GPU path (a small copy of the finished frame) replaces it here, without the styles changing.

// Whether the backings draw at all (cl_neo_hud_backing above 0).
bool NeoHudBackingsOn();

// How bright the scene looks through a screen pixel (0 black, 1 white): one ray, the world's light where it lands
// (sky counts as bright), times the view's auto exposure on HDR maps.
float NeoHudSceneBrightness(C_NEO_Player *pPlayer, const Vector2D &pixel);

// Eases a region's brightness toward what its spots measured, the bright spots counting most (a patch of sky behind
// half a group still needs the backing); on a boot it goes straight there.
void NeoHudEaseBrightness(float &brightness, const float *pSamples, int count, float dt, bool bBoot);

// A backing's opacity for the brightness behind it: darkOpacity in the dark up to brightOpacity in daylight, times
// cl_neo_hud_backing.
float NeoHudBackingOpacity(float brightness, float darkOpacity, float brightOpacity);

// A feathered dark backing in screen pixels: a rounded rectangle (a superellipse) of half size half round centre,
// solid inside and fading to nothing over feather, with points round it (the unit shape is cached per count).
void NeoHudPaintBacking(const Vector2D &centre, const Vector2D &half, const Vector2D &feather, float alpha, int points);
