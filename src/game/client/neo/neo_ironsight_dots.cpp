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

// Pin state: the gun bone's pose relative to the eye when the aim last settled on the sights.
struct NeoDotPin
{
	int viewModel = 0;
	char weapon[MAX_WEAPON_STRING] = "";
	bool pinned = false;
	bool wasSettled = false;
	int gunBone = -1;
	matrix3x4_t restGunInEye;
	float muzzleDepth = 0.0f;
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

static void EyeToWorld(matrix3x4_t &out)
{
	AngleMatrix(CurrentViewAngles(), CurrentViewOrigin(), out);
}

void NeoIronsightDrawDots(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, float ironsightBlend, bool bAiming, bool bCloaked)
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
		s_pin.viewModel = pViewModel->entindex();
		V_strncpy(s_pin.weapon, data.szClassName, sizeof(s_pin.weapon));
		s_pin.gunBone = GunBone(hdr);
	}
	if (s_pin.gunBone < 0)
	{
		return;
	}

	matrix3x4_t eyeToWorld, worldToEye, gunToWorld, gunInEye;
	EyeToWorld(eyeToWorld);
	MatrixInvert(eyeToWorld, worldToEye);
	pViewModel->GetBoneTransform(s_pin.gunBone, gunToWorld);
	ConcatTransforms(worldToEye, gunToWorld, gunInEye);

	// Pin on the first settled frame of each aim, so the dots follow tuning and the aim pose.
	const int activity = pViewModel->GetSequenceActivity(pViewModel->GetSequence());
	const bool bSettled = bAiming && ironsightBlend >= 0.999f && (activity == ACT_VM_IDLE || activity == ACT_VM_IDLE_EMPTY);
	if (bSettled && !s_pin.wasSettled)
	{
		MatrixCopy(gunInEye, s_pin.restGunInEye);
		Vector muzzle;
		QAngle muzzleAngles;
		// Attachment depth is unaffected by the viewmodel FOV conversion (only x/y are rescaled).
		if (pViewModel->GetAttachment(1, muzzle, muzzleAngles))
		{
			s_pin.muzzleDepth = DotProduct(muzzle - CurrentViewOrigin(), CurrentViewForward());
		}
		s_pin.pinned = s_pin.muzzleDepth > 0.0f;
	}
	s_pin.wasSettled = bSettled;
	if (!s_pin.pinned || !(bCloaked || cl_neo_ironsight_dots_always.GetBool()))
	{
		return;
	}

	// Rest eye-space points -> gun space (at the pin) -> world now.
	const NeoDotLayout layout = DotLayout(data);
	matrix3x4_t eyeAtRestInGun, restEyeToWorld;
	MatrixInvert(s_pin.restGunInEye, eyeAtRestInGun);
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
