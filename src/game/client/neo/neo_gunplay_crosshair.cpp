#include "cbase.h"
#include "neo_gunplay_crosshair.h"
#include "neo_crosshair_family.h"
#include "neo_gunplay_tunnel.h"
#include "neo_ironsights.h"
#include "neo_crosshair.h"
#include "neo_predicted_viewmodel.h"
#include "c_neo_player.h"
#include "view.h"
#include "weapon_neobasecombatweapon.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_crosshair("cl_neo_gunplay_crosshair", "1", FCVAR_ARCHIVE,
	"With Enable Gunplay: ghost linework around your crosshair, riding the spread (0 = your crosshair alone).",
	true, 0, true, 1);
ConVar cl_neo_gunplay_crosshair_alpha("cl_neo_gunplay_crosshair_alpha", "1", FCVAR_ARCHIVE,
	"Opacity of the crosshair layer.", true, 0, true, 1);
ConVar cl_neo_gunplay_crosshair_parallax("cl_neo_gunplay_crosshair_parallax", "1", FCVAR_ARCHIVE,
	"How much the layer's parts near the gun move with its knock and pivot against your crosshair.",
	true, 0, true, 3);
ConVar cl_neo_gunplay_crosshair_centre("cl_neo_gunplay_crosshair_centre", "0", FCVAR_ARCHIVE,
	"What marks the centre in place of the Default or Alt crosshair: 0 = nothing (the layer's moving parts show the"
	" aim), 1 = a tiny square, 2 = a small cross. A Custom crosshair is always drawn as it is.", true, 0, true, 2);
ConVar cl_neo_gunplay_crosshair_family("cl_neo_gunplay_crosshair_family", "-1", FCVAR_NONE,
	"Debug: draw every gun's crosshair as this family (0 rifle, 1 SMG, 2 MG, 3 shotgun, 4 pistol, 5 scoped;"
	" -1 = each gun its own).", true, -1, true, NEO_CROSSHAIR_FAMILY__TOTAL - 1);

static constexpr float CROSS_ARM = 4.0f;			// the plain centre cross, each arm, at 1080p
static constexpr float SQUARE_HALF = 1.5f;			// the tiny centre square
static constexpr float PRECISION_SIZE = 2.0f;		// the precision dot, its side
static constexpr float PRECISION_TIME = 0.05f;		// it comes and goes this quickly
static constexpr float PRECISE_SPREAD = 1e-5f;		// a cone this narrow (a tangent) is no spread at all
static constexpr float SPREAD_TIME = 0.04f;		// the spread's ease (a critically damped spring's time constant)
static constexpr float SHOT_POP = 900.0f;			// each shot kicks the spread's spring outward, pixels/s at 1080p
static constexpr float AIM_TIME = 0.15f;			// hip to aimed look
static constexpr float TRACE_TIME = 0.11f;			// coming online, as the sight ghost does
static constexpr float TUNNEL_OPACITY = 0.6f;		// of the layer's
static constexpr float SHOT_TIME = 0.08f;			// a shot scrambles the layer this long
static constexpr float SCRAMBLE_CYCLE = 0.3f;		// fully on guns this slow; less on faster ones (no constant flicker)

static struct
{
	float spread = 0.0f;	// eased, in pixels
	float spreadVelocity = 0.0f;
	float aim = 0.0f;		// 0 hip to 1 aimed, linear (eased when drawn)
	float bootStart = 0.0f;
	float lastTime = 0.0f;
	int lastFrame = -1;
	const C_NEOBaseCombatWeapon *pWeapon = nullptr;
	int lastClip = -1;
	float shotTime = -100.0f;
	float precise = 0.0f;	// 0 to 1: the precision dot showing
} s_layer;

