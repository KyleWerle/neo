#include "cbase.h"
#include "neo_ironsight_optic.h"
#include "neo_ironsight_optic_disc.h"
#include "neo_ironsight_augment.h"
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
#include "materialsystem/itexture.h"
#include "view.h"
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
			// No alpha channel, so the view always samples as opaque: its alpha is never meaningful, and the
			// lens drawing (neo_ironsight_optic_disc.cpp) supplies its own through vertex alpha.
			RT_SIZE_NO_CHANGE, IMAGE_FORMAT_BGRX8888, MATERIAL_RT_DEPTH_SEPARATE,
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
C_NEO_Player *NeoIronsightOpticViewPlayer()
{
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	if (pLocal && pLocal->IsObserver() && pLocal->GetObserverMode() == OBS_MODE_IN_EYE)
	{
		return dynamic_cast<C_NEO_Player *>(pLocal->GetObserverTarget());
	}
	return pLocal;
}

bool NeoIronsightInThermals(const C_NEO_Player *pPlayer)
{
	return pPlayer && pPlayer->GetClass() == NEO_CLASS_SUPPORT && pPlayer->IsInVision();
}

ITexture *NeoIronsightOpticTexture()
{
	return s_opticSystem.m_texture.IsValid() ? static_cast<ITexture *>(s_opticSystem.m_texture) : nullptr;
}

// The viewed player's active weapon data, if it has an optic and ironsights apply to it.
static const CNEOWeaponInfo *LocalOpticWeaponData()
{
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
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
	if (pData->m_bIronOpticWindow)
	{
		// Sight glass needs the view only while the gun is drawn over (cloak or thermals); it never becomes
		// a full-screen scope.
		C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
		return (pPlayer && (pPlayer->IsCloaked() || NeoIronsightInThermals(pPlayer))) ? NEO_OPTIC_PIP : NEO_OPTIC_NONE;
	}
	if (!cl_neo_ironsight_optic.GetBool() || !pData->m_szIronOpticLens[0])
	{
		// The full-screen scope only while aiming.
		C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
		return (pPlayer && pPlayer->IsInAim()) ? NEO_OPTIC_OVERLAY : NEO_OPTIC_NONE;
	}
	// The live view on the lens runs whenever the weapon is out, hip or sights, like a real optic.
	return NEO_OPTIC_PIP;
}

// The optic camera points where the gun points: the first time the gun settles on the sights (idle,
// fully aimed), the muzzle attachment's pose relative to the eye is recorded as "looking straight
// ahead". After that, at the hip or on the sights, the gun's turn from that pose (hip angle, mouse sway,
// bob, idle, recoil) turns the optic camera too. Kept until the weapon or viewed player changes; until the first aim the
// camera looks along the eye. It stays level with the eye; the picture is laid out on the lens as seen on
// screen, so it moves with the gun but never rolls with it.
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
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
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
	// Level with the eye: the gun's roll (a cant, or a viewmodel-only lean) turns the lens and its reticle,
	// not the world seen through it, which the lens drawing lays out level on screen.
	Vector forward, up;
	MatrixGetColumn(cameraToWorld, 0, forward);
	MatrixGetColumn(eyeToWorld, 2, up);
	QAngle cameraAngles;
	VectorAngles(forward, up, cameraAngles);
	return cameraAngles;
}

// Where the lens appears from the eye: the direction to its centre in eye space and the tangent of its
// half-angle, both as seen on screen in the main view's terms (the viewmodel is drawn with its own FOV).
static bool LensOnScreen(const CViewSetup &mainView, const CNEOWeaponInfo &data, Vector &dirInEye, float &tanHalf)
{
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	C_BaseAnimating *pViewModel = pPlayer ? pPlayer->GetNEOViewModel() : nullptr;
	const int bone = (pViewModel && data.m_bHasIronOpticLensMap) ? pViewModel->LookupBone(data.m_szIronOpticLensBone) : -1;
	if (bone < 0)
	{
		return false;
	}
	matrix3x4_t lensToWorld;
	// From the drawn pose, not GetBoneTransform: its cache holds only hitbox bones, and lens bones that are not
	// (the MX-S's sight_glass) would come back as the viewmodel's origin.
	MatrixCopy(pViewModel->GetBone(bone), lensToWorld);
	const Vector &circle = data.m_vecIronOpticLensCircle;
	Vector centre;
	VectorTransform(data.m_vecIronOpticLensOrigin + data.m_vecIronOpticLensU * circle.x + data.m_vecIronOpticLensV * circle.y, lensToWorld, centre);
	const float radius = data.m_vecIronOpticLensU.Length() * circle.z;
	const float distance = (centre - mainView.origin).Length();
	if (distance <= radius)
	{
		return false;
	}
	const float fovScale = tanf(DEG2RAD(mainView.fov * 0.5f)) / tanf(DEG2RAD(mainView.fovViewmodel * 0.5f));
	matrix3x4_t eyeToWorld, worldToEye;
	AngleMatrix(mainView.angles, mainView.origin, eyeToWorld);
	MatrixInvert(eyeToWorld, worldToEye);
	Vector eye;
	VectorTransform(centre, worldToEye, eye);
	dirInEye.Init(eye.x, eye.y * fovScale, eye.z * fovScale);
	tanHalf = fovScale * radius / sqrtf(distance * distance - radius * radius);
	return true;
}

