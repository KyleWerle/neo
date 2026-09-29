#include "cbase.h"
#include "neo_ironsight_augment.h"
#include "neo_ironsight_profile.h"
#include "neo_ironsight_optic.h"
#include "neo_ironsights.h"
#include "neo_gunplay_crosshair.h"
#include "neo_crosshair.h"
#include "c_neo_player.h"
#include "neo_player_shared.h"
#include "weapon_neobasecombatweapon.h"
#include "view.h"
#include "view_shared.h"
#include "igamesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static constexpr float AUGMENT_FADE_TIME = 0.12f;	// seconds to fade in or out with the aim
static constexpr int AUGMENT_RT_SIZE = 512;		// the optic render target's size (neo_ironsight_optic.cpp)

static float s_flFade = 0.0f;	// 0 = gone, 1 = fully up

// The augment weapon's data if the given player is holding one with ironsights on.
static const CNEOWeaponInfo *AugmentData(C_BaseCombatWeapon *pActive)
{
	static ConVarRef cl_neo_ironsights("cl_neo_ironsights");
	auto *pWeapon = dynamic_cast<CNEOBaseCombatWeapon *>(pActive);
	if (!pWeapon || !NeoGunplayEnabled() || !cl_neo_ironsights.GetBool())
	{
		return nullptr;
	}
	const CNEOWeaponInfo &data = pWeapon->GetNEOWpnData();
	return data.m_flIronAugmentMagnification > 0.0f ? &data : nullptr;
}

bool NeoIronsightAugmentView(const CViewSetup &mainView, CViewSetup &augmentView)
{
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	const CNEOWeaponInfo *pData = (pPlayer && pPlayer->IsAlive()) ? AugmentData(pPlayer->GetActiveWeapon()) : nullptr;
	if (!pData || (!pPlayer->IsInAim() && s_flFade <= 0.0f) || !NeoIronsightOpticTexture())
	{
		return false;
	}
	// The part of the main view the window covers, magnified: same centre, the window's share of the
	// field of view divided by the zoom, at the screen's aspect ratio (the window is the screen scaled down).
	augmentView = mainView;
	const float tanHalf = tanf(DEG2RAD(mainView.fov * 0.5f)) * pData->m_flIronAugmentSize / pData->m_flIronAugmentMagnification;
	augmentView.fov = RAD2DEG(2.0f * atanf(tanHalf));
	augmentView.m_flAspectRatio = mainView.height > 0 ? static_cast<float>(mainView.width) / mainView.height : 1.0f;
	augmentView.x = 0;
	augmentView.y = 0;
	augmentView.width = AUGMENT_RT_SIZE;
	augmentView.height = AUGMENT_RT_SIZE;
	augmentView.m_bOrtho = false;
	augmentView.m_bViewToProjectionOverride = false;
	return true;
}

//-----------------------------------------------------------------------------
// The window, drawn into the scene just before the viewmodel (the gun and its muzzle flash go over it)
// and before post-processing, so vision modes
// (night, motion, thermal) and the rest of the screen's look apply to it just as to the world around it.
// It is a quad in front of the eye spanning exactly its part of the screen, so it needs no projection of
// its own; opaque inside, fading to translucent over the edge band.
//-----------------------------------------------------------------------------
static IMaterial *WindowMaterial()
{
	static CMaterialReference s_material;
	if (!s_material.IsValid())
	{
		KeyValues *pVMT = new KeyValues("UnlitGeneric");
		pVMT->SetString("$basetexture", NeoIronsightOpticTexture()->GetName());
		pVMT->SetInt("$translucent", 1);
		pVMT->SetInt("$vertexcolor", 1);
		pVMT->SetInt("$vertexalpha", 1);
		pVMT->SetInt("$ignorez", 1);
		pVMT->SetInt("$nocull", 1);
		s_material.Init("__neo_ironsight_augment_window", TEXTURE_GROUP_OTHER, pVMT);
	}
	return s_material;
}

