#pragma once

// Optics for weapons whose script has an "IronsightOptic" block (e.g. the MX).
//   Live view (cl_neo_ironsight_optic 1, and the block names a "lens" material): a magnified view,
//     rendered like a point_camera monitor (CViewRender::DrawNeoIronsightOptic), is drawn on the lens
//     mesh itself, with the reticle over it; the gun's own geometry frames and occludes it. It runs
//     whenever the weapon is out, at the hip too, pointing where the gun points.
//   Overlay (cl_neo_ironsight_optic 0, or no lens): while aiming, the gun hides and a full-screen scope
//     texture is drawn, the way the scoped rifles do it.
// It follows whoever is on screen: the local player, or the player spectated in first person.
// Everything here is client-only; weapons without the block, or ironsights off, are untouched.

class CNEOWeaponInfo;
class IMaterial;
class IMaterialVar;
class ITexture;

enum NeoIronsightOpticMode
{
	NEO_OPTIC_NONE,
	NEO_OPTIC_PIP,
	NEO_OPTIC_OVERLAY,
};

// The optic mode the current view (local player or first-person spectate target) is in right now.
NeoIronsightOpticMode NeoGetIronsightOpticMode();

// While the live view is active, puts it on the weapon's lens material for the lifetime of a viewmodel
// draw: the lens's $basetexture becomes the optic render and its $detail the reticle. The lens material
// must be UnlitGeneric with a $detail declared (see dev-assets). Restored when it goes out of scope.
// With "lens_disc" the lens material is left as it is and only hidden for the draw instead; the optic
// is then drawn over it by NeoIronsightDrawOpticDisc.
class NeoIronsightOpticLens
{
public:
	explicit NeoIronsightOpticLens(const CNEOWeaponInfo *pData);
	~NeoIronsightOpticLens();
private:
	IMaterialVar *m_pBase = nullptr;
	IMaterialVar *m_pDetail = nullptr;
	IMaterialVar *m_pBlend = nullptr;
	ITexture *m_pOriginalBase = nullptr;
	ITexture *m_pOriginalDetail = nullptr;
	float m_flOriginalBlend = 0.0f;
	IMaterial *m_pHiddenLens = nullptr;
};

class C_BaseAnimating;
// Draws the live optic and reticle as a disc on the lens surface (the weapon's "lens_map" and
// "lens_circle"). Call after the gun. While cloaked it fades out towards the rim, since the cloak
// override replaces the lens material; with "lens_disc" it is drawn solid the rest of the time too, and
// glitches off the sights (ironsightBlend below 1).
void NeoIronsightDrawOpticDisc(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, float ironsightBlend);
