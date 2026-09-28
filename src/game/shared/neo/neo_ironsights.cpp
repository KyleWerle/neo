#include "cbase.h"
#include "neo_ironsights.h"
#include "neo_player_shared.h"
#include "neo_weapon_parse.h"

#ifdef CLIENT_DLL
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "filesystem.h"
#include "studio.h"
#include "bone_setup.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
ConVar cl_neo_ironsights("cl_neo_ironsights", "0", FCVAR_ARCHIVE,
	"Aim down the weapon's sights instead of the traditional NT aim pose, for weapons that define one.", true, 0, true, 1);
ConVar cl_neo_ironsight_time("cl_neo_ironsight_time", "0.2", FCVAR_ARCHIVE,
	"Seconds for the viewmodel to move between hip and ironsight.", true, 0.01f, true, 1.0f);

ConVar cl_neo_ironsight_bob("cl_neo_ironsight_bob", "0.1", FCVAR_ARCHIVE,
	"Movement bob scale while on the sights (1 = same as hip).", true, 0, true, 1);
ConVar cl_neo_ironsight_idle("cl_neo_ironsight_idle", "0.02", FCVAR_ARCHIVE,
	"Idle animation sway scale while on the sights (1 = same as hip).", true, 0, true, 1);
ConVar cl_neo_ironsight_recoil_vertical("cl_neo_ironsight_recoil_vertical", "0.05", FCVAR_ARCHIVE,
	"Fire animation vertical kick scale on the sights: rise and pitch (1 = same as hip).", true, 0, true, 1);
ConVar cl_neo_ironsight_recoil_side("cl_neo_ironsight_recoil_side", "1", FCVAR_ARCHIVE,
	"Fire animation sideways kick scale on the sights: drift, yaw and roll (1 = same as hip).", true, 0, true, 1);
ConVar cl_neo_ironsight_recoil_back("cl_neo_ironsight_recoil_back", "0.4", FCVAR_ARCHIVE,
	"Fire animation pushback scale on the sights (1 = same as hip).", true, 0, true, 1);
ConVar cl_neo_ironsight_recoil_max_dist("cl_neo_ironsight_recoil_max_dist", "1", FCVAR_ARCHIVE,
	"Leash on the sights: furthest the gun may move from its idle position, in units.", true, 0, false, 0);
ConVar cl_neo_ironsight_recoil_max_angle("cl_neo_ironsight_recoil_max_angle", "2", FCVAR_ARCHIVE,
	"Leash on the sights: furthest the gun may rotate from its idle angle on each axis, in degrees.", true, 0, false, 0);
ConVar cl_neo_ironsight_recoil_debug("cl_neo_ironsight_recoil_debug", "0", FCVAR_NONE,
	"Print the fire animation's raw and damped gun motion while on the sights.", true, 0, true, 1);
ConVar cl_neo_ironsight_crosshair("cl_neo_ironsight_crosshair", "0", FCVAR_NONE,
	"Debug: show crosshairs while ironsights are enabled (they are hidden otherwise).", true, 0, true, 1);

// Live tuning: with cl_neo_ironsight_tune 1 the pose below replaces the weapon script's AimOffset,
// so offsets can be dialled in while looking down the sights. The pose loads from each weapon as you
// switch to it; cl_neo_ironsight_save appends the tuned pose to ironsight_tuning.txt in the game dir,
// ready to merge into the weapon scripts.
ConVar cl_neo_ironsight_tune("cl_neo_ironsight_tune", "0", FCVAR_NONE, "Use the cl_neo_ironsight_* pose instead of the weapon script.", true, 0, true, 1);
ConVar cl_neo_ironsight_forward("cl_neo_ironsight_forward", "0", FCVAR_NONE, "Tuning: ironsight forward offset.");
ConVar cl_neo_ironsight_right("cl_neo_ironsight_right", "0", FCVAR_NONE, "Tuning: ironsight right offset.");
ConVar cl_neo_ironsight_up("cl_neo_ironsight_up", "0", FCVAR_NONE, "Tuning: ironsight up offset.");
ConVar cl_neo_ironsight_pitch("cl_neo_ironsight_pitch", "0", FCVAR_NONE, "Tuning: ironsight pitch offset.");
ConVar cl_neo_ironsight_yaw("cl_neo_ironsight_yaw", "0", FCVAR_NONE, "Tuning: ironsight yaw offset.");
ConVar cl_neo_ironsight_roll("cl_neo_ironsight_roll", "0", FCVAR_NONE, "Tuning: ironsight roll offset.");
ConVar cl_neo_ironsight_fov("cl_neo_ironsight_fov", "45", FCVAR_NONE, "Tuning: ironsight viewmodel FOV.");