void NeoIronsightDrawAugmentWindow(const CViewSetup &mainView)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_AUGMENT, "NeoIronsightDrawAugmentWindow");
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	const CNEOWeaponInfo *pData = (pPlayer && pPlayer->IsAlive()) ? AugmentData(pPlayer->GetActiveWeapon()) : nullptr;
	if (!pData || s_flFade <= 0.0f || !NeoIronsightOpticTexture())
	{
		return;
	}
	const float alpha = pData->m_flIronAugmentAlpha * NeoSmoothStep(s_flFade);
	// The window's half-extent on a plane in front of the eye, from the main view's field of view.
	constexpr float DISTANCE = 16.0f;
	const float aspect = mainView.height > 0 ? static_cast<float>(mainView.width) / mainView.height : 1.0f;
	const float halfW = DISTANCE * tanf(DEG2RAD(mainView.fov * 0.5f)) * pData->m_flIronAugmentSize;
	const float halfH = halfW / aspect;
	const Vector centre = mainView.origin + CurrentViewForward() * DISTANCE;
	const Vector &right = CurrentViewRight(), &up = CurrentViewUp();
	// The edge band as a fraction of each half-extent, over the shorter side, as the HUD frame uses it.
	const float edgeFraction = pData->m_flIronAugmentEdge;
	const float edgeW = edgeFraction * Min(halfW, halfH) / halfW, edgeH = edgeFraction * Min(halfW, halfH) / halfH;

	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->Bind(WindowMaterial());
	constexpr int RINGS = 8;	// rings of the edge band, plus the opaque middle
	// Rings first..last-1, each a mitred frame of four trapezoids between its outer rectangle (alpha a0)
	// and its inner one (a1); then the opaque middle if asked.
	const auto draw = [&](int first, int last, bool bMiddle) {
		IMesh *pMesh = pRenderContext->GetDynamicMesh();
		CMeshBuilder meshBuilder;
		meshBuilder.Begin(pMesh, MATERIAL_QUADS, 4 * (last - first) + (bMiddle ? 1 : 0));
		// A point by its position across the window, -1..1 each way; u, v are the render target's coordinates.
		const auto vertex = [&](float sx, float sy, float a) {
			const Vector position = centre + right * (sx * halfW) + up * (sy * halfH);
			meshBuilder.Position3fv(position.Base());
			meshBuilder.Color4f(1.0f, 1.0f, 1.0f, a);
			meshBuilder.TexCoord2f(0, 0.5f + 0.5f * sx, 0.5f - 0.5f * sy);
			meshBuilder.AdvanceVertex();
		};
		for (int ring = first; ring < last; ++ring)
		{
			const float t0 = float(ring) / RINGS, t1 = float(ring + 1) / RINGS;
			const float a0 = alpha * NeoSmoothStep(t0), a1 = alpha * NeoSmoothStep(t1);
			const float ox = 1.0f - edgeW * t0, oy = 1.0f - edgeH * t0;	// outer rectangle's corner
			const float ix = 1.0f - edgeW * t1, iy = 1.0f - edgeH * t1;	// inner rectangle's corner
			vertex(-ox, oy, a0); vertex(ox, oy, a0); vertex(ix, iy, a1); vertex(-ix, iy, a1);		// top
			vertex(ox, -oy, a0); vertex(-ox, -oy, a0); vertex(-ix, -iy, a1); vertex(ix, -iy, a1);	// bottom
			vertex(-ox, -oy, a0); vertex(-ox, oy, a0); vertex(-ix, iy, a1); vertex(-ix, -iy, a1);	// left
			vertex(ox, oy, a0); vertex(ox, -oy, a0); vertex(ix, -iy, a1); vertex(ix, iy, a1);		// right
		}
		if (bMiddle)
		{
			const float ix = 1.0f - edgeW, iy = 1.0f - edgeH;
			vertex(-ix, iy, alpha); vertex(ix, iy, alpha); vertex(ix, -iy, alpha); vertex(-ix, -iy, alpha);
		}
		meshBuilder.End();
		pMesh->Draw();
	};
	// The outer half of the fade, where the world around shows through, keeps the teammate outlines.
	draw(0, RINGS / 2, false);
