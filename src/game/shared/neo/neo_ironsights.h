#pragma once

// Optional ironsight presentation for the aim (ADS) state.
// Gameplay aim (spread, camera FOV, speed) is unchanged; this only decides where the
// viewmodel sits and how it moves while aiming. Weapons opt in with an "AimOffset" block
// in their weapon script; without one, or with cl_neo_ironsights 0, the traditional
// NT "ZoomOffset" pose is used.

#include "mathlib/vector.h"

class CNEOWeaponInfo;

struct NeoAimPose
{
	Vector pos;
	QAngle ang;
	float fov;
};

// True when this client should show the weapon's ironsight pose. Always false on the server.
bool NeoIronsightsActive(const CNEOWeaponInfo &data);

// The viewmodel pose to use at full aim: ironsight (possibly live-tuned) or traditional zoom.
NeoAimPose NeoGetAimPose(const CNEOWeaponInfo &data);

// Seconds for the hip <-> aim viewmodel transition.
float NeoAimTransitionTime(const CNEOWeaponInfo &data);

// Shapes the linear transition fraction (0..1) into the motion curve used for the viewmodel.
float NeoAimTransitionCurve(const CNEOWeaponInfo &data, float fraction);

// Scale for movement bob at the given ironsight blend (0 = hip, 1 = fully on the sights).
float NeoIronsightBobScale(float ironsightBlend);

// How much of the idle animation's motion to show at the given ironsight blend (1 = full idle).
// On the sights the idle sway shrinks to cl_neo_ironsight_idle of its size so the sight picture holds.
float NeoIronsightIdleScale(float ironsightBlend);

// Fire and recoil animations whose kick is damped on the sights.
bool NeoIronsightIsRecoilActivity(int activity);

#ifdef CLIENT_DLL
#include "studio.h"

// Model-space transform of a bone in a pose, built by walking its parent chain.
void NeoIronsightBoneToModel(CStudioHdr *hdr, int bone, const Vector pos[], const Quaternion q[], matrix3x4_t &out);

// A cached viewmodel pose: one sequence at one cycle. Ironsight damping uses the idle's first frame
// (the pose the sights are tuned in) and the fire animation's settled last frame.
struct NeoIronsightRestPose
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
// vertical kick scales by cl_neo_ironsight_recoil_vertical, sideways by cl_neo_ironsight_recoil_side,
// pushback by cl_neo_ironsight_recoil_back, within the cl_neo_ironsight_recoil_max_* leash. The weapon
// script's "IronsightRecoil" block adjusts these per weapon. Applied rigidly at the root bones.
void NeoIronsightDampRecoil(CStudioHdr *hdr, Vector pos[], Quaternion q[],
	const Vector settledPos[], const Quaternion settledQ[], int refBone, const CNEOWeaponInfo &data, float ironsightBlend);
#endif

#ifdef CLIENT_DLL
class IMaterial;

// True when crosshairs should be hidden: ironsights are on and the debug crosshair is off. Aiming
// while cloaked keeps it, since the cloaked viewmodel's sights are hard to see, unless the weapon has its
// own aiming aid that stays visible while cloaked (glowing sight dots or an optic).
bool NeoIronsightsHideCrosshair(bool bAiming, bool bCloaked, bool bHasCloakedAimAid);

// Hides the weapon's "IronsightHideMaterials" (e.g. an optic's lens) for its lifetime while the gun
// is on the sights. Wrap the viewmodel draw in one; the materials are restored when it goes out of scope.
class NeoIronsightHiddenMaterials
{
public:
	NeoIronsightHiddenMaterials(const CNEOWeaponInfo *pData, float ironsightBlend);
	~NeoIronsightHiddenMaterials();
private:
	static constexpr int MAX_MATERIALS = 8;
	IMaterial *m_materials[MAX_MATERIALS];
	int m_count = 0;
};
#endif