// Which weapon the tuning cvars were loaded from. Compared by class name, since callers
// may pass a copy of the weapon info.
static char s_szTunedWeapon[MAX_WEAPON_STRING] = "";

static CNEOBaseCombatWeapon *LocalActiveWeapon()
{
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	return pPlayer ? dynamic_cast<CNEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon()) : nullptr;
}

static void LoadTuningFrom(const CNEOWeaponInfo &data)
{
	const Vector &pos = data.m_bHasIronsight ? data.m_vecVMIronPosOffset : data.m_vecVMAimPosOffset;
	const QAngle &ang = data.m_bHasIronsight ? data.m_angVMIronAngOffset : data.m_angVMAimAngOffset;
	cl_neo_ironsight_forward.SetValue(pos.x);
	cl_neo_ironsight_right.SetValue(pos.y);
	cl_neo_ironsight_up.SetValue(pos.z);
	cl_neo_ironsight_pitch.SetValue(ang[PITCH]);
	cl_neo_ironsight_yaw.SetValue(ang[YAW]);
	cl_neo_ironsight_roll.SetValue(ang[ROLL]);
	cl_neo_ironsight_fov.SetValue(data.m_bHasIronsight ? data.m_flVMIronFov : data.m_flVMAimFov);
	V_strncpy(s_szTunedWeapon, data.szClassName, sizeof(s_szTunedWeapon));
}

CON_COMMAND(cl_neo_ironsight_tune_load, "Reload the active weapon's script pose into the cl_neo_ironsight_* tuning cvars.")
{
	if (CNEOBaseCombatWeapon *pWeapon = LocalActiveWeapon())
	{
		LoadTuningFrom(pWeapon->GetNEOWpnData());
		Msg("Loaded pose for %s.\n", pWeapon->GetClassname());
	}
}

CON_COMMAND(cl_neo_ironsight_nudge, "Nudge an ironsight tuning value and switch tuning on. Usage: cl_neo_ironsight_nudge <forward|right|up|pitch|yaw|roll|fov> <delta>")
{
	if (args.ArgC() != 3)
	{
		Msg("Usage: cl_neo_ironsight_nudge <forward|right|up|pitch|yaw|roll|fov> <delta>\n");
		return;
	}
	char cvarName[64];
	V_snprintf(cvarName, sizeof(cvarName), "cl_neo_ironsight_%s", args.Arg(1));
	ConVarRef cvar(cvarName);
	if (!cvar.IsValid())
	{
		Msg("Unknown tuning value: %s\n", args.Arg(1));
		return;
	}
	if (!cl_neo_ironsight_tune.GetBool())
	{
		if (CNEOBaseCombatWeapon *pWeapon = LocalActiveWeapon())
		{
			LoadTuningFrom(pWeapon->GetNEOWpnData());
		}
		cl_neo_ironsight_tune.SetValue(1);
	}
	cvar.SetValue(cvar.GetFloat() + V_atof(args.Arg(2)));
	Msg("ironsight: forward %g  right %g  up %g  pitch %g  yaw %g  roll %g  fov %g\n",
		cl_neo_ironsight_forward.GetFloat(), cl_neo_ironsight_right.GetFloat(), cl_neo_ironsight_up.GetFloat(),
		cl_neo_ironsight_pitch.GetFloat(), cl_neo_ironsight_yaw.GetFloat(), cl_neo_ironsight_roll.GetFloat(),
		cl_neo_ironsight_fov.GetFloat());
}

