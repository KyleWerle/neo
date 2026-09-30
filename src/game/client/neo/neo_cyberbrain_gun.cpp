#include "cbase.h"
#include "neo_cyberbrain_gun.h"
#include "neo_cyberbrain_internal.h"
#include "c_baseanimating.h"
#include "bone_setup.h"
#include "view.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "engine/ivmodelrender.h"
#include "KeyValues.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The weapon on the gun. The bands run across the gun along the view's forward axis (the depth from the eye), so they
// fit every gun with no per-gun art: the muzzle's depth is the front, the magazine's (or the ejection port's) a little
// further back the rear. The callout points come from the models' own attachments (every NT gun has "muzzle" and
// "eject") and magazine bones, as drawn this frame.

ConVar cl_neo_hud_gun("cl_neo_hud_gun", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's overlay on the gun: a wireframe scan on reload, a flash at the muzzle on each shot, the BALC's"
	" heat. 0 = off.", true, 0, true, 1);

namespace NC = NeoCyberbrain;

constexpr float GUN_SCAN_TIME = 0.7f, GUN_SCAN_EVERY = 1.0f;	// seconds a scan takes, and between scans in a reload
constexpr float GUN_SCAN_WIDTH = 2.5f;							// units across the gun
constexpr float GUN_SHOT_TIME = 0.12f, GUN_SHOT_DEPTH = 5.0f;
constexpr float GUN_REAR_MARGIN = 10.0f;						// units behind the magazine the scan runs to

static struct
{
	Vector2D at[NEO_GUN__COUNT];
	bool bOn[NEO_GUN__COUNT] = {};
	int frame = -100;
} s_points;

static IMaterial *WireMaterial()
{
	static CMaterialReference s_material;
	if (!s_material.IsValid())
	{
		KeyValues *pVMT = new KeyValues("UnlitGeneric");
		pVMT->SetString("$basetexture", "white");
		pVMT->SetInt("$wireframe", 1);
		pVMT->SetInt("$additive", 1);
		// Through the gun: the scan shows the far side's lines too, and never fights the gun's own depth.
		pVMT->SetInt("$ignorez", 1);
		pVMT->SetInt("$nocull", 1);
		s_material.Init("__neo_cyberbrain_gun_wire", TEXTURE_GROUP_OTHER, pVMT);
	}
	return s_material;
}

// The first attachment with one of these names, as drawn, in world space.
static bool AttachmentAt(C_BaseAnimating *pViewModel, CStudioHdr *hdr, const char *const names[], int count, Vector &out)
{
	for (int n = 0; n < count; ++n)
	{
		for (int i = 0; i < hdr->GetNumAttachments(); ++i)
		{
			const mstudioattachment_t &attachment = hdr->pAttachment(i);
			if (V_stricmp(attachment.pszName(), names[n]) != 0 || attachment.localbone < 0)
				continue;
			matrix3x4_t world;
			ConcatTransforms(pViewModel->GetBone(attachment.localbone), attachment.local, world);
			MatrixGetColumn(world, 3, out);
			return true;
		}
	}
	return false;
}

// The first bone with one of these names, as drawn (GetBone, never GetBoneTransform: that reads a hitbox-only cache).
static bool BoneAt(C_BaseAnimating *pViewModel, const char *const names[], int count, Vector &out)
{
	for (int n = 0; n < count; ++n)
	{
		const int bone = pViewModel->LookupBone(names[n]);
		if (bone >= 0)
		{
			MatrixGetColumn(pViewModel->GetBone(bone), 3, out);
			return true;
		}
	}
	return false;
}

// Projects a world point with this render view's own matrices (the viewmodel's field of view) to screen pixels.
static bool Project(const VMatrix &worldToClip, int vx, int vy, int vw, int vh, const Vector &p, Vector2D &out)
{
	Vector4D clip;
	Vector4DMultiply(worldToClip, Vector4D(p.x, p.y, p.z, 1.0f), clip);
	if (clip.w <= 0.001f)
		return false;
	out.Init(vx + (0.5f + 0.5f * clip.x / clip.w) * vw, vy + (0.5f - 0.5f * clip.y / clip.w) * vh);
	return true;
}

// A band from depth `from` to depth `to` (units ahead of the eye).
static void AddBand(NeoCyberGunPass &pass, float from, float to, const Color &color, float alpha)
{
	if (pass.count >= NEO_CYBER_GUN_BANDS || alpha <= 0.01f || to <= from)
		return;
	const Vector &forward = CurrentViewForward();
	const float eye = DotProduct(forward, CurrentViewOrigin());
	NeoCyberGunBand &band = pass.bands[pass.count++];
	band.planes[0][0] = forward.x; band.planes[0][1] = forward.y; band.planes[0][2] = forward.z; band.planes[0][3] = eye + from;
	band.planes[1][0] = -forward.x; band.planes[1][1] = -forward.y; band.planes[1][2] = -forward.z; band.planes[1][3] = -(eye + to);
	band.color = color;
	band.alpha = alpha;
}

