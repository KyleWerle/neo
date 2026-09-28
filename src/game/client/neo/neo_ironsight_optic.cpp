#include "cbase.h"
#include "neo_ironsight_optic.h"
#include "neo_ironsights.h"
#include "neo_predicted_viewmodel.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "viewrender.h"
#include "ivrenderview.h"
#include "igamesystem.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "KeyValues.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_ironsight_optic("cl_neo_ironsight_optic", "1", FCVAR_ARCHIVE,
	"Optics on the sights: 1 = live magnified view in the lens, 0 = full-screen scope overlay.", true, 0, true, 1);

static constexpr int OPTIC_RT_SIZE = 512;
static constexpr const char *OPTIC_RT_NAME = "_rt_NeoIronsightOptic";

// Allocates the optic's render target once at startup, as view.cpp does for its own targets, so
// scoping in never pays for it mid-game.
class CNeoIronsightOpticSystem : public CAutoGameSystem
{
public:
	CNeoIronsightOpticSystem() : CAutoGameSystem("CNeoIronsightOpticSystem") {}

	void PostInit() override
	{
		materials->BeginRenderTargetAllocation();
		m_texture.Init(materials->CreateNamedRenderTargetTextureEx2(OPTIC_RT_NAME, OPTIC_RT_SIZE, OPTIC_RT_SIZE,
			RT_SIZE_NO_CHANGE, materials->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, CREATERENDERTARGETFLAGS_HDR));
		materials->EndRenderTargetAllocation();
	}

	void Shutdown() override
	{
		m_texture.Shutdown();
	}

	CTextureReference m_texture;
};
static CNeoIronsightOpticSystem s_opticSystem;

// The local player's active weapon data, if it has an optic and ironsights apply to it.
static const CNEOWeaponInfo *LocalOpticWeaponData()
{
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (!pPlayer || !pPlayer->IsAlive() || !pPlayer->IsInAim())
	{
		return nullptr;
	}
	auto *pWeapon = dynamic_cast<CNEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon());
	if (!pWeapon)
	{
		return nullptr;
	}
	const CNEOWeaponInfo &data = pWeapon->GetNEOWpnData();
	return (data.m_flIronOpticFov > 0.0f && NeoIronsightsActive(data)) ? &data : nullptr;
}

NeoIronsightOpticMode NeoGetIronsightOpticMode()
{
	if (!LocalOpticWeaponData())
	{
		return NEO_OPTIC_NONE;
	}
	if (!cl_neo_ironsight_optic.GetBool())
	{
		return NEO_OPTIC_OVERLAY;
	}
	// The live view appears once the gun has (nearly) settled onto the sights.
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	const auto *pViewModel = pPlayer ? pPlayer->GetNEOViewModel() : nullptr;
	return (pViewModel && pViewModel->GetIronsightBlend() >= 0.9f) ? NEO_OPTIC_PIP : NEO_OPTIC_NONE;
}

// Renders the magnified view from the eye into the optic's render target, like a point_camera monitor.
void CViewRender::DrawNeoIronsightOptic(const CViewSetup &mainView)
{
	const CNEOWeaponInfo *pData = LocalOpticWeaponData();
	if (!pData || NeoGetIronsightOpticMode() != NEO_OPTIC_PIP || !s_opticSystem.m_texture.IsValid())
	{
		return;
	}

	CViewSetup opticView = mainView;
	opticView.x = 0;
	opticView.y = 0;
	opticView.width = OPTIC_RT_SIZE;
	opticView.height = OPTIC_RT_SIZE;
	opticView.fov = pData->m_flIronOpticFov;
	opticView.m_flAspectRatio = 1.0f;
	opticView.m_bOrtho = false;
	opticView.m_bViewToProjectionOverride = false;

	Frustum frustum;
	render->Push3DView(opticView, VIEW_CLEAR_DEPTH | VIEW_CLEAR_COLOR, s_opticSystem.m_texture, (VPlane *)frustum);
	ViewDrawScene(false, SKYBOX_2DSKYBOX_VISIBLE, opticView, 0, VIEW_MONITOR);
	render->PopView(frustum);
}

//-----------------------------------------------------------------------------
// Draws the optic: the live view as a round picture over the lens with a reticle, or the full-screen overlay.
//-----------------------------------------------------------------------------
class CNeoHudIronsightOptic : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE(CNeoHudIronsightOptic, vgui::Panel);

public:
	CNeoHudIronsightOptic(const char *pElementName)
		: CHudElement(pElementName), BaseClass(nullptr, "NeoHudIronsightOptic")
	{
		SetParent(g_pClientMode->GetViewport());
		SetHiddenBits(HIDEHUD_PLAYERDEAD);
	}

	bool ShouldDraw() override
	{
		return NeoGetIronsightOpticMode() != NEO_OPTIC_NONE && CHudElement::ShouldDraw();
	}

