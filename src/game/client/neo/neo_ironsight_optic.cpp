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

// The optic follows the gun: the first moment the gun is settled on the sights (idle, fully aimed), the
// muzzle attachment's pose relative to the eye is recorded, with the lens at screen centre. After that,
// each frame's gun movement (mouse sway, bob, idle, recoil) moves the lens picture on screen and turns
// the optic camera, so the reticle keeps marking where the gun points. Resets when leaving the sights.
struct NeoOpticFollow
{
	bool calibrated = false;
	char weapon[MAX_WEAPON_STRING] = "";
	matrix3x4_t restGunInEye;	// muzzle attachment in eye space (x forward, y left, z up) at calibration
	Vector lensInGun;			// lens centre in muzzle space
	// This frame's result, used by the render and the HUD.
	bool valid = false;
	Vector2D lensScreen;		// lens centre in screen pixels
	QAngle cameraAngles;		// optic camera direction
};
static NeoOpticFollow s_follow;

static void UpdateOpticFollow(const CViewSetup &mainView, const CNEOWeaponInfo &data)
{
	s_follow.valid = false;
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	C_NEOPredictedViewModel *pViewModel = pPlayer ? pPlayer->GetNEOViewModel() : nullptr;
	Vector gunOrigin;
	QAngle gunAngles;
	// Attachment positions come back converted from the viewmodel's FOV into the main view's.
	if (!pViewModel || !pViewModel->GetAttachment(1, gunOrigin, gunAngles))
	{
		s_follow.calibrated = false;
		return;
	}
	if (V_strcmp(s_follow.weapon, data.szClassName) != 0)
	{
		s_follow.calibrated = false;
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
		const bool bSettled = pViewModel->GetIronsightBlend() >= 0.999f && (activity == ACT_VM_IDLE || activity == ACT_VM_IDLE_EMPTY);
		if (!bSettled)
		{
			return;
		}
		// The lens sits on the view axis, part way between the eye and the muzzle.
		Vector gunInEyeOrigin;
		MatrixGetColumn(gunInEye, 3, gunInEyeOrigin);
		const Vector lensInEye(gunInEyeOrigin.x * data.m_flIronOpticLensDepth, 0.0f, 0.0f);
		matrix3x4_t eyeInGun;
		MatrixInvert(gunInEye, eyeInGun);
		VectorTransform(lensInEye, eyeInGun, s_follow.lensInGun);
		MatrixCopy(gunInEye, s_follow.restGunInEye);
		s_follow.calibrated = true;
	}

	// Where the lens is now, projected with the main view (eye space: x forward, y left, z up).
	Vector lensInEye;
	VectorTransform(s_follow.lensInGun, gunInEye, lensInEye);
	if (lensInEye.x <= 1.0f)
	{
		return;
	}
	const float focal = (mainView.width * 0.5f) / tanf(DEG2RAD(mainView.fov * 0.5f));
	s_follow.lensScreen.Init(mainView.x + mainView.width * 0.5f - focal * lensInEye.y / lensInEye.x,
		mainView.y + mainView.height * 0.5f - focal * lensInEye.z / lensInEye.x);

	// The gun's turn since calibration, applied to the eye, aims the optic camera.
	matrix3x4_t restInv, turn, cameraToWorld;
	MatrixInvert(s_follow.restGunInEye, restInv);
	ConcatTransforms(gunInEye, restInv, turn);
	MatrixSetColumn(vec3_origin, 3, turn);
	ConcatTransforms(eyeToWorld, turn, cameraToWorld);
	MatrixAngles(cameraToWorld, s_follow.cameraAngles);
	s_follow.valid = true;
}

// Renders the magnified view from the eye into the optic's render target, like a point_camera monitor.
void CViewRender::DrawNeoIronsightOptic(const CViewSetup &mainView)
{
	const CNEOWeaponInfo *pData = LocalOpticWeaponData();
	if (!pData || NeoGetIronsightOpticMode() != NEO_OPTIC_PIP || !s_opticSystem.m_texture.IsValid())
	{
		s_follow.calibrated = false;
		s_follow.valid = false;
		return;
	}
	UpdateOpticFollow(mainView, *pData);

	CViewSetup opticView = mainView;
	if (s_follow.valid)
	{
		opticView.angles = s_follow.cameraAngles;
	}
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

	// Full-screen scope, sized exactly as the scoped rifles size theirs in hud_crosshair.cpp, so the
	// lens stays round: scope03's lens area is 960x720 texels, stretched back to a circle.
	void PaintOverlay(const CNEOWeaponInfo &data)
	{
		const int texture = FileTexture(m_overlayTexture, m_szOverlayFile, data.m_szIronOpticOverlay);
		int texWide = 0, texTall = 0;
		vgui::surface()->DrawGetTextureSize(texture, texWide, texTall);
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
		vgui::surface()->DrawSetTexture(texture);
		vgui::surface()->DrawTexturedRect(x0, y0, x0 + scopeWide, y0 + scopeTall);
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
		// Centred on the lens as it moves with the gun, or the screen centre until the optic has calibrated.
		const float centreX = s_follow.valid ? s_follow.lensScreen.x : wide * 0.5f;
		const float centreY = s_follow.valid ? s_follow.lensScreen.y : tall * 0.5f;
		const float radius = data.m_flIronOpticRadius * tall;
		// The live picture stops short of the lens edge, inside the reticle's opaque rim, so no view
		// peeks past the rim's soft (filtered, cut-out) outermost pixels as the lens moves.
		const float liveRadius = radius * 0.96f;

		constexpr int SEGMENTS = 64;
		vgui::Vertex_t circle[SEGMENTS];
		for (int i = 0; i < SEGMENTS; ++i)
		{
			const float angle = 2.0f * M_PI_F * i / SEGMENTS;
			const float c = cosf(angle), s = sinf(angle);
			circle[i].Init(Vector2D(centreX + liveRadius * c, centreY + liveRadius * s),
				Vector2D(0.5f + 0.48f * c, 0.5f + 0.48f * s));
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
