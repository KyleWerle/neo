#pragma once

// Where a weapon's lens is ("lens_bone", "lens_map", "lens_map2", "lens_circle" in its IronsightOptic
// block), and seeing through sight glass ("window"). Shared by the optic's camera (neo_ironsight_optic.cpp)
// and the lens drawing (neo_ironsight_optic_disc.cpp).

class C_BaseAnimating;
class CNEOWeaponInfo;
class CViewSetup;

// One pane of the lens in world space: point(u, v) = origin + u * uAxis + v * vAxis, in lens UV.
struct NeoLensPane
{
	Vector origin, u, v;

	// The point at lens UV (u, v).
	Vector At(float lensU, float lensV) const { return origin + u * lensU + v * lensV; }
	// The centre of the lens ("lens_circle").
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

// For "window" optics: points the optic camera from the eye at the sight glass and sizes it to just
// cover the glass, remembering the projection so the glass can look up exactly what lies behind each
// of its points. False if the glass is not in front of the eye.
bool NeoIronsightWindowCamera(const CViewSetup &mainView, const CNEOWeaponInfo &data, QAngle &angles, float &fov);

// Whether this frame's window camera ran, so the glass can look up the view behind it.
bool NeoIronsightWindowReady();

// Where the world behind a point of the glass lies in the window camera's view, in texture coordinates.
void NeoIronsightWindowTexCoord(const Vector &world, float &u, float &v);

// Why the window camera last failed ("" if it didn't), for cl_neo_ironsight_optic_debug.
const char *NeoIronsightWindowDebugReason();
