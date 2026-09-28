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
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "materialsystem/MaterialSystemUtil.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_ironsight_optic("cl_neo_ironsight_optic", "1", FCVAR_ARCHIVE,
	"Optics on the sights: 1 = live magnified view on the lens, 0 = full-screen scope overlay.", true, 0, true, 1);

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

// Whose view this is: the local player, or the player they spectate in first person (whose gun, aim
// and viewmodel are on screen then).
static C_NEO_Player *OpticViewPlayer()
{
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	if (pLocal && pLocal->IsObserver() && pLocal->GetObserverMode() == OBS_MODE_IN_EYE)
	{
		return dynamic_cast<C_NEO_Player *>(pLocal->GetObserverTarget());
	}
	return pLocal;
}

// The viewed player's active weapon data, if it has an optic and ironsights apply to it.
static const CNEOWeaponInfo *LocalOpticWeaponData()
{
	C_NEO_Player *pPlayer = OpticViewPlayer();
	if (!pPlayer || !pPlayer->IsAlive())
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
	const CNEOWeaponInfo *pData = LocalOpticWeaponData();
	if (!pData)
	{
		return NEO_OPTIC_NONE;
	}
	if (!cl_neo_ironsight_optic.GetBool() || !pData->m_szIronOpticLens[0])
	{
		// The full-screen scope only while aiming.
		C_NEO_Player *pPlayer = OpticViewPlayer();
		return (pPlayer && pPlayer->IsInAim()) ? NEO_OPTIC_OVERLAY : NEO_OPTIC_NONE;
	}
	// The live view on the lens runs whenever the weapon is out, hip or sights, like a real optic.
	return NEO_OPTIC_PIP;
}

// The optic camera points where the gun points: the first time the gun settles on the sights (idle,
// fully aimed), the muzzle attachment's pose relative to the eye is recorded as "looking straight
// ahead". After that, at the hip or on the sights, the gun's turn from that pose (hip angle, mouse sway,
// bob, idle, recoil) turns the optic camera too. Kept until the weapon or viewed player changes; until the first aim the
// camera looks along the eye. The picture itself sits on the lens mesh, so it moves with the gun exactly.
struct NeoOpticFollow
{
	bool calibrated = false;
	int player = 0;			// entity index of the viewed player (changes when spectating someone else)
	char weapon[MAX_WEAPON_STRING] = "";
	matrix3x4_t restGunInEye;	// muzzle attachment in eye space at calibration
};
static NeoOpticFollow s_follow;