#ifdef GLOWS_ENABLE
	// The rest is marked like the viewmodel in the stencil, so the outlines (drawn later, for the unzoomed
	// view) leave it alone instead of landing offset from the magnified players.
	pRenderContext->SetStencilEnable(true);
	pRenderContext->SetStencilReferenceValue(NEO_GLOW_VIEWMODEL);
	pRenderContext->SetStencilWriteMask(NEO_GLOW_VIEWMODEL);
	pRenderContext->SetStencilCompareFunction(STENCILCOMPARISONFUNCTION_ALWAYS);
	pRenderContext->SetStencilPassOperation(STENCILOPERATION_REPLACE);
	pRenderContext->SetStencilFailOperation(STENCILOPERATION_KEEP);
	pRenderContext->SetStencilZFailOperation(STENCILOPERATION_REPLACE);
#endif
	draw(RINGS / 2, RINGS, true);
#ifdef GLOWS_ENABLE
	pRenderContext->SetStencilEnable(false);
#endif
}

//-----------------------------------------------------------------------------
// The HUD side: the window, its frame of growing dotted squares, the readout and the spread ring.
//-----------------------------------------------------------------------------
// A rectangle of dots, dotSize square, every spacing pixels along its edges.
static void DrawDottedRect(int x0, int y0, int x1, int y1, int spacing, int dotSize)
{
	constexpr int MAX_DOTS = 1024;
	vgui::IntRect dots[MAX_DOTS];
	int count = 0;
	const auto add = [&](int x, int y) {
		if (count < MAX_DOTS)
		{
			dots[count++] = { x, y, x + dotSize, y + dotSize };
		}
	};
	for (int x = x0; x <= x1; x += spacing)
	{
		add(x, y0);
		add(x, y1);
	}
	for (int y = y0 + spacing; y < y1; y += spacing)
	{
		add(x0, y);
		add(x1, y);
	}
	vgui::surface()->DrawFilledRectArray(dots, count);
}

// Distance along the view to whatever the crosshair is on, in metres (0 if nothing within ~200 m).
static float RangeMetres(C_NEO_Player *pPlayer)
{
	constexpr float MAX_RANGE = 8192.0f;
	constexpr float METRES_PER_UNIT = 0.0254f;
	trace_t trace;
	UTIL_TraceLine(MainViewOrigin(), MainViewOrigin() + MainViewForward() * MAX_RANGE, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &trace);
	return (trace.fraction < 1.0f) ? trace.fraction * MAX_RANGE * METRES_PER_UNIT : 0.0f;
}

