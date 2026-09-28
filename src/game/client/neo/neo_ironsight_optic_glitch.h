#pragma once

// The live optic of a disc lens ("lens_disc", e.g. the Jitte's) while the gun is off the sights: the
// view breaks up into coarse pixels with a choppy digital glitch (torn rows, dropped and tinted cells,
// cells spilling over the lens rim), refining into the clean view as the gun comes onto the sights.

class CNEOWeaponInfo;
class IMaterial;

// How clear the optic is for an ironsight blend (0 at the hip, 1 on the sights); below 1 it glitches.
float NeoOpticClarity(float ironsightBlend);

// Draws the glitched view on the lens circle: pLiveView (depth-tested) inside the lens, pSpill (drawn
// over the gun) outside it. centreAlpha and fadeStart fade it towards the rim as the clean disc does.
void NeoDrawOpticGlitch(IMaterial *pLiveView, IMaterial *pSpill, const matrix3x4_t &lensToWorld,
	const CNEOWeaponInfo &data, float clarity, float centreAlpha, float fadeStart);
