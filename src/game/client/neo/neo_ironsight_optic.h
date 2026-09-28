#pragma once

// Optics for weapons whose script has an "IronsightOptic" block (e.g. the MX).
//   Live view (cl_neo_ironsight_optic 1, and the block names a "lens" material): a magnified view,
//     rendered like a point_camera monitor (CViewRender::DrawNeoIronsightOptic), is drawn as a disc over
//     the untouched lens ("lens_disc"; neo_ironsight_optic_disc.cpp), with the reticle over it. It fades
//     in on the sights and shows at the hip while the gun is drawn over (cloak, thermals).
//   Sight glass ("window"): while the gun is drawn over, the glass shows the world behind it.
//   Overlay (cl_neo_ironsight_optic 0, or no lens): while aiming, the gun hides and a full-screen scope
//     texture is drawn, the way the scoped rifles do it.
// It follows whoever is on screen: the local player, or the player spectated in first person.
// Everything here is client-only; weapons without the block, or ironsights off, are untouched.

class CNEOWeaponInfo;

enum NeoIronsightOpticMode
{
	NEO_OPTIC_NONE,
	NEO_OPTIC_PIP,
	NEO_OPTIC_OVERLAY,
};

// The optic mode the current view (local player or first-person spectate target) is in right now.
NeoIronsightOpticMode NeoGetIronsightOpticMode();

class C_BaseAnimating;
class C_NEO_Player;
class ITexture;

// The player whose view is on screen: the local player, or the one spectated in first person.
C_NEO_Player *NeoIronsightOpticViewPlayer();

// The optic's render target, or null if it could not be made.
ITexture *NeoIronsightOpticTexture();

// True while this player sees in thermals (a support in vision mode): their gun is drawn with the opaque
// thermal material then, which covers the lens just as the cloak does.
bool NeoIronsightInThermals(const C_NEO_Player *pPlayer);

// Draws the live optic and reticle as a disc on the lens surface (the weapon's "lens_map" and
// "lens_circle"). Call after the gun. While cloaked it fades out towards the rim, since the cloak
// override replaces the lens material; in thermals (bThermal) it is drawn whole over the thermal gun, and
// the thermal filter later colours it like the rest of the screen. With "lens_disc" it is drawn uncloaked
// too, fading in over the lens as the gun comes onto the sights (ironsightBlend), so the hip shows the
// lens as it is; cloaked it shows at the hip as well. "one_pane" and "gyro" glass get
// their reticle drawn here always.
enum NeoIronsightLensPart
{
	NEO_LENS_ALL,		// the view, then the reticle
	NEO_LENS_VIEW,
	NEO_LENS_RETICLE,
};
void NeoIronsightDrawOpticDisc(C_BaseAnimating *pViewModel, const CNEOWeaponInfo &data, bool bCloaked, bool bThermal,
	float ironsightBlend, NeoIronsightLensPart part = NEO_LENS_ALL);