CON_COMMAND(cl_neo_ironsight_save, "Append the tuned pose for the active weapon to ironsight_tuning.txt.")
{
	CNEOBaseCombatWeapon *pWeapon = LocalActiveWeapon();
	if (!pWeapon)
	{
		Msg("No active NT weapon.\n");
		return;
	}
	// One line per save: script name, fov, forward, right, up, pitch, yaw, roll. The last line per weapon wins.
	char line[256];
	V_snprintf(line, sizeof(line), "%s %g %g %g %g %g %g %g\n", pWeapon->GetClassname(),
		cl_neo_ironsight_fov.GetFloat(), cl_neo_ironsight_forward.GetFloat(), cl_neo_ironsight_right.GetFloat(),
		cl_neo_ironsight_up.GetFloat(), cl_neo_ironsight_pitch.GetFloat(), cl_neo_ironsight_yaw.GetFloat(),
		cl_neo_ironsight_roll.GetFloat());
	FileHandle_t file = g_pFullFileSystem->Open("ironsight_tuning.txt", "a", "MOD");
	if (!file)
	{
		Warning("Could not open ironsight_tuning.txt for writing.\n");
		return;
	}
	g_pFullFileSystem->Write(line, V_strlen(line), file);
	g_pFullFileSystem->Close(file);
	Msg("Saved: %s", line);
}
#endif // CLIENT_DLL

bool NeoIronsightsActive(const CNEOWeaponInfo &data)
{
#ifdef CLIENT_DLL
	return cl_neo_ironsights.GetBool() && (data.m_bHasIronsight || cl_neo_ironsight_tune.GetBool());
#else
	return false;
#endif
}

NeoAimPose NeoGetAimPose(const CNEOWeaponInfo &data)
{
#ifdef CLIENT_DLL
	if (cl_neo_ironsights.GetBool() && cl_neo_ironsight_tune.GetBool())
	{
		if (V_strcmp(data.szClassName, s_szTunedWeapon) != 0)
		{
			LoadTuningFrom(data); // Switched weapons: start from this weapon's current pose.
		}
		return {
			Vector(cl_neo_ironsight_forward.GetFloat(), cl_neo_ironsight_right.GetFloat(), cl_neo_ironsight_up.GetFloat()),
			QAngle(cl_neo_ironsight_pitch.GetFloat(), cl_neo_ironsight_yaw.GetFloat(), cl_neo_ironsight_roll.GetFloat()),
			cl_neo_ironsight_fov.GetFloat() };
	}
#endif
	if (NeoIronsightsActive(data))
	{
		return { data.m_vecVMIronPosOffset, data.m_angVMIronAngOffset, data.m_flVMIronFov };
	}
	return { data.m_vecVMAimPosOffset, data.m_angVMAimAngOffset, data.m_flVMAimFov };
}

float NeoAimTransitionTime(const CNEOWeaponInfo &data)
{
#ifdef CLIENT_DLL
	if (NeoIronsightsActive(data))
	{
		return cl_neo_ironsight_time.GetFloat();
	}
#endif
	return NEO_ZOOM_SPEED;
}