bool NeoCyberGunPrepare(C_BaseAnimating *pViewModel, NeoCyberGunPass &pass)
{
	pass.count = 0;
	Color color;
	const NC::Senses *pSenses = NC::PublishedSenses(color);
	CStudioHdr *hdr = pViewModel ? pViewModel->GetModelPtr() : nullptr;
	if (!pSenses || !hdr || !NeoCyberbrainShowing())
		return false;

	// The points, as drawn: this frame's pose (attachment bones included).
	pViewModel->SetupBones(nullptr, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime);
	static const char *const s_muzzle[] = { "muzzle", "1" }, *const s_eject[] = { "eject", "2" };
	static const char *const s_mag[] = { "Clip", "Clip01", "clip", "mag", "v_weapon.MP5_Clip", "ValveBiped.clip" };
	Vector world[NEO_GUN__COUNT];
	bool bHas[NEO_GUN__COUNT];
	bHas[NEO_GUN_MUZZLE] = AttachmentAt(pViewModel, hdr, s_muzzle, ARRAYSIZE(s_muzzle), world[NEO_GUN_MUZZLE]);
	bHas[NEO_GUN_EJECT] = AttachmentAt(pViewModel, hdr, s_eject, ARRAYSIZE(s_eject), world[NEO_GUN_EJECT]);
	bHas[NEO_GUN_MAG] = BoneAt(pViewModel, s_mag, ARRAYSIZE(s_mag), world[NEO_GUN_MAG]);
	if (!bHas[NEO_GUN_MAG] && bHas[NEO_GUN_EJECT])
	{
		world[NEO_GUN_MAG] = world[NEO_GUN_EJECT];
		bHas[NEO_GUN_MAG] = true;
	}

	CMatRenderContextPtr pRenderContext(materials);
	VMatrix view, projection, worldToClip;
	pRenderContext->GetMatrix(MATERIAL_VIEW, &view);
	pRenderContext->GetMatrix(MATERIAL_PROJECTION, &projection);
	MatrixMultiply(projection, view, worldToClip);
	int vx, vy, vw, vh;
	pRenderContext->GetViewport(vx, vy, vw, vh);
	for (int i = 0; i < NEO_GUN__COUNT; ++i)
		s_points.bOn[i] = bHas[i] && Project(worldToClip, vx, vy, vw, vh, world[i], s_points.at[i]);
	s_points.frame = gpGlobals->framecount;

	// Custom clip planes can't change mid-scene under fast clipping (as the sight glass's split).
	if (!cl_neo_hud_gun.GetBool() || !bHas[NEO_GUN_MUZZLE] || materials->UsingFastClipping())
		return false;
	const Vector &forward = CurrentViewForward();
	const Vector &eye = CurrentViewOrigin();
	const float front = DotProduct(forward, world[NEO_GUN_MUZZLE] - eye);
	const float back = bHas[NEO_GUN_MAG] ? DotProduct(forward, world[NEO_GUN_MAG] - eye) : front * 0.4f;
	const float rear = Max(1.0f, Min(back, front) - GUN_REAR_MARGIN);
	if (front <= rear)
		return false;

	const NC::Senses &s = *pSenses;
	const float now = gpGlobals->realtime;
	// The reload: a scan from the muzzle back, again each second until it's done.
	if (s.bReloading)
	{
		const float t = fmodf(Max(0.0f, now - s.reloadStart), GUN_SCAN_EVERY) / GUN_SCAN_TIME;
		if (t < 1.0f)
		{
			const float at = front + 1.0f - t * (front + 1.0f - rear);
			AddBand(pass, at - GUN_SCAN_WIDTH * 0.5f, at + GUN_SCAN_WIDTH * 0.5f, NC::WARN, 0.8f * (1.0f - 0.5f * t));
		}
	}
	// A shot: a flash round the muzzle.
	const float shot = 1.0f - (now - s.shotTime) / GUN_SHOT_TIME;
	if (shot > 0.0f)
		AddBand(pass, front - GUN_SHOT_DEPTH, front + 2.0f, color, 0.6f * shot);
	// The BALC's heat, creeping back from the muzzle as it climbs.
	if (s.ammo.bHeat && s.ammo.heat > 0.05f)
	{
		const float heat = s.ammo.heat, pulse = s.ammo.bOverheated ? 0.6f + 0.4f * sinf(now * 12.0f) : 1.0f;
		AddBand(pass, front - heat * (front - rear), front + 2.0f, s.heatLevel == 2 ? NC::CRIT : NC::WARN, 0.3f * heat * pulse);
	}
	return pass.count > 0;
}

void NeoCyberGunBandBegin(const NeoCyberGunPass &pass, int band)
{
	const NeoCyberGunBand &b = pass.bands[band];
	IMaterial *pMaterial = WireMaterial();
	pMaterial->ColorModulate(b.color.r() / 255.0f, b.color.g() / 255.0f, b.color.b() / 255.0f);
	pMaterial->AlphaModulate(b.alpha);
	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->PushCustomClipPlane(b.planes[0]);
	pRenderContext->PushCustomClipPlane(b.planes[1]);
	modelrender->ForcedMaterialOverride(pMaterial);
}

void NeoCyberGunBandEnd()
{
	modelrender->ForcedMaterialOverride(nullptr);
	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->PopCustomClipPlane();
	pRenderContext->PopCustomClipPlane();
}

bool NeoCyberGunPointOnScreen(NeoCyberGunPoint point, Vector2D &out)
{
	if (s_points.frame < gpGlobals->framecount - 1 || !s_points.bOn[point])
		return false;
	out = s_points.at[point];
	return true;
}