// The optic's view. With a "magnification" and the lens surface known, its field of view comes from how
// big the lens looks on screen right now: at 1x the picture matches the world around the lens exactly
// (like empty glass), at 3x everything in it is three times larger, whatever the lens size, sight tuning
// or viewmodel FOV. Otherwise the script's fixed "fov". Disc lenses (the Jitte's) are plain glass at the
// hip: 1x, looking from the eye through the lens, so cloaking there doesn't change what it shows; coming
// onto the sights they turn into the gun-following magnified view.
static void OpticView(const CViewSetup &mainView, const CNEOWeaponInfo &data, QAngle &angles, float &fov)
{
	angles = OpticCameraAngles(mainView, data);
	fov = data.m_flIronOpticFov;
	Vector dirInEye;
	float tanHalf;
	if (data.m_flIronOpticMagnification <= 0.0f || !LensOnScreen(mainView, data, dirInEye, tanHalf))
	{
		return;
	}
	float aim = 1.0f;
	if (data.m_bIronOpticLensDisc)
	{
		C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
		C_NEOPredictedViewModel *pViewModel = pPlayer ? pPlayer->GetNEOViewModel() : nullptr;
		aim = pViewModel ? pViewModel->GetIronsightBlend() : 1.0f;
	}
	fov = RAD2DEG(2.0f * atanf(tanHalf / Lerp(aim, 1.0f, data.m_flIronOpticMagnification)));
	if (aim >= 1.0f)
	{
		return;
	}
	// At the hip, along the eye's line through the lens, upright like the eye.
	QAngle throughInEye, through;
	VectorAngles(dirInEye, Vector(0.0f, 0.0f, 1.0f), throughInEye);
	matrix3x4_t eyeToWorld, throughToEye, throughToWorld;
	AngleMatrix(mainView.angles, eyeToWorld);
	AngleMatrix(throughInEye, throughToEye);
	ConcatTransforms(eyeToWorld, throughToEye, throughToWorld);
	MatrixAngles(throughToWorld, through);
	Quaternion hip, sights, blended;
	AngleQuaternion(through, hip);
	AngleQuaternion(angles, sights);
	QuaternionSlerp(hip, sights, aim, blended);
	QuaternionAngles(blended, angles);
}

// Whether anything shows the optic's view this frame, so the scene render can be skipped when not. Disc
// lenses at the hip show only their own lens unless it is drawn over (cloak, thermals); the live view
// fades in over the second half of aiming (as NeoIronsightDrawOpticDisc draws it). Sight glass is
// already gated by the optic mode.
static bool OpticViewShown(const CNEOWeaponInfo &data)
{
	if (!data.m_bIronOpticLensDisc || data.m_bIronOpticWindow)
	{
		return true;
	}
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	if (!pPlayer)
	{
		return false;
	}
	if (pPlayer->IsCloaked() || NeoIronsightInThermals(pPlayer))
	{
		return true;
	}
	const C_NEOPredictedViewModel *pViewModel = pPlayer->GetNEOViewModel();
	return pViewModel && pViewModel->GetIronsightBlend() > 0.5f;
}

// Renders the magnified view from the eye into the optic's render target, like a point_camera monitor.
void CViewRender::DrawNeoIronsightOptic(const CViewSetup &mainView)
{
	// The augmented aim (e.g. the MPN45's) shares the render target; its weapons have no optic.
	CViewSetup augmentView;
	if (NeoIronsightAugmentView(mainView, augmentView))
	{
		{
			CMatRenderContextPtr pRenderContext(materials);
			pRenderContext->TurnOnToneMapping();
		}
		Frustum frustum;
		render->Push3DView(augmentView, VIEW_CLEAR_DEPTH | VIEW_CLEAR_COLOR, s_opticSystem.m_texture, (VPlane *)frustum);
		ViewDrawScene(false, SKYBOX_2DSKYBOX_VISIBLE, augmentView, 0, VIEW_MONITOR);
		render->PopView(frustum);
		return;
	}

	const CNEOWeaponInfo *pData = LocalOpticWeaponData();
	if (!pData || NeoGetIronsightOpticMode() != NEO_OPTIC_PIP || !s_opticSystem.m_texture.IsValid() || !OpticViewShown(*pData))
	{
		return;
	}

	CViewSetup opticView = mainView;
	if (pData->m_bIronOpticWindow)
	{
		if (!NeoIronsightWindowCamera(mainView, *pData, opticView.angles, opticView.fov))
		{
			return;
		}
	}
	else
	{
		OpticView(mainView, *pData, opticView.angles, opticView.fov);
	}
	opticView.x = 0;
	opticView.y = 0;
	opticView.width = OPTIC_RT_SIZE;
	opticView.height = OPTIC_RT_SIZE;
	opticView.m_flAspectRatio = 1.0f;
	opticView.m_bOrtho = false;
	opticView.m_bViewToProjectionOverride = false;

	// The main view's exposure: the end of the last frame reset the HDR tone mapping scale to 1, and without
	// it the optic's picture is brighter or darker than the world around the glass (in thermals, where
	// brightness becomes colour, a solid patch).
	{
		CMatRenderContextPtr pRenderContext(materials);
		pRenderContext->TurnOnToneMapping();
	}
	Frustum frustum;
	render->Push3DView(opticView, VIEW_CLEAR_DEPTH | VIEW_CLEAR_COLOR, s_opticSystem.m_texture, (VPlane *)frustum);
	ViewDrawScene(false, SKYBOX_2DSKYBOX_VISIBLE, opticView, 0, VIEW_MONITOR);
	render->PopView(frustum);
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
