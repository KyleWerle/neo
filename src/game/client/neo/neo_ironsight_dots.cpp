#include "cbase.h"
#include "neo_ironsight_dots.h"
#include "neo_ironsights.h"
#include "weapon_neobasecombatweapon.h"
#include "view.h"
#include "studio.h"
#include "KeyValues.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "c_neo_player.h"
#include "filesystem.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_ironsight_dots("cl_neo_ironsight_dots", "1", FCVAR_ARCHIVE,
	"Glowing sight dots on ironsight weapons while cloaked.", true, 0, true, 1);
ConVar cl_neo_ironsight_dots_always("cl_neo_ironsight_dots_always", "0", FCVAR_NONE,
	"Debug: show sight dots uncloaked too (for tuning).", true, 0, true, 1);

// Live tuning: with cl_neo_ironsight_dots_tune 1 these replace the weapon script's IronsightDots values.
ConVar cl_neo_ironsight_dots_tune("cl_neo_ironsight_dots_tune", "0", FCVAR_NONE, "Use the cl_neo_ironsight_dot_* values below.", true, 0, true, 1);
ConVar cl_neo_ironsight_dot_front("cl_neo_ironsight_dot_front", "0 0", FCVAR_NONE, "Tuning: front dot \"depth drop\" (0 depth = auto).");
ConVar cl_neo_ironsight_dot_rear("cl_neo_ironsight_dot_rear", "0 0 0", FCVAR_NONE, "Tuning: rear dots \"depth drop halfgap\" (0 depth = auto).");
ConVar cl_neo_ironsight_dot_size("cl_neo_ironsight_dot_size", "0.12", FCVAR_NONE, "Tuning: dot radius in viewmodel units.");

static constexpr const char *DOT_TEXTURE = "effects/neo_sight_dot";

// Where the dots sit, in eye space at rest on the sights (x forward along the sight line, y left, z up).
struct NeoDotLayout
{
	Vector front;
	Vector rearLeft;
	Vector rearRight;
	float size;
};

// Per weapon: the gun bone, the idle rest pose, and the muzzle depth on the sights (for auto placement).
struct NeoDotPin
{
	int viewModel = 0;
	char weapon[MAX_WEAPON_STRING] = "";
	int gunBone = -1;
	float muzzleDepth = 0.0f;
	NeoIronsightRestPose rest;
};
static NeoDotPin s_pin;

CON_COMMAND(cl_neo_ironsight_dots_dump, "Print the tuned sight dots as an IronsightDots block for the weapon script.")
{
	Msg("\t\"IronsightDots\"\n\t{\n\t\t\"front\"\t\t\"%s\"\n\t\t\"rear\"\t\t\"%s\"\n\t\t\"size\"\t\t\"%g\"\n\t}\n",
		cl_neo_ironsight_dot_front.GetString(), cl_neo_ironsight_dot_rear.GetString(), cl_neo_ironsight_dot_size.GetFloat());
}

static NeoDotLayout DotLayout(const CNEOWeaponInfo &data)
{
	Vector front = data.m_vecIronDotFront;	// depth, -, drop
	Vector rear = data.m_vecIronDotRear;	// depth, halfgap, drop
	float size = data.m_flIronDotSize;
	if (cl_neo_ironsight_dots_tune.GetBool())
	{
		front.Init();
		rear.Init();
		sscanf(cl_neo_ironsight_dot_front.GetString(), "%f %f", &front.x, &front.z);
		sscanf(cl_neo_ironsight_dot_rear.GetString(), "%f %f %f", &rear.x, &rear.z, &rear.y);
		size = cl_neo_ironsight_dot_size.GetFloat();
	}
	// Unset depths: the front post near the muzzle, the rear notch about halfway back.
	const float muzzle = s_pin.muzzleDepth;
	if (front.x <= 0.0f)
	{
		front.x = muzzle * 0.9f;
	}
	if (rear.x <= 0.0f)
	{
		rear.x = muzzle * 0.45f;
	}
	if (rear.y <= 0.0f)
	{
		rear.y = rear.x * 0.035f;
	}
	return { Vector(front.x, 0.0f, front.z), Vector(rear.x, rear.y, rear.z), Vector(rear.x, -rear.y, rear.z), size };
}

