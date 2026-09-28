#pragma once

// Optics drawn by us over a weapon's lens rather than on its mesh (neo_ironsight_optic_disc.cpp):
//   any optic while cloaked, since the cloak override replaces the lens material;
//   "lens_disc" lenses (e.g. the Jitte's), fading in on the sights over their untouched lens;
//   "window" sight glass (red dots, holo sights), only while cloaked: the glass shows exactly the world
//     behind it, so the cloak doesn't smear the view through the sight.
// Shared with neo_ironsight_optic.cpp, which renders the optic view.

class C_NEO_Player;
class CNEOWeaponInfo;
class CViewSetup;
class ITexture;

// The player whose view is on screen: the local player, or the one spectated in first person.
C_NEO_Player *NeoIronsightOpticViewPlayer();

// The optic's render target, or null if it could not be made.
ITexture *NeoIronsightOpticTexture();

// For "window" optics: points the optic camera from the eye at the sight glass and sizes it to just
// cover the glass, remembering the projection so the glass can look up exactly what lies behind each
// of its points. False if the glass is not in front of the eye.
bool NeoIronsightWindowCamera(const CViewSetup &mainView, const CNEOWeaponInfo &data, QAngle &angles, float &fov);