static QAngle OpticCameraAngles(const CViewSetup &mainView, const CNEOWeaponInfo &data)
{
	C_NEO_Player *pPlayer = OpticViewPlayer();
	C_NEOPredictedViewModel *pViewModel = pPlayer ? pPlayer->GetNEOViewModel() : nullptr;
	Vector gunOrigin;
	QAngle gunAngles;
	if (!pViewModel || !pViewModel->GetAttachment(1, gunOrigin, gunAngles))
	{
		s_follow.calibrated = false;
		return mainView.angles;
	}
	if (s_follow.player != pPlayer->entindex() || V_strcmp(s_follow.weapon, data.szClassName) != 0)
	{
		s_follow.calibrated = false;
		s_follow.player = pPlayer->entindex();
		V_strncpy(s_follow.weapon, data.szClassName, sizeof(s_follow.weapon));
	}

	matrix3x4_t eyeToWorld, worldToEye, gunToWorld, gunInEye;
	AngleMatrix(mainView.angles, mainView.origin, eyeToWorld);
	MatrixInvert(eyeToWorld, worldToEye);
	AngleMatrix(gunAngles, gunOrigin, gunToWorld);
	ConcatTransforms(worldToEye, gunToWorld, gunInEye);

	if (!s_follow.calibrated)
	{
		const int activity = pViewModel->GetSequenceActivity(pViewModel->GetSequence());
		if (!pPlayer->IsInAim() || pViewModel->GetIronsightBlend() < 0.999f || (activity != ACT_VM_IDLE && activity != ACT_VM_IDLE_EMPTY))
		{
			return mainView.angles;
		}
		MatrixCopy(gunInEye, s_follow.restGunInEye);
		s_follow.calibrated = true;
	}

	// The gun's turn since calibration, applied to the eye.
	matrix3x4_t restInv, turn, cameraToWorld;
	MatrixInvert(s_follow.restGunInEye, restInv);
	ConcatTransforms(gunInEye, restInv, turn);
	MatrixSetColumn(vec3_origin, 3, turn);
	ConcatTransforms(eyeToWorld, turn, cameraToWorld);
	QAngle cameraAngles;
	MatrixAngles(cameraToWorld, cameraAngles);
	return cameraAngles;
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
	opticView.angles = OpticCameraAngles(mainView, *pData);
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
// The live view on the lens: for one viewmodel draw, the lens material's base texture becomes the optic
// render and its detail layer the weapon's reticle.
//-----------------------------------------------------------------------------
NeoIronsightOpticLens::NeoIronsightOpticLens(const CNEOWeaponInfo *pData)
{
	if (!pData || !pData->m_szIronOpticLens[0] || NeoGetIronsightOpticMode() != NEO_OPTIC_PIP || !s_opticSystem.m_texture.IsValid())
	{
		return;
	}
	IMaterial *pLens = materials->FindMaterial(pData->m_szIronOpticLens, TEXTURE_GROUP_MODEL, false);
	if (!pLens || pLens->IsErrorMaterial())
	{
		return;
	}
	bool bFound = false;
	m_pBase = pLens->FindVar("$basetexture", &bFound, false);
	if (!bFound || !m_pBase)
	{
		m_pBase = nullptr;
		return;
	}
	m_pOriginalBase = m_pBase->GetTextureValue();
	m_pBase->SetTextureValue(s_opticSystem.m_texture);

	// The reticle rides on the material's $detail layer (the lens VMT must declare one).
	m_pDetail = pLens->FindVar("$detail", &bFound, false);
	IMaterialVar *pBlend = pLens->FindVar("$detailblendfactor", &bFound, false);
	if (m_pDetail && bFound && pBlend && pData->m_szIronOpticReticle[0])
	{
		ITexture *pReticle = materials->FindTexture(pData->m_szIronOpticReticle, TEXTURE_GROUP_VGUI, false);
		if (pReticle && !pReticle->IsError())
		{
			m_pOriginalDetail = m_pDetail->GetTextureValue();
			m_pDetail->SetTextureValue(pReticle);
			m_pBlend = pBlend;
			m_flOriginalBlend = m_pBlend->GetFloatValue();
			m_pBlend->SetFloatValue(1.0f);
		}
	}
}

NeoIronsightOpticLens::~NeoIronsightOpticLens()
{
	if (m_pBlend)
	{
		m_pBlend->SetFloatValue(m_flOriginalBlend);
		m_pDetail->SetTextureValue(m_pOriginalDetail);
	}
	if (m_pBase)
	{
		m_pBase->SetTextureValue(m_pOriginalBase);
	}
}

//-----------------------------------------------------------------------------
// The full-screen scope overlay (cl_neo_ironsight_optic 0), with the gun hidden.
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
		return NeoGetIronsightOpticMode() == NEO_OPTIC_OVERLAY && CHudElement::ShouldDraw();
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

	// Sized exactly as the scoped rifles size their scope in hud_crosshair.cpp, so the lens stays
	// round: scope03's lens area is 960x720 texels, stretched back to a circle.
	void Paint() override
	{
		const CNEOWeaponInfo *pData = LocalOpticWeaponData();
		if (!pData)
		{
			return;
		}
		if (m_texture < 0)
		{
			m_texture = vgui::surface()->CreateNewTextureID();
		}
		if (V_strcmp(m_szTextureFile, pData->m_szIronOpticOverlay) != 0)
		{
			vgui::surface()->DrawSetTextureFile(m_texture, pData->m_szIronOpticOverlay, true, false);
			V_strncpy(m_szTextureFile, pData->m_szIronOpticOverlay, sizeof(m_szTextureFile));
		}
		int texWide = 0, texTall = 0;
		vgui::surface()->DrawGetTextureSize(m_texture, texWide, texTall);
		int wide, tall;
		GetSize(wide, tall);
		if (texWide <= 0 || texTall <= 0)
		{
			return;
		}

		float scaleX = static_cast<float>(wide) / texWide;
		float scaleY = static_cast<float>(tall) / texTall;
		static ConVarRef cl_neo_scope_restrict_to_rectangle("cl_neo_scope_restrict_to_rectangle");
		if (cl_neo_scope_restrict_to_rectangle.GetBool())
		{
			constexpr float VISIBLE_AREA_SCALING = 720.0f / 960.0f;
			scaleX = Min(scaleX, scaleY);
			scaleY = scaleX * VISIBLE_AREA_SCALING;
			scaleX *= 1.0f + (1.0f - VISIBLE_AREA_SCALING);
			scaleY *= 1.0f + (1.0f - VISIBLE_AREA_SCALING);
		}
		const int scopeWide = RoundFloatToInt(texWide * scaleX);
		const int scopeTall = RoundFloatToInt(texTall * scaleY);
		const int x0 = (wide - scopeWide) / 2;
		const int y0 = (tall - scopeTall) / 2;

		// The scoped rifles' fill colour around the scope, not pure black.
		vgui::surface()->DrawSetColor(16, 17, 16, 255);
		vgui::surface()->DrawFilledRect(0, 0, x0, tall);
		vgui::surface()->DrawFilledRect(x0 + scopeWide, 0, wide, tall);
		vgui::surface()->DrawFilledRect(x0, 0, x0 + scopeWide, y0);
		vgui::surface()->DrawFilledRect(x0, y0 + scopeTall, x0 + scopeWide, tall);

		vgui::surface()->DrawSetColor(255, 255, 255, 255);
		vgui::surface()->DrawSetTexture(m_texture);
		vgui::surface()->DrawTexturedRect(x0, y0, x0 + scopeWide, y0 + scopeTall);
	}

private:
	void FillScreen()
	{
		int wide, tall;
		vgui::surface()->GetScreenSize(wide, tall);
		SetBounds(0, 0, wide, tall);
	}

	int m_texture = -1;
	char m_szTextureFile[MAX_WEAPON_STRING] = "";
};

DECLARE_HUDELEMENT(CNeoHudIronsightOptic);