static IMaterial *DotMaterial()
{
	static CMaterialReference s_material;
	if (!s_material.IsValid())
	{
		KeyValues *pVMT = new KeyValues("UnlitGeneric");
		pVMT->SetString("$basetexture", DOT_TEXTURE);
		pVMT->SetInt("$additive", 1);
		pVMT->SetInt("$vertexcolor", 1);
		pVMT->SetInt("$vertexalpha", 1);
		pVMT->SetInt("$nocull", 1);
		s_material.Init("__neo_ironsight_dot", TEXTURE_GROUP_OTHER, pVMT);
	}
	return s_material;
}

// The gun bone: the bone carrying the first attachment (the muzzle), or its nearest ancestor that has
// vertices, since render-time bone setup always computes those.
static int GunBone(CStudioHdr *hdr)
{
	int bone = (hdr->GetNumAttachments() > 0) ? hdr->pAttachment(0).localbone : -1;
	while (bone >= 0 && !(hdr->boneFlags(bone) & BONE_USED_BY_VERTEX_MASK))
	{
		bone = hdr->pBone(bone)->parent;
	}
	return bone;
}

// Where the viewmodel entity sits relative to the eye when fully on the sights, before bob and lag:
// the same offset CNEOPredictedViewModel::CalcViewModelView applies (the weapon's aim pose).
static void SightsEntityToWorld(const CNEOWeaponInfo &data, matrix3x4_t &out)
{
	const NeoAimPose aimPose = NeoGetAimPose(data);
	const QAngle &eyeAngles = CurrentViewAngles();
	Vector forward, right, up;
	AngleVectors(eyeAngles, &forward, &right, &up);
	const Vector origin = CurrentViewOrigin() + forward * aimPose.pos.x + right * aimPose.pos.y + up * aimPose.pos.z;
	AngleMatrix(eyeAngles + aimPose.ang, origin, out);
}