NeoCrosshairFamily NeoCrosshairFamilyOf(const C_NEOBaseCombatWeapon *pWeapon)
{
	const int forced = cl_neo_gunplay_crosshair_family.GetInt();
	if (forced >= 0)
	{
		return static_cast<NeoCrosshairFamily>(forced);
	}
	const auto bits = pWeapon->GetNeoWepBits();
	if (bits & (NEO_WEP_ZR68_L | NEO_WEP_M41_L | NEO_WEP_SRS))
	{
		return NEO_CROSSHAIR_SCOPED;
	}
	if (bits & (NEO_WEP_SUPA7 | NEO_WEP_AA13))
	{
		return NEO_CROSSHAIR_SHOTGUN;
	}
	if (bits & (NEO_WEP_TACHI | NEO_WEP_MILSO | NEO_WEP_KYLA))
	{
		return NEO_CROSSHAIR_PISTOL;
	}
	if (bits & (NEO_WEP_PZ | NEO_WEP_BALC))
	{
		return NEO_CROSSHAIR_MG;
	}
	if (bits & (NEO_WEP_MPN | NEO_WEP_MPN_S | NEO_WEP_SRM | NEO_WEP_SRM_S | NEO_WEP_JITTE | NEO_WEP_SMAC))
	{
		return NEO_CROSSHAIR_SMG;
	}
	return NEO_CROSSHAIR_RIFLE;
}

int NeoCrosshairFrame::Alpha(float opacity) const
{
	return clamp(RoundFloatToInt(255.0f * alpha * opacity), 0, 255);
}

static vgui::HFont ReadoutFont()
{
	static vgui::HFont s_font = vgui::INVALID_FONT;
	if (s_font == vgui::INVALID_FONT)
	{
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
		s_font = pScheme ? pScheme->GetFont("NHudOCRSmallerNoAdditive", true) : vgui::INVALID_FONT;
	}
	return s_font;
}

int NeoCrosshairReadoutTall()
{
	const vgui::HFont font = ReadoutFont();
	return (font != vgui::INVALID_FONT) ? vgui::surface()->GetFontTall(font) : 0;
}

void NeoCrosshairReadout(const NeoCrosshairFrame &frame, const Vector2D &at, const wchar_t *pText, float typed,
	float opacity)
{
	const vgui::HFont font = ReadoutFont();
	if (font == vgui::INVALID_FONT || typed <= 0.0f)
	{
		return;
	}
	NeoGhostFlush();	// the strokes so far under the text
	wchar_t text[64];
	V_wcsncpy(text, pText, sizeof(text) - sizeof(wchar_t));
	const int length = V_wcslen(text);
	int count = Min(length, static_cast<int>(ceilf(length * typed)));
	if (typed < 1.0f && count < ARRAYSIZE(text) - 1)
	{
		text[count++] = L'_';
	}
	vgui::surface()->DrawSetTextFont(font);
	vgui::surface()->DrawSetTextColor(frame.color.r(), frame.color.g(), frame.color.b(), frame.Alpha(opacity));
	vgui::surface()->DrawSetTextPos(RoundFloatToInt(at.x), RoundFloatToInt(at.y) - vgui::surface()->GetFontTall(font) / 2);
	vgui::surface()->DrawPrintText(text, count);
}

// Where the gun's knock and pivot have turned it from the aim, in screen pixels (right, down).
static Vector2D GunDeviation(C_NEO_Player *pPlayer, float pixelsPerTangent)
{
	auto *pViewModel = dynamic_cast<C_NEOPredictedViewModel *>(pPlayer->GetViewModel());
	if (!pViewModel)
	{
		return Vector2D(0.0f, 0.0f);
	}
	QAngle turn = pViewModel->GetUnswayedAngles() - pViewModel->GetUnkickedAngles();
	turn.x = AngleNormalize(turn.x);
	turn.y = AngleNormalize(turn.y);
	// Yaw is positive to the left, pitch positive down.
	return Vector2D(-tanf(DEG2RAD(turn.y)), tanf(DEG2RAD(turn.x))) * pixelsPerTangent;
}

static bool LayerShown(C_NEOBaseCombatWeapon *pWeapon)
{
	return NeoGunplayEnabled() && cl_neo_gunplay_crosshair.GetBool() && pWeapon && (pWeapon->GetNeoWepBits() & NEO_WEP_FIREARM);
}

bool NeoGunplayCrosshairLayerOn(C_NEOBaseCombatWeapon *pWeapon)
{
	return LayerShown(pWeapon);
}

