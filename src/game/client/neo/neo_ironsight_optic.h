#pragma once

// Optics for weapons whose script has an "IronsightOptic" block (e.g. the MX), while on the sights.
//   Picture-in-picture (cl_neo_ironsight_optic 1): the gun stays; a magnified live view fills its lens,
//     rendered like a point_camera monitor (CViewRender::DrawNeoIronsightOptic) and drawn by a HUD element.
//   Overlay (cl_neo_ironsight_optic 0): the gun hides and a full-screen scope texture is drawn, the way
//     the scoped rifles do it.
// Everything here is client-only; weapons without the block, or ironsights off, are untouched.

enum NeoIronsightOpticMode
{
	NEO_OPTIC_NONE,
	NEO_OPTIC_PIP,
	NEO_OPTIC_OVERLAY,
};

// The optic mode the local player's view is in right now.
NeoIronsightOpticMode NeoGetIronsightOpticMode();