float NeoAimTransitionCurve(const CNEOWeaponInfo &data, float fraction)
{
	if (!NeoIronsightsActive(data))
	{
		return fraction; // Traditional NT aim moves linearly.
	}
	// Smootherstep: the gun eases out of the hip and settles gently onto the sights.
	const float t = clamp(fraction, 0.0f, 1.0f);
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float NeoIronsightBobScale(float ironsightBlend)
{
#ifdef CLIENT_DLL
	return Lerp(clamp(ironsightBlend, 0.0f, 1.0f), 1.0f, cl_neo_ironsight_bob.GetFloat());
#else
	return 1.0f;
#endif
}

float NeoIronsightIdleScale(float ironsightBlend)
{
#ifdef CLIENT_DLL
	return Lerp(clamp(ironsightBlend, 0.0f, 1.0f), 1.0f, cl_neo_ironsight_idle.GetFloat());
#else
	return 1.0f;
#endif
}

bool NeoIronsightIsRecoilActivity(int activity)
{
	// The Supa 7 fires buckshot with its secondary attack animation (slugs use the primary).
	return activity == ACT_VM_PRIMARYATTACK || activity == ACT_VM_SECONDARYATTACK || activity == ACT_VM_DRYFIRE
		|| activity == ACT_VM_RECOIL1 || activity == ACT_VM_RECOIL2 || activity == ACT_VM_RECOIL3;
}

#ifdef CLIENT_DLL
// Model-space transform of a bone, built by walking its parent chain.
static void BoneToModel(CStudioHdr *hdr, int bone, const Vector pos[], const Quaternion q[], matrix3x4_t &out)
{
	matrix3x4_t local;
	QuaternionMatrix(q[bone], pos[bone], local);
	const int parent = hdr->pBone(bone)->parent;
	if (parent < 0)
	{
		MatrixCopy(local, out);
		return;
	}
	matrix3x4_t parentToModel;
	BoneToModel(hdr, parent, pos, q, parentToModel);
	ConcatTransforms(parentToModel, local, out);
}

void NeoIronsightRestPose::Update(CStudioHdr *hdr, int poseSequence, float poseCycle, const float poseparam[])
{
	if (!hdr || (hdr->GetRenderHdr() == pModel && poseSequence == sequence && poseCycle == cycle))
	{
		return;
	}
	pModel = hdr->GetRenderHdr();
	sequence = poseSequence;
	cycle = poseCycle;
	IBoneSetup boneSetup(hdr, BONE_USED_BY_ANYTHING, poseparam);
	boneSetup.InitPose(pos, q);
	boneSetup.AccumulatePose(pos, q, poseSequence, poseCycle, 1.0f, 0.0f, nullptr);
}

void NeoIronsightDampRecoil(CStudioHdr *hdr, Vector pos[], Quaternion q[],
	const Vector settledPos[], const Quaternion settledQ[], int refBone, const CNEOWeaponInfo &data, float ironsightBlend)
{
	const float blend = clamp(ironsightBlend, 0.0f, 1.0f);
	if (!hdr || refBone < 0 || refBone >= hdr->numbones() || blend <= 0.0f)
	{
		return;
	}
	const auto onSights = [blend](float scale, float weaponScale) {
		return Lerp(blend, 1.0f, clamp(scale * weaponScale, 0.0f, 1.0f));
	};
	const float vertScale = onSights(cl_neo_ironsight_recoil_vertical.GetFloat(), data.m_flIronRecoilVertical);
	const float sideScale = onSights(cl_neo_ironsight_recoil_side.GetFloat(), data.m_flIronRecoilSide);
	const float backScale = onSights(cl_neo_ironsight_recoil_back.GetFloat(), data.m_flIronRecoilBack);

	// Gun (refBone) transforms in viewmodel space (x forward, y left, z up): live, and the fire
	// animation's settled last frame. Only the kick relative to the settled frame is damped; the
	// settled pose itself is left as animated. The Jitte's fire animation sits ~24 units off its idle
	// and the engine already compensates for that later, so correcting toward idle would double it.
	matrix3x4_t live, settled, settledInv, kick;
	BoneToModel(hdr, refBone, pos, q, live);
	BoneToModel(hdr, refBone, settledPos, settledQ, settled);
	MatrixInvert(settled, settledInv);
	ConcatTransforms(live, settledInv, kick);

	Vector liveOrigin, settledOrigin, unused;
	MatrixGetColumn(live, 3, liveOrigin);
	MatrixGetColumn(settled, 3, settledOrigin);
	QAngle kickAngles;
	MatrixAngles(kick, kickAngles, unused);

	// The kick with each axis scaled: pushback (x), sideways (y, yaw, roll), vertical (z, pitch).
	const Vector travel = liveOrigin - settledOrigin;
	Vector dampedTravel(travel.x * backScale, travel.y * sideScale, travel.z * vertScale);
	QAngle dampedAngles(kickAngles[PITCH] * vertScale, kickAngles[YAW] * sideScale, kickAngles[ROLL] * sideScale);

	// Leash: on the sights the gun never strays far from its settled pose, whatever the animation does.
	const float maxDist = ((data.m_flIronRecoilMaxDist > 0.0f) ? data.m_flIronRecoilMaxDist : cl_neo_ironsight_recoil_max_dist.GetFloat()) / blend;
	const float dist = dampedTravel.Length();
	if (dist > maxDist)
	{
		dampedTravel *= maxDist / dist;
	}
	const float maxAngle = ((data.m_flIronRecoilMaxAngle > 0.0f) ? data.m_flIronRecoilMaxAngle : cl_neo_ironsight_recoil_max_angle.GetFloat()) / blend;
	for (int axis = 0; axis < 3; ++axis)
	{
		dampedAngles[axis] = clamp(dampedAngles[axis], -maxAngle, maxAngle);
	}

	if (cl_neo_ironsight_recoil_debug.GetBool())
	{
		static float s_flNextDebugPrint = 0.0f;
		if (gpGlobals->curtime >= s_flNextDebugPrint)
		{
			s_flNextDebugPrint = gpGlobals->curtime + 0.05f;
			Msg("recoil raw: back %.2f side %.2f up %.2f | pitch %.2f yaw %.2f roll %.2f  ->  shown: back %.2f side %.2f up %.2f | pitch %.2f yaw %.2f roll %.2f\n",
				travel.x, travel.y, travel.z, kickAngles[PITCH], kickAngles[YAW], kickAngles[ROLL],
				dampedTravel.x, dampedTravel.y, dampedTravel.z, dampedAngles[PITCH], dampedAngles[YAW], dampedAngles[ROLL]);
		}
	}

	// Where the gun should be: the settled pose plus the damped kick.
	matrix3x4_t kickRot, target;
	AngleMatrix(dampedAngles, kickRot);
	ConcatTransforms(kickRot, settled, target);
	MatrixSetColumn(settledOrigin + dampedTravel, 3, target);

	// Correct the whole viewmodel rigidly at its root bones so arms and gun stay together.
	matrix3x4_t liveInv, correction;
	MatrixInvert(live, liveInv);
	ConcatTransforms(target, liveInv, correction);
	for (int i = 0; i < hdr->numbones(); ++i)
	{
		if (hdr->pBone(i)->parent >= 0)
		{
			continue;
		}
		matrix3x4_t local, corrected;
		QuaternionMatrix(q[i], pos[i], local);
		ConcatTransforms(correction, local, corrected);
		MatrixQuaternion(corrected, q[i]);
		MatrixGetColumn(corrected, 3, pos[i]);
	}
}
#endif // CLIENT_DLL

#ifdef CLIENT_DLL
// Prints where the gun (first attachment's bone) sits, in viewmodel space, across the idle loop and at
// the start and end of each fire animation. Rest-pose problems show up as poses far from the rest.
static void PrintGunOrigin(CStudioHdr *hdr, IBoneSetup &boneSetup, int refBone, int sequence, float cycle, const char *label)
{
	Vector pos[MAXSTUDIOBONES];
	QuaternionAligned q[MAXSTUDIOBONES];
	boneSetup.InitPose(pos, q);
	boneSetup.AccumulatePose(pos, q, sequence, cycle, 1.0f, 0.0f, nullptr);
	matrix3x4_t gunToModel;
	BoneToModel(hdr, refBone, pos, q, gunToModel);
	Vector origin;
	MatrixGetColumn(gunToModel, 3, origin);
	Msg("  %-10s cycle %.3f  gun forward %7.2f  left %7.2f  up %7.2f\n", label, cycle, origin.x, origin.y, origin.z);
}

CON_COMMAND(cl_neo_ironsight_restinfo, "Print the gun's position across the active viewmodel's idle and fire animations.")
{
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	C_BaseViewModel *pViewModel = pPlayer ? pPlayer->GetViewModel() : nullptr;
	CStudioHdr *hdr = pViewModel ? pViewModel->GetModelPtr() : nullptr;
	if (!hdr)
	{
		Msg("No viewmodel.\n");
		return;
	}
	float poseparam[MAXSTUDIOPOSEPARAM];
	pViewModel->GetPoseParameters(hdr, poseparam);
	IBoneSetup boneSetup(hdr, BONE_USED_BY_ANYTHING, poseparam);
	const int refBone = (hdr->GetNumAttachments() > 0) ? hdr->pAttachment(0).localbone : hdr->numbones() - 1;
	Msg("%s (gun bone %s)\n", pViewModel->GetModelName() ? STRING(pViewModel->GetModelName()) : "?", hdr->pBone(refBone)->pszName());
	for (int seq = 0; seq < hdr->GetNumSeq(); ++seq)
	{
		const int activity = pViewModel->GetSequenceActivity(seq);
		const char *name = pViewModel->GetSequenceName(seq);
		if (activity == ACT_VM_IDLE || activity == ACT_VM_IDLE_EMPTY)
		{
			for (int i = 0; i < 16; ++i)
			{
				PrintGunOrigin(hdr, boneSetup, refBone, seq, i / 16.0f, name);
			}
		}
		else if (NeoIronsightIsRecoilActivity(activity))
		{
			PrintGunOrigin(hdr, boneSetup, refBone, seq, 0.0f, name);
			PrintGunOrigin(hdr, boneSetup, refBone, seq, 0.999f, name);
		}
	}
}
#endif // CLIENT_DLL

#ifdef CLIENT_DLL
bool NeoIronsightsHideCrosshair(bool bAiming, bool bCloaked)
{
	return cl_neo_ironsights.GetBool() && !cl_neo_ironsight_crosshair.GetBool() && !(bAiming && bCloaked);
}

NeoIronsightHiddenMaterials::NeoIronsightHiddenMaterials(const CNEOWeaponInfo *pData, float ironsightBlend)
{
	// Hide once the gun is most of the way onto the sights.
	if (!pData || ironsightBlend < 0.5f || !NeoIronsightsActive(*pData) || !pData->m_szIronHideMaterials[0])
	{
		return;
	}
	CUtlStringList names;
	V_SplitString(pData->m_szIronHideMaterials, ";", names);
	for (int i = 0; i < names.Count() && m_count < MAX_MATERIALS; ++i)
	{
		IMaterial *pMaterial = materials->FindMaterial(names[i], TEXTURE_GROUP_MODEL, false);
		if (pMaterial && !pMaterial->IsErrorMaterial() && !pMaterial->GetMaterialVarFlag(MATERIAL_VAR_NO_DRAW))
		{
			pMaterial->SetMaterialVarFlag(MATERIAL_VAR_NO_DRAW, true);
			m_materials[m_count++] = pMaterial;
		}
	}
}

NeoIronsightHiddenMaterials::~NeoIronsightHiddenMaterials()
{
	for (int i = 0; i < m_count; ++i)
	{
		m_materials[i]->SetMaterialVarFlag(MATERIAL_VAR_NO_DRAW, false);
	}
}
#endif // CLIENT_DLL