void NeoIronsightDrawDots(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked)
{
	if (!pViewModel || !data.m_bHasIronDots || !cl_neo_ironsight_dots.GetBool() || !NeoIronsightsActive(data))
	{
		return;
	}
	CStudioHdr *hdr = pViewModel->GetModelPtr();
	if (!hdr)
	{
		return;
	}
	if (s_pin.viewModel != pViewModel->entindex() || V_strcmp(s_pin.weapon, data.szClassName) != 0)
	{
		s_pin = NeoDotPin();
		// Tuning values belong to the previous weapon; the next nudge loads this one's.
		cl_neo_ironsight_dots_tune.SetValue(0);
		s_pin.viewModel = pViewModel->entindex();
		V_strncpy(s_pin.weapon, data.szClassName, sizeof(s_pin.weapon));
		s_pin.gunBone = GunBone(hdr);
	}
	if (s_pin.gunBone < 0)
	{
		return;
	}

	// The gun on the sights, computed rather than measured, so the dots exist from the moment the
	// weapon is out: the sights entity offset times the idle rest pose (the pose the sights are tuned in).
	const int idleSequence = pViewModel->SelectWeightedSequence(ACT_VM_IDLE);
	if (idleSequence < 0)
	{
		return;
	}
	float poseparam[MAXSTUDIOPOSEPARAM];
	pViewModel->GetPoseParameters(hdr, poseparam);
	s_pin.rest.Update(hdr, idleSequence, 0.0f, poseparam);

	matrix3x4_t eyeToWorld, worldToEye, sightsEntity, restGunModel, restGunWorld, restGunInEye;
	AngleMatrix(CurrentViewAngles(), CurrentViewOrigin(), eyeToWorld);
	MatrixInvert(eyeToWorld, worldToEye);
	SightsEntityToWorld(data, sightsEntity);
	NeoIronsightBoneToModel(hdr, s_pin.gunBone, s_pin.rest.pos, s_pin.rest.q, restGunModel);
	ConcatTransforms(sightsEntity, restGunModel, restGunWorld);
	ConcatTransforms(worldToEye, restGunWorld, restGunInEye);

	// Muzzle depth on the sights, for automatic placement.
	if (hdr->GetNumAttachments() > 0)
	{
		const mstudioattachment_t &muzzle = hdr->pAttachment(0);
		matrix3x4_t attachBoneModel, muzzleModel, muzzleWorld;
		NeoIronsightBoneToModel(hdr, muzzle.localbone, s_pin.rest.pos, s_pin.rest.q, attachBoneModel);
		ConcatTransforms(attachBoneModel, muzzle.local, muzzleModel);
		ConcatTransforms(sightsEntity, muzzleModel, muzzleWorld);
		Vector muzzleOrigin;
		MatrixGetColumn(muzzleWorld, 3, muzzleOrigin);
		s_pin.muzzleDepth = DotProduct(muzzleOrigin - CurrentViewOrigin(), CurrentViewForward());
	}
	if (s_pin.muzzleDepth <= 0.0f || !(bCloaked || cl_neo_ironsight_dots_always.GetBool()))
	{
		return;
	}

	// Rest eye-space points -> gun space (on the sights) -> where the gun is now.
	matrix3x4_t gunToWorld, eyeAtRestInGun, restEyeToWorld;
	pViewModel->GetBoneTransform(s_pin.gunBone, gunToWorld);
	const NeoDotLayout layout = DotLayout(data);
	MatrixInvert(restGunInEye, eyeAtRestInGun);
	ConcatTransforms(gunToWorld, eyeAtRestInGun, restEyeToWorld);
	const Vector eyePoints[3] = { layout.front, layout.rearLeft, layout.rearRight };
	const Color colours[3] = { data.m_clrIronDotFront, data.m_clrIronDotRear, data.m_clrIronDotRear };

	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->Bind(DotMaterial());
	IMesh *pMesh = pRenderContext->GetDynamicMesh();
	CMeshBuilder meshBuilder;
	meshBuilder.Begin(pMesh, MATERIAL_QUADS, 3);
	const Vector right = CurrentViewRight() * layout.size;
	const Vector up = CurrentViewUp() * layout.size;
	for (int i = 0; i < 3; ++i)
	{
		Vector centre;
		VectorTransform(eyePoints[i], restEyeToWorld, centre);
		// A hair toward the eye so the insert never sinks into the post's surface.
		Vector toEye = CurrentViewOrigin() - centre;
		VectorNormalize(toEye);
		centre += toEye * 0.05f;

		const Vector corners[4] = { centre - right + up, centre + right + up, centre + right - up, centre - right - up };
		const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		for (int c = 0; c < 4; ++c)
		{
			meshBuilder.Color4ub(colours[i].r(), colours[i].g(), colours[i].b(), 255);
			meshBuilder.TexCoord2f(0, uv[c][0], uv[c][1]);
			meshBuilder.Position3fv(corners[c].Base());
			meshBuilder.AdvanceVertex();
		}
	}
	meshBuilder.End();
	pMesh->Draw();
}

//-----------------------------------------------------------------------------
// Tuning: nudge the dots in game (bound to the numpad by the dev autoexec), save to a file that
// SourceDev/apply-ironsights.py merges into the weapon scripts.
//-----------------------------------------------------------------------------
static const CNEOWeaponInfo *LocalWeaponData(const char **ppszClass = nullptr)
{
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	auto *pWeapon = pPlayer ? dynamic_cast<CNEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon()) : nullptr;
	if (pWeapon && ppszClass)
	{
		*ppszClass = pWeapon->GetClassname();
	}
	return pWeapon ? &pWeapon->GetNEOWpnData() : nullptr;
}

static void PrintDots()
{
	Msg("dots: front \"%s\"  rear \"%s\"  size %g\n", cl_neo_ironsight_dot_front.GetString(),
		cl_neo_ironsight_dot_rear.GetString(), cl_neo_ironsight_dot_size.GetFloat());
}