bool NeoGunplayReplacesCrosshair(C_NEOBaseCombatWeapon *pWeapon, int crosshairStyle)
{
	return LayerShown(pWeapon) && (crosshairStyle == CROSSHAIR_STYLE_DEFAULT || crosshairStyle == CROSSHAIR_STYLE_ALT_B);
}

void NeoGunplayPaintCrosshairLayer(C_NEOBaseCombatWeapon *pWeapon, const Color &color, int x, int y, bool bCentre,
	float spreadScale)
{
	auto *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (!LayerShown(pWeapon) || !pPlayer)
	{
		return;
	}
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);

	// Coming online: whenever the layer wasn't drawn the frame before, or the gun changed, it traces in again.
	const float now = gpGlobals->realtime;
	float dt = clamp(now - s_layer.lastTime, 0.0f, 0.1f);
	s_layer.lastTime = now;
	const bool bBoot = s_layer.lastFrame != gpGlobals->framecount - 1 || s_layer.pWeapon != pWeapon;
	if (bBoot)
	{
		dt = 0.0f;
		s_layer.bootStart = now;
		s_layer.spread = HalfInaccuracyConeInScreenPixels(pWeapon, wide / 2) * spreadScale;
		s_layer.spreadVelocity = 0.0f;
		s_layer.aim = pPlayer->IsInAim() ? 1.0f : 0.0f;
		s_layer.pWeapon = pWeapon;
		s_layer.lastClip = pWeapon->Clip1();
	}
	// A shot: the clip drops.
	const bool bShot = pWeapon->Clip1() < s_layer.lastClip;
	if (bShot)
	{
		s_layer.shotTime = now;
	}
	s_layer.lastClip = pWeapon->Clip1();
	s_layer.lastFrame = gpGlobals->framecount;

	// The spread's edge, eased (it steps shot to shot), and the hip-to-aimed look.
	const float target = HalfInaccuracyConeInScreenPixels(pWeapon, wide / 2) * spreadScale;
	// Each shot pops it out past the spread a moment, springing back: the layer shifts with every shot.
	if (bShot)
	{
		s_layer.spreadVelocity += SHOT_POP * (tall / 1080.0f);
	}
	const float omega = 1.0f / SPREAD_TIME;
	s_layer.spreadVelocity += ((target - s_layer.spread) * omega * omega - 2.0f * omega * s_layer.spreadVelocity) * dt;
	s_layer.spread = Max(0.0f, s_layer.spread + s_layer.spreadVelocity * dt);
	s_layer.aim = Approach(pPlayer->IsInAim() ? 1.0f : 0.0f, s_layer.aim, dt / AIM_TIME);

	NeoCrosshairFrame frame;
	frame.pPlayer = pPlayer;
	frame.pWeapon = pWeapon;
	frame.color = color;
	frame.s = tall / 1080.0f;
	frame.pen.scale = frame.s;
	frame.pen.trace = NeoSmoothStep((now - s_layer.bootStart) / TRACE_TIME);
	frame.centre.Init(static_cast<float>(x), static_cast<float>(y));
	const float scaledFov = DEG2RAD(ScaleFOVByWidthRatio(pPlayer->GetFOV(), engine->GetScreenAspectRatio() * 0.75f)) * 0.5f;
	frame.deviation = GunDeviation(pPlayer, (wide * 0.5f) / tanf(scaledFov)) * cl_neo_gunplay_crosshair_parallax.GetFloat();
	frame.spread = s_layer.spread;
	frame.aim = NeoSmoothStep(s_layer.aim);
	frame.alpha = cl_neo_gunplay_crosshair_alpha.GetFloat();
	frame.sinceBoot = now - s_layer.bootStart;
	frame.sinceShot = now - s_layer.shotTime;
	frame.clip = pWeapon->Clip1();
	frame.maxClip = pWeapon->GetMaxClip1();
	frame.cycle = pWeapon->GetFireRate();
	frame.ready = (frame.cycle > 0.0f)
		? clamp(1.0f - (pWeapon->m_flNextPrimaryAttack - gpGlobals->curtime) / frame.cycle, 0.0f, 1.0f) : 1.0f;
	frame.dt = dt;
	frame.bBoot = bBoot;
	frame.bShot = bShot;

	// The precision dot: the next shot is sure to go exactly where aimed (no spread at all: the precise guns, aimed
	// and settled). Very small, at the aim point, whatever the centre mark.
	const bool bPrecise = pWeapon->GetBulletSpread().x < PRECISE_SPREAD;
	s_layer.precise = bBoot ? (bPrecise ? 1.0f : 0.0f) : Approach(bPrecise ? 1.0f : 0.0f, s_layer.precise, dt / PRECISION_TIME);
	if (s_layer.precise > 0.0f)
	{
		const int size = Max(2, RoundFloatToInt(PRECISION_SIZE * frame.s));
		const int x0 = x - size / 2, y0 = y - size / 2;
		vgui::surface()->DrawSetColor(color.r(), color.g(), color.b(), RoundFloatToInt(color.a() * s_layer.precise));
		vgui::surface()->DrawFilledRect(x0, y0, x0 + size, y0 + size);
	}

	// The centre mark in place of the Default or Alt crosshair (cl_neo_gunplay_crosshair_centre): steady, full strength, traced in with the rest.
	const int centreStyle = cl_neo_gunplay_crosshair_centre.GetInt();
	if (bCentre && centreStyle == 2)
	{
		NeoGhostBegin(color, color.a());
		const float arm = CROSS_ARM * frame.s;
		NeoGhostStroke(frame.pen, frame.centre - Vector2D(arm, 0.0f), frame.centre + Vector2D(arm, 0.0f), NEO_GHOST_MEDIUM);
		NeoGhostStroke(frame.pen, frame.centre - Vector2D(0.0f, arm), frame.centre + Vector2D(0.0f, arm), NEO_GHOST_MEDIUM);
	}
	else if (bCentre && centreStyle == 1)
	{
		NeoGhostBegin(color, color.a());
		const float half = SQUARE_HALF * frame.s;
		const Vector2D corners[4] = { frame.centre + Vector2D(-half, -half), frame.centre + Vector2D(half, -half),
			frame.centre + Vector2D(half, half), frame.centre + Vector2D(-half, half) };
		for (int c = 0; c < 4; ++c)
		{
			NeoGhostStroke(frame.pen, corners[c], corners[(c + 1) % 4], NEO_GHOST_LIGHT);
		}
	}

	// Aimed: the projected shooting space, behind the rest (stubbed for now, see neo_gunplay_tunnel.h).
	if (frame.aim > 0.0f)
	{
		NeoGunplayPaintTunnel(pPlayer, pWeapon->GetBulletSpread().x, color, frame.alpha * frame.aim * TUNNEL_OPACITY);
	}

	// A shot scrambles it: a jitter and a flicker, as the sight ghost's; less on fast guns, whose shots come too
	// often for it to be anything but noise.
	frame.scramble = Max(0.0f, 1.0f - frame.sinceShot / SHOT_TIME) * Min(1.0f, frame.cycle / SCRAMBLE_CYCLE);
	frame.jitter.Init(0.0f, 0.0f);
	if (frame.scramble > 0.0f)
	{
		frame.jitter.Init(random->RandomFloat(-2.0f, 2.0f) * frame.scramble * frame.s,
			random->RandomFloat(-2.0f, 2.0f) * frame.scramble * frame.s);
		frame.alpha *= (gpGlobals->framecount & 1) ? 1.0f - 0.35f * frame.scramble : 1.0f;
	}

	switch (NeoCrosshairFamilyOf(pWeapon))
	{
	case NEO_CROSSHAIR_SMG:		NeoCrosshairPaintSmg(frame); break;
	case NEO_CROSSHAIR_MG:		NeoCrosshairPaintMg(frame); break;
	case NEO_CROSSHAIR_SHOTGUN:	NeoCrosshairPaintShotgun(frame); break;
	case NEO_CROSSHAIR_PISTOL:	NeoCrosshairPaintPistol(frame); break;
	case NEO_CROSSHAIR_SCOPED:	NeoCrosshairPaintScoped(frame); break;
	case NEO_CROSSHAIR_RIFLE:
	default:					NeoCrosshairPaintRifle(frame); break;
	}
	NeoGhostFlush();
}
