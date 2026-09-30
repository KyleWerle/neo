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
// cl_neo_hud_backing. The GPU backing darkens only what's bright by itself, so it stays near opaque (the blur shows
// in the dark too).
float NeoHudBackingOpacity(float brightness, float darkOpacity, float brightOpacity);

// A feathered dark backing in screen pixels: a rounded rectangle (a superellipse) of half size half round centre,
// solid inside and fading to nothing over feather, with points round it (the unit shape is cached per count).
void NeoHudPaintBacking(const Vector2D &centre, const Vector2D &half, const Vector2D &feather, float alpha, int points);

// The unit superellipse a backing is drawn round, points round it (cached per count).
const Vector2D *NeoHudBackingShape(int points);

// The GPU backing (HUD-SYSTEM.md step 4, cl_neo_hud_backing_gpu): the backings drawn with a shader over a quarter-size
// copy of the finished frame (made once a frame, on the first backing), the scene behind blurred, its bright pixels
// pulled down toward a target and dark ones left be, with a dithered, glitching cyberbrain noise over it. Off, or
// without shader model 3, the backings are the flat dark blobs.
bool NeoHudBackingGpu();

// How a GPU backing looks; the flat blob ignores it. The glitch and blur are 0 to 1, times their settings.
struct NeoHudBackingLook
{
	float glitch = 0.4f;	// dither, cloud, scanlines and torn rows
	float blur = 1.0f;		// how wide the blur reaches
	float seed = 0.0f;		// so neighbours' noise differs
	float motion = 0.0f;	// 0 still (nothing drifts or tears) to 1 full: the outline morphs, blocks glitch out of place
};
void NeoHudPaintBacking(const Vector2D &centre, const Vector2D &half, const Vector2D &feather, float alpha, int points,
	const NeoHudBackingLook &look);
