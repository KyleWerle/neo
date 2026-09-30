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
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "datacache/imdlcache.h"
#include "engine/ivmodelinfo.h"
#include "studio.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The weapon on the gun. The reload's band is a box round the magazine itself: its real extent, measured once per model
// from the vertices weighted to its bone (in that bone's own space), carried with the bone every frame, so it fits
// every gun with no per-gun art, whatever the bone's origin (the ZR68s' and the SRM's sit off the magazine). A model
// with no vertices on its magazine bone falls back to a box round the bone, square to the view
// (cl_neo_hud_gun_mag_box). cl_neo_hud_gun_debug 1 draws the box all the time. The
// callout points come from the models' own attachments (every NT gun has "muzzle" and "eject") and magazine bones,
// as drawn this frame.

ConVar cl_neo_hud_gun("cl_neo_hud_gun", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's overlay on the gun: a wireframe scan through the magazine on reload. 0 = off.", true, 0, true, 1);
ConVar cl_neo_hud_gun_mag_box("cl_neo_hud_gun_mag_box", "3 7 2", FCVAR_NONE,
	"The box round the magazine bone the reload scan draws in, units: half its width, how far it reaches down, and up.");
ConVar cl_neo_hud_gun_debug("cl_neo_hud_gun_debug", "0", FCVAR_NONE,
	"1 = draw the magazine box's wireframe all the time (for tuning cl_neo_hud_gun_mag_box).", true, 0, true, 1);

extern ConVar cl_neo_hud_gun_count;

namespace NC = NeoCyberbrain;

constexpr float GUN_SCAN_TIME = 0.7f, GUN_SCAN_EVERY = 1.0f;	// seconds a scan takes, and between scans in a reload
constexpr float GUN_SCAN_WIDTH = 2.0f;							// units along the view
constexpr float GUN_SCAN_ALPHA = 0.45f;

static struct
{
	Vector2D at[NEO_GUN__COUNT];
	bool bOn[NEO_GUN__COUNT] = {};
	int frame = -100;
} s_points;

static int s_pushed = 0;	// the planes the band pushed

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

// An attachment as drawn, in world space.
static void AttachmentAt(C_BaseAnimating *pViewModel, CStudioHdr *hdr, int index, Vector &out)
{
	const mstudioattachment_t &attachment = hdr->pAttachment(index);
	matrix3x4_t world;
	ConcatTransforms(pViewModel->GetBone(attachment.localbone), attachment.local, world);
	MatrixGetColumn(world, 3, out);
}

// The first attachment with one of these names (on a bone), or -1.
static int AttachmentIndex(CStudioHdr *hdr, const char *const names[], int count)
{
	for (int n = 0; n < count; ++n)
	{
		for (int i = 0; i < hdr->GetNumAttachments(); ++i)
		{
			const mstudioattachment_t &attachment = hdr->pAttachment(i);
			if (V_stricmp(attachment.pszName(), names[n]) == 0 && attachment.localbone >= 0)
				return i;
		}
	}
	return -1;
}

static void SetGunPlane(float plane[4], const Vector &n, float d);

// The first bone with one of these names, or -1.
static int BoneIndex(C_BaseAnimating *pViewModel, const char *const names[], int count)
{
	for (int n = 0; n < count; ++n)
	{
		const int bone = pViewModel->LookupBone(names[n]);
		if (bone >= 0)
			return bone;
	}
	return -1;
}

// A magazine's extent in its bone's own space, measured from the model's vertices (those weighted at least half to
// the bone), once per model.
struct MagMesh { const model_t *pModel = nullptr; int bone = -1; bool bValid = false; Vector mins, maxs; };
static MagMesh s_magMeshes[16];
static int s_magNext = 0;

static const MagMesh *MagMeshOf(C_BaseAnimating *pViewModel, CStudioHdr *hdr, int bone)
{
	const model_t *pModel = pViewModel->GetModel();
	for (const MagMesh &mesh : s_magMeshes)
	{
		if (mesh.pModel == pModel && mesh.bone == bone)
			return mesh.bValid ? &mesh : nullptr;
	}
	MagMesh &mesh = s_magMeshes[s_magNext];
	s_magNext = (s_magNext + 1) % ARRAYSIZE(s_magMeshes);
	mesh = MagMesh();
	mesh.pModel = pModel;
	mesh.bone = bone;
	const MDLHandle_t handle = modelinfo->GetCacheHandle(pModel);
	const vertexFileHeader_t *pVvd = handle != MDLHANDLE_INVALID ? mdlcache->GetVertexData(handle) : nullptr;
	if (!pVvd || bone >= hdr->numbones())
		return nullptr;
	const mstudiovertex_t *pVerts = pVvd->GetVertexData();
	const matrix3x4_t &poseToBone = hdr->pBone(bone)->poseToBone;
	mesh.mins.Init(FLT_MAX, FLT_MAX, FLT_MAX);
	mesh.maxs.Init(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	int found = 0;
	for (int i = 0; pVerts && i < pVvd->numLODVertexes[0]; ++i)
	{
		const mstudioboneweight_t &w = pVerts[i].m_BoneWeights;
		float weight = 0.0f;
		for (int k = 0; k < w.numbones && k < MAX_NUM_BONES_PER_VERT; ++k)
		{
			if (w.bone[k] == bone)
				weight += w.weight[k];
		}
		if (weight < 0.5f)
			continue;
		Vector local;
		VectorTransform(pVerts[i].m_vecPosition, poseToBone, local);
		VectorMin(mesh.mins, local, mesh.mins);
		VectorMax(mesh.maxs, local, mesh.maxs);
		++found;
	}
	if (found < 8)
		return nullptr;
	// A little room round it, so the scan takes the magazine's own edges.
	mesh.mins -= Vector(0.4f, 0.4f, 0.4f);
	mesh.maxs += Vector(0.4f, 0.4f, 0.4f);
	mesh.bValid = true;
	return &mesh;
}

// The band as the magazine's own box (its bone's axes), the scan running along its longest side: t 0 to 1 from one end
// to the other, width in units (a width of 0: the whole box).
static void AddMeshBand(NeoCyberGunPass &pass, const matrix3x4_t &boneToWorld, const MagMesh &mesh, float t, float width,
	const Color &color, float alpha)
{
	if (pass.count >= NEO_CYBER_GUN_BANDS || alpha <= 0.01f)
		return;
	Vector axis[3], origin;
	float scale[3];
	for (int a = 0; a < 3; ++a)
	{
		MatrixGetColumn(boneToWorld, a, axis[a]);
		scale[a] = VectorNormalize(axis[a]);
	}
	MatrixGetColumn(boneToWorld, 3, origin);
	int longest = 0;
	for (int a = 1; a < 3; ++a)
	{
		if ((mesh.maxs[a] - mesh.mins[a]) * scale[a] > (mesh.maxs[longest] - mesh.mins[longest]) * scale[longest])
			longest = a;
	}
	NeoCyberGunBand &band = pass.bands[pass.count++];
	band.planeCount = 0;
	for (int a = 0; a < 3; ++a)
	{
		float lo = mesh.mins[a], hi = mesh.maxs[a];
		if (a == longest && width > 0.0f)
		{
			const float along = hi - t * (hi - lo), half = 0.5f * width / Max(scale[a], 0.001f);
			lo = along - half;
			hi = along + half;
		}
		const float base = DotProduct(axis[a], origin);
		SetGunPlane(band.planes[band.planeCount++], axis[a], base + lo * scale[a]);
		SetGunPlane(band.planes[band.planeCount++], -axis[a], -(base + hi * scale[a]));
	}
	band.color = color;
	band.alpha = alpha;
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

static void SetGunPlane(float plane[4], const Vector &n, float d)
{
	plane[0] = n.x;
	plane[1] = n.y;
	plane[2] = n.z;
	plane[3] = d;
}

// The magazine box's size: half its width, how far down and up it reaches from the bone.
static void MagBox(float &half, float &down, float &up)
{
	half = 3.0f;
	down = 7.0f;
	up = 2.0f;
	sscanf(cl_neo_hud_gun_mag_box.GetString(), "%f %f %f", &half, &down, &up);
}

// A box round the magazine, square to the view (its sides along the view's right and up), from depth `from` to depth
// `to` (units ahead of the eye).
static void AddMagBand(NeoCyberGunPass &pass, const Vector &mag, float from, float to, const Color &color, float alpha)
{
	if (pass.count >= NEO_CYBER_GUN_BANDS || alpha <= 0.01f || to <= from)
		return;
	float half, down, up;
	MagBox(half, down, up);
	const Vector &forward = CurrentViewForward(), &right = CurrentViewRight(), &upward = CurrentViewUp();
	const float eye = DotProduct(forward, CurrentViewOrigin()), r = DotProduct(right, mag), u = DotProduct(upward, mag);
	NeoCyberGunBand &band = pass.bands[pass.count++];
	SetGunPlane(band.planes[0], forward, eye + from);
	SetGunPlane(band.planes[1], -forward, -(eye + to));
	SetGunPlane(band.planes[2], right, r - half);
	SetGunPlane(band.planes[3], -right, -(r + half));
	SetGunPlane(band.planes[4], upward, u - down);
	SetGunPlane(band.planes[5], -upward, -(u + up));
	band.planeCount = 6;
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

	// The gun's points and magazine bone, looked up by name once per model; its magazine measured then too, on the gun's
	// first frame rather than its first reload (a hitch mid-fight).
	static const char *const s_muzzle[] = { "muzzle", "1" }, *const s_eject[] = { "eject", "2" };
	// Magazine bones: the AA13's drum, the Supa 7's "Shell" (the shell going in).
	static const char *const s_mag[] = { "Clip", "Clip01", "clip", "mag", "Drum", "Shell", "v_weapon.MP5_Clip", "ValveBiped.clip" };
	static struct { const model_t *pModel = nullptr; int muzzle = -1, eject = -1, magBone = -1; } s_gun;
	if (s_gun.pModel != pViewModel->GetModel())
	{
		s_gun.pModel = pViewModel->GetModel();
		s_gun.muzzle = AttachmentIndex(hdr, s_muzzle, ARRAYSIZE(s_muzzle));
		s_gun.eject = AttachmentIndex(hdr, s_eject, ARRAYSIZE(s_eject));
		s_gun.magBone = BoneIndex(pViewModel, s_mag, ARRAYSIZE(s_mag));
		if (s_gun.magBone >= 0)
			MagMeshOf(pViewModel, hdr, s_gun.magBone);
	}

	// Nothing to draw or place: no bone setup. The points' only reader is the muzzle count (stubbed off by default); the
	// band shows on reload or with the debug box. Custom clip planes can't change mid-scene under fast clipping (as the
	// sight glass's split), and the box needs six. Only a real magazine bone: the ejection port's fallback would scan the
	// hands.
	const NC::Senses &s = *pSenses;
	const bool bPoints = cl_neo_hud_gun_count.GetBool();
	const bool bBand = cl_neo_hud_gun.GetBool() && (s.bReloading || cl_neo_hud_gun_debug.GetBool()) && s_gun.magBone >= 0
		&& !materials->UsingFastClipping() && g_pMaterialSystemHardwareConfig->MaxUserClipPlanes() >= NEO_CYBER_GUN_PLANES;
	if (!bPoints && !bBand)
		return false;

	// The points, as drawn: this frame's pose (attachment bones included).
	pViewModel->SetupBones(nullptr, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime);
	Vector world[NEO_GUN__COUNT];
	bool bHas[NEO_GUN__COUNT] = { s_gun.muzzle >= 0, s_gun.magBone >= 0, s_gun.eject >= 0 };
	if (bHas[NEO_GUN_MUZZLE])
		AttachmentAt(pViewModel, hdr, s_gun.muzzle, world[NEO_GUN_MUZZLE]);
	if (bHas[NEO_GUN_EJECT])
		AttachmentAt(pViewModel, hdr, s_gun.eject, world[NEO_GUN_EJECT]);
	if (bHas[NEO_GUN_MAG])
		MatrixGetColumn(pViewModel->GetBone(s_gun.magBone), 3, world[NEO_GUN_MAG]);
	else if (bHas[NEO_GUN_EJECT])
	{
		world[NEO_GUN_MAG] = world[NEO_GUN_EJECT];
		bHas[NEO_GUN_MAG] = true;
	}

	if (bPoints)
	{
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
	}
	if (!bBand)
		return false;

	const float now = gpGlobals->realtime;
	// The magazine's own box, measured from the model (the fallback below when a model gives none).
	const int magBone = s_gun.magBone;
	const MagMesh *pMesh = MagMeshOf(pViewModel, hdr, magBone);
	if (pMesh)
	{
		const matrix3x4_t &boneToWorld = pViewModel->GetBone(magBone);
		if (cl_neo_hud_gun_debug.GetBool())
		{
			AddMeshBand(pass, boneToWorld, *pMesh, 0.0f, 0.0f, color, 0.6f);
		}
		else if (s.bReloading)
		{
			const float t = fmodf(Max(0.0f, now - s.reloadStart), GUN_SCAN_EVERY) / GUN_SCAN_TIME;
			if (t < 1.0f)
				AddMeshBand(pass, boneToWorld, *pMesh, t, GUN_SCAN_WIDTH, NC::WARN, GUN_SCAN_ALPHA * (1.0f - 0.5f * t));
		}
		return pass.count > 0;
	}
	float half, down, up;
	MagBox(half, down, up);
	const float depth = DotProduct(CurrentViewForward(), world[NEO_GUN_MAG] - CurrentViewOrigin());
	const float front = depth + half, back = Max(1.0f, depth - half);
	if (cl_neo_hud_gun_debug.GetBool())
	{
		AddMagBand(pass, world[NEO_GUN_MAG], back, front, color, 0.6f);
	}
	else if (s.bReloading)
	{
		// The reload: a band through the magazine from front to back, again each second until it's done.
		const float t = fmodf(Max(0.0f, now - s.reloadStart), GUN_SCAN_EVERY) / GUN_SCAN_TIME;
		if (t < 1.0f)
		{
			const float at = front - t * (front - back);
			AddMagBand(pass, world[NEO_GUN_MAG], at - GUN_SCAN_WIDTH * 0.5f, at + GUN_SCAN_WIDTH * 0.5f, NC::WARN,
				GUN_SCAN_ALPHA * (1.0f - 0.5f * t));
		}
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
	for (int i = 0; i < b.planeCount; ++i)
		pRenderContext->PushCustomClipPlane(b.planes[i]);
	s_pushed = b.planeCount;
	modelrender->ForcedMaterialOverride(pMaterial);
}

void NeoCyberGunBandEnd()
{
	modelrender->ForcedMaterialOverride(nullptr);
	CMatRenderContextPtr pRenderContext(materials);
	for (; s_pushed > 0; --s_pushed)
		pRenderContext->PopCustomClipPlane();
}

bool NeoCyberGunPointOnScreen(NeoCyberGunPoint point, Vector2D &out)
{
	if (s_points.frame < gpGlobals->framecount - 1 || !s_points.bOn[point])
		return false;
	out = s_points.at[point];
	return true;
}
