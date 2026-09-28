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
// With "lens_disc" the lens material is left untouched; NeoIronsightDrawOpticDisc draws over it instead.
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
};

class C_BaseAnimating;
class C_NEO_Player;
// True while this player sees in thermals (a support in vision mode): their gun is drawn with the opaque
// thermal material then, which covers the lens just as the cloak does.
bool NeoIronsightInThermals(const C_NEO_Player *pPlayer);

// Draws the live optic and reticle as a disc on the lens surface (the weapon's "lens_map" and
// "lens_circle"). Call after the gun. While cloaked it fades out towards the rim, since the cloak
// override replaces the lens material; in thermals (bThermal) it is drawn whole over the thermal gun, and
// the thermal filter later colours it like the rest of the screen. With "lens_disc" it is drawn uncloaked
// too, fading in over the lens as the gun comes onto the sights (ironsightBlend), so the hip shows the
// lens as it is; cloaked it shows at the hip as well. "one_pane" glass gets its reticle drawn here always.
enum NeoIronsightLensPart
{
	NEO_LENS_ALL,		// the view, then the reticle
	NEO_LENS_VIEW,
	NEO_LENS_RETICLE,
};
void NeoIronsightDrawOpticDisc(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float ironsightBlend, NeoIronsightLensPart part = NEO_LENS_ALL);
