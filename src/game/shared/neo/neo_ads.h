#pragma once

// Aim down sights (ADS): an optional viewmodel presentation of the aim state.
// Gameplay aim (spread, camera FOV, speed) is unchanged; this only decides where the
// viewmodel sits and how it moves while aiming. Weapons opt in with "enabled" "1" in the
// "AimOffset" block of their weapon script; without it, or with cl_neo_ads 0, the traditional
// NT "ZoomOffset" pose is used.

#include "mathlib/vector.h"

class CNEOWeaponInfo;

struct NeoAimPose
{
	Vector pos;
	QAngle ang;
	float fov;
};

// True when this client should show the weapon's ADS pose. Always false on the server.
bool NeoAdsActive(const CNEOWeaponInfo &data);

// The viewmodel pose to use at full aim: ADS (possibly live-tuned) or traditional zoom.
NeoAimPose NeoGetAimPose(const CNEOWeaponInfo &data);

// Seconds for the hip <-> aim viewmodel transition.
float NeoAimTransitionTime(const CNEOWeaponInfo &data);

// Shapes the linear transition fraction (0..1) into the motion curve used for the viewmodel.
float NeoAimTransitionCurve(const CNEOWeaponInfo &data, float fraction);

// The smoothstep ease, 0 to 1 over t in [0, 1] (clamped), flat at both ends.
inline float NeoSmoothStep(float t)
{
	t = clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// Scale for movement bob at the given ADS blend (0 = hip, 1 = fully on the sights).
float NeoAdsBobScale(float adsBlend);

// How much of the idle animation's motion to show at the given ADS blend (1 = full idle).
// On the sights the idle sway shrinks to cl_neo_ads_idle of its size so the sight picture holds.
float NeoAdsIdleScale(float adsBlend);

// Fire and recoil animations whose kick is damped on the sights.
bool NeoAdsIsRecoilActivity(int activity);

#ifdef CLIENT_DLL
#include "studio.h"

// A cached viewmodel pose: one sequence at one cycle. ADS damping uses the idle's first frame
// (the pose the sights are tuned in) and the fire animation's settled last frame.
struct NeoAdsRestPose
{
	const studiohdr_t *pModel = nullptr;
	int sequence = -1;
	float cycle = -1.0f;
	Vector pos[MAXSTUDIOBONES];
	QuaternionAligned q[MAXSTUDIOBONES];

	// Rebuilds the pose if the model, sequence or cycle changed since the last call.
	void Update(CStudioHdr *hdr, int poseSequence, float poseCycle, const float poseparam[]);
};

// Damps the fire animation on the sights: only the kick relative to the fire animation's settled
// last frame (settledPos/settledQ) shrinks; the settled pose is left as animated. The gun's (refBone)
// vertical kick scales by cl_neo_ads_recoil_vertical, sideways by cl_neo_ads_recoil_side,
// pushback by cl_neo_ads_recoil_back, within the cl_neo_ads_recoil_max_* leash. The weapon
// script's "AdsRecoil" block adjusts these per weapon. Applied rigidly at the root bones.
void NeoAdsDampRecoil(CStudioHdr *hdr, Vector pos[], Quaternion q[],
	const Vector settledPos[], const Quaternion settledQ[], int refBone, const CNEOWeaponInfo &data, float adsBlend);

// True when crosshairs should be hidden: this weapon is using its ADS pose and the crosshair option is off.
// Weapons without an ADS pose keep the standard crosshair. Aiming
// while cloaked keeps it, since the cloaked viewmodel's sights are hard to see, unless the weapon has an
// optic: its glass stays see-through while cloaked.
bool NeoAdsHideCrosshair(const CNEOWeaponInfo &data, bool bAiming, bool bCloaked);

class IMaterial;

// How far onto the sights (the viewmodel's ADS blend) the gun counts as on them: a scope's housing behind the
// glass stops drawing from here.
constexpr float NEO_ADS_ON_SIGHTS = 0.5f;

// Hides an optic's glass ("one_pane" in its "AdsOptic" block) for its lifetime, since the optic draws that glass's art itself
// (neo_ads_optic.h). Wrap the viewmodel draw in one; the material is restored when it goes out of scope.
class NeoAdsHiddenMaterials
{
public:
	NeoAdsHiddenMaterials(const CNEOWeaponInfo *pData);
	~NeoAdsHiddenMaterials();
private:
	IMaterial *m_pLens = nullptr;
};
#endif