CON_COMMAND(cl_neo_ironsight_dots_nudge, "Nudge a sight dot value and switch dot tuning on. Usage: cl_neo_ironsight_dots_nudge <front|rear> <depth|drop|gap|size> <delta>")
{
	if (args.ArgC() != 4)
	{
		Msg("Usage: cl_neo_ironsight_dots_nudge <front|rear> <depth|drop|gap|size> <delta>\n");
		return;
	}
	const CNEOWeaponInfo *pData = LocalWeaponData();
	if (!pData || !pData->m_bHasIronDots)
	{
		Msg("The active weapon has no IronsightDots block.\n");
		return;
	}
	if (!cl_neo_ironsight_dots_tune.GetBool())
	{
		// Start from what is on screen now, automatic placements included.
		const NeoDotLayout layout = DotLayout(*pData);
		cl_neo_ironsight_dot_front.SetValue(CFmtStr("%g %g", layout.front.x, layout.front.z));
		cl_neo_ironsight_dot_rear.SetValue(CFmtStr("%g %g %g", layout.rearLeft.x, layout.rearLeft.z, layout.rearLeft.y));
		cl_neo_ironsight_dot_size.SetValue(layout.size);
		cl_neo_ironsight_dots_tune.SetValue(1);
		cl_neo_ironsight_dots_always.SetValue(1);
	}

	const bool bFront = V_stricmp(args.Arg(1), "front") == 0;
	const char *pszField = args.Arg(2);
	const float delta = V_atof(args.Arg(3));
	if (V_stricmp(pszField, "size") == 0)
	{
		cl_neo_ironsight_dot_size.SetValue(Max(0.01f, cl_neo_ironsight_dot_size.GetFloat() + delta));
		PrintDots();
		return;
	}
	float depth = 0.0f, drop = 0.0f, gap = 0.0f;
	if (bFront)
	{
		sscanf(cl_neo_ironsight_dot_front.GetString(), "%f %f", &depth, &drop);
	}
	else
	{
		sscanf(cl_neo_ironsight_dot_rear.GetString(), "%f %f %f", &depth, &drop, &gap);
	}
	if (V_stricmp(pszField, "depth") == 0)
	{
		depth = Max(1.0f, depth + delta);
	}
	else if (V_stricmp(pszField, "drop") == 0)
	{
		drop += delta;
	}
	else if (V_stricmp(pszField, "gap") == 0 && !bFront)
	{
		gap = Max(0.0f, gap + delta);
	}
	if (bFront)
	{
		cl_neo_ironsight_dot_front.SetValue(CFmtStr("%g %g", depth, drop));
	}
	else
	{
		cl_neo_ironsight_dot_rear.SetValue(CFmtStr("%g %g %g", depth, drop, gap));
	}
	PrintDots();
}

CON_COMMAND(cl_neo_ironsight_dots_save, "Append the tuned dots for the active weapon to ironsight_dots.txt.")
{
	const char *pszClass = nullptr;
	if (!LocalWeaponData(&pszClass) || !pszClass)
	{
		Msg("No active NT weapon.\n");
		return;
	}
	float frontDepth = 0, frontDrop = 0, rearDepth = 0, rearDrop = 0, gap = 0;
	sscanf(cl_neo_ironsight_dot_front.GetString(), "%f %f", &frontDepth, &frontDrop);
	sscanf(cl_neo_ironsight_dot_rear.GetString(), "%f %f %f", &rearDepth, &rearDrop, &gap);
	// One line per save: script name, front depth, front drop, rear depth, rear drop, rear half gap, size.
	const CFmtStr line("%s %g %g %g %g %g %g\n", pszClass, frontDepth, frontDrop, rearDepth, rearDrop, gap, cl_neo_ironsight_dot_size.GetFloat());
	FileHandle_t file = g_pFullFileSystem->Open("ironsight_dots.txt", "a", "MOD");
	if (!file)
	{
		Warning("Could not open ironsight_dots.txt for writing.\n");
		return;
	}
	g_pFullFileSystem->Write(line.Get(), V_strlen(line.Get()), file);
	g_pFullFileSystem->Close(file);
	Msg("Saved: %s", line.Get());
}
