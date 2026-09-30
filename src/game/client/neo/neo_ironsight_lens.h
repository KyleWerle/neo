#pragma once

// Where a weapon's lens is ("lens_bone", "lens_map", "lens_map2" in its IronsightOptic
// block), for the glass drawing (neo_ironsight_optic_disc.cpp) and the collimated dots.

class C_BaseAnimating;
class CNEOWeaponInfo;
class CViewSetup;

// One pane of the lens in world space: point(u, v) = origin + u * uAxis + v * vAxis, in lens UV.
struct NeoLensPane
{
	Vector origin, u, v;

	// The point at lens UV (u, v).
	Vector At(float lensU, float lensV) const { return origin + u * lensU + v * lensV; }
	// The centre of the glass ("window_glass", else "lens_circle").
	Vector Centre(const CNEOWeaponInfo &data) const;
};

// The lens pane nearer the eye ("lens_map" or "lens_map2"), from the viewmodel's drawn pose, and the other
// pane in pFarPane if there are two (else it is left alone). False if the lens bone isn't on the model.
bool NeoIronsightLensPane(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, const Vector &eye, NeoLensPane &pane,
	NeoLensPane *pFarPane = nullptr);

// How much larger things look in the main view than in the viewmodel's: the tangent of the main view's
// half field of view over the viewmodel's. A point of the gun appears on screen where the world along its
// eye-space ray, scaled sideways by this, does.
float NeoIronsightFovScale(const CViewSetup &view);