protected:
	void ApplySchemeSettings(vgui::IScheme *pScheme) override
	{
		BaseClass::ApplySchemeSettings(pScheme);
		FillScreen();
		SetPaintBackgroundEnabled(false);
	}

	void OnScreenSizeChanged(int iOldWide, int iOldTall) override
	{
		BaseClass::OnScreenSizeChanged(iOldWide, iOldTall);
		FillScreen();
	}

	void Paint() override
	{
		const CNEOWeaponInfo *pData = LocalOpticWeaponData();
		if (!pData)
		{
			return;
		}
		if (NeoGetIronsightOpticMode() == NEO_OPTIC_OVERLAY)
		{
			PaintOverlay(*pData);
		}
		else
		{
			PaintPictureInPicture(*pData);
		}
	}

private:
	void FillScreen()
	{
		int wide, tall;
		vgui::surface()->GetScreenSize(wide, tall);
		SetBounds(0, 0, wide, tall);
	}

	// A texture id for a file, reusing one id and re-pointing it when the file changes.
	static int FileTexture(int &id, char (&current)[MAX_WEAPON_STRING], const char *pszFile)
	{
		if (id < 0)
		{
			id = vgui::surface()->CreateNewTextureID();
			current[0] = 0;
		}
		if (V_strcmp(current, pszFile) != 0)
		{
			vgui::surface()->DrawSetTextureFile(id, pszFile, true, false);
			V_strncpy(current, pszFile, sizeof(current));
		}
		return id;
	}

	// Full-screen scope: a square texture at full height, black bars at the sides.
	void PaintOverlay(const CNEOWeaponInfo &data)
	{
		int wide, tall;
		GetSize(wide, tall);
		const int left = (wide - tall) / 2;
		vgui::surface()->DrawSetColor(0, 0, 0, 255);
		vgui::surface()->DrawFilledRect(0, 0, left, tall);
		vgui::surface()->DrawFilledRect(left + tall, 0, wide, tall);
		vgui::surface()->DrawSetColor(255, 255, 255, 255);
		vgui::surface()->DrawSetTexture(FileTexture(m_overlayTexture, m_szOverlayFile, data.m_szIronOpticOverlay));
		vgui::surface()->DrawTexturedRect(left, 0, left + tall, tall);
	}

	// The live view as a circle at screen centre (where the lens sits on the sights), then the reticle.
	void PaintPictureInPicture(const CNEOWeaponInfo &data)
	{
		if (m_liveTexture < 0)
		{
			KeyValues *pVMT = new KeyValues("UnlitGeneric");
			pVMT->SetString("$basetexture", OPTIC_RT_NAME);
			pVMT->SetInt("$vertexcolor", 1);
			m_liveMaterial.Init("__neo_ironsight_optic", TEXTURE_GROUP_OTHER, pVMT);
			m_liveTexture = vgui::surface()->CreateNewTextureID(true);
			g_pMatSystemSurface->DrawSetTextureMaterial(m_liveTexture, m_liveMaterial);
		}

		int wide, tall;
		GetSize(wide, tall);
		const float centreX = wide * 0.5f;
		const float centreY = tall * 0.5f;
		const float radius = data.m_flIronOpticRadius * tall;

		constexpr int SEGMENTS = 64;
		vgui::Vertex_t circle[SEGMENTS];
		for (int i = 0; i < SEGMENTS; ++i)
		{
			const float angle = 2.0f * M_PI_F * i / SEGMENTS;
			const float c = cosf(angle), s = sinf(angle);
			circle[i].Init(Vector2D(centreX + radius * c, centreY + radius * s), Vector2D(0.5f + 0.5f * c, 0.5f + 0.5f * s));
		}
		vgui::surface()->DrawSetColor(255, 255, 255, 255);
		vgui::surface()->DrawSetTexture(m_liveTexture);
		vgui::surface()->DrawTexturedPolygon(SEGMENTS, circle);

		const int x0 = RoundFloatToInt(centreX - radius), y0 = RoundFloatToInt(centreY - radius);
		const int x1 = RoundFloatToInt(centreX + radius), y1 = RoundFloatToInt(centreY + radius);
		if (data.m_szIronOpticReticle[0])
		{
			vgui::surface()->DrawSetTexture(FileTexture(m_reticleTexture, m_szReticleFile, data.m_szIronOpticReticle));
			vgui::surface()->DrawTexturedRect(x0, y0, x1, y1);
		}
		else
		{
			// Default reticle: a lens rim and a small red dot.
			vgui::surface()->DrawSetColor(0, 0, 0, 255);
			vgui::surface()->DrawOutlinedCircle(RoundFloatToInt(centreX), RoundFloatToInt(centreY), RoundFloatToInt(radius), SEGMENTS);
			vgui::surface()->DrawSetColor(255, 40, 40, 255);
			vgui::surface()->DrawFilledRect(RoundFloatToInt(centreX) - 1, RoundFloatToInt(centreY) - 1,
				RoundFloatToInt(centreX) + 2, RoundFloatToInt(centreY) + 2);
		}
	}

	int m_overlayTexture = -1;
	char m_szOverlayFile[MAX_WEAPON_STRING] = "";
	int m_reticleTexture = -1;
	char m_szReticleFile[MAX_WEAPON_STRING] = "";
	int m_liveTexture = -1;
	CMaterialReference m_liveMaterial;
};

DECLARE_HUDELEMENT(CNeoHudIronsightOptic);