bool NeoIronsightPaintAugment(C_NEOBaseCombatWeapon *pWeapon, const Color &color, int x, int y)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_AUGMENT, "NeoIronsightPaintAugment");
	C_NEO_Player *pPlayer = pWeapon ? dynamic_cast<C_NEO_Player *>(pWeapon->GetOwner()) : nullptr;
	const CNEOWeaponInfo *pData = pPlayer ? AugmentData(pWeapon) : nullptr;
	const bool bAiming = pData && pPlayer->IsAlive() && pPlayer->IsInAim();
	s_flFade = Approach(bAiming ? 1.0f : 0.0f, s_flFade, gpGlobals->frametime / AUGMENT_FADE_TIME);
	if (!pData)
	{
		return false;
	}
	// No crosshair at the hip: the augment is the weapon's only aiming aid.
	if (s_flFade <= 0.0f)
	{
		return true;
	}
	const float fade = NeoSmoothStep(s_flFade);

	int screenWide, screenTall;
	vgui::surface()->GetScreenSize(screenWide, screenTall);
	const int w = RoundFloatToInt(screenWide * pData->m_flIronAugmentSize);
	const int h = RoundFloatToInt(screenTall * pData->m_flIronAugmentSize);
	const int x0 = x - w / 2, y0 = y - h / 2;
	const int edge = Max(1, RoundFloatToInt(pData->m_flIronAugmentEdge * Min(w, h) * 0.5f));
	// Dotted squares growing out from the centre to the rim, one after another, fading in and out.
	constexpr int SQUARES = 3;
	constexpr float SQUARE_PERIOD = 4.5f;	// seconds for one square to grow from the start to the rim
	for (int i = 0; i < SQUARES; ++i)
	{
		const float t = fmodf(gpGlobals->realtime / SQUARE_PERIOD + float(i) / SQUARES, 1.0f);
		const float scale = Lerp(t, 0.3f, 1.0f);
		const float strength = NeoSmoothStep(t / 0.25f) * NeoSmoothStep((1.0f - t) / 0.35f);
		const int hw = RoundFloatToInt(w * 0.5f * scale), hh = RoundFloatToInt(h * 0.5f * scale);
		vgui::surface()->DrawSetColor(color.r(), color.g(), color.b(), RoundFloatToInt(110.0f * strength * fade));
		DrawDottedRect(x - hw, y - hh, x + hw, y + hh, 8, 2);
	}

	// The readout along the bottom, inside the fade.
	static vgui::HFont s_font = vgui::INVALID_FONT;
	if (s_font == vgui::INVALID_FONT)
	{
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
		s_font = pScheme ? pScheme->GetFont("NHudOCRSmallerNoAdditive", true) : vgui::INVALID_FONT;
	}
	if (s_font != vgui::INVALID_FONT)
	{
		const float spread = (pWeapon->GetNeoWepBits() & NEO_WEP_FIREARM) ? pWeapon->GetBulletSpread().x : 0.0f;
		const float range = RangeMetres(pPlayer);
		wchar_t text[96];
		if (range > 0.0f)
		{
			V_snwprintf(text, ARRAYSIZE(text), L"AUG %.1fx   RNG %.1f m   SPR %.2f°", pData->m_flIronAugmentMagnification, range, RAD2DEG(atanf(spread)));
		}
		else
		{
			V_snwprintf(text, ARRAYSIZE(text), L"AUG %.1fx   RNG ---   SPR %.2f°", pData->m_flIronAugmentMagnification, RAD2DEG(atanf(spread)));
		}
		vgui::surface()->DrawSetTextFont(s_font);
		vgui::surface()->DrawSetTextColor(color.r(), color.g(), color.b(), RoundFloatToInt(190.0f * fade));
		vgui::surface()->DrawSetTextPos(x0 + edge / 2, y0 + h - edge / 2 - vgui::surface()->GetFontTall(s_font));
		vgui::surface()->DrawPrintText(text, V_wcslen(text));
	}

	// The crosshair: the gunplay layer's (the SMG family's box, a small cross at the centre), the spread as seen at
	// the window's zoom. Faded in with the window.
	if (NeoGunplayCrosshairLayerOn(pWeapon))
	{
		const Color faded(color.r(), color.g(), color.b(), RoundFloatToInt(color.a() * fade));
		NeoGunplayPaintCrosshairLayer(pWeapon, faded, x, y, true, pData->m_flIronAugmentMagnification);
		return true;
	}
	// Without the layer: a clear centre dot, and a dotted ring for the spread.
	const float ring = Max(3.0f, HalfInaccuracyConeInScreenPixels(pWeapon, screenWide / 2) * pData->m_flIronAugmentMagnification);
	constexpr int RING_DOTS = 48;
	vgui::IntRect dots[RING_DOTS];
	for (int i = 0; i < RING_DOTS; ++i)
	{
		const float angle = 2.0f * M_PI_F * i / RING_DOTS;
		const int dx = RoundFloatToInt(x + cosf(angle) * ring), dy = RoundFloatToInt(y + sinf(angle) * ring);
		dots[i] = { dx - 1, dy - 1, dx + 1, dy + 1 };
	}
	vgui::surface()->DrawSetColor(color.r(), color.g(), color.b(), RoundFloatToInt(200.0f * fade));
	vgui::surface()->DrawFilledRectArray(dots, RING_DOTS);
	vgui::surface()->DrawSetColor(color.r(), color.g(), color.b(), RoundFloatToInt(255.0f * fade));
	vgui::surface()->DrawFilledRect(x - 1, y - 1, x + 1, y + 1);
	return true;
}
