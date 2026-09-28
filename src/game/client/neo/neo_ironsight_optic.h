#pragma once

// Optics for weapons whose script has an "IronsightOptic" block (e.g. the MX), while on the sights.
//   Live view (cl_neo_ironsight_optic 1, and the block names a "lens" material): a magnified view,
//     rendered like a point_camera monitor (CViewRender::DrawNeoIronsightOptic), is drawn on the lens
//     mesh itself, with the reticle over it; the gun's own geometry frames and occludes it.
//   Overlay (cl_neo_ironsight_optic 0, or no lens): the gun hides and a full-screen scope texture is
//     drawn, the way the scoped rifles do it.
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

// The optic mode the local player's view is in right now.
NeoIronsightOpticMode NeoGetIronsightOpticMode();

// While the live view is active, puts it on the weapon's lens material for the lifetime of a viewmodel
// draw: the lens's $basetexture becomes the optic render and its $detail the reticle. The lens material
// must be UnlitGeneric with a $detail declared (see dev-assets). Restored when it goes out of scope.
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
