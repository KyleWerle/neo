#include "cbase.h"
#include "neo_gunplay_crosshair.h"
#include "neo_crosshair_family.h"
#include "neo_gunplay_aim.h"
#include "neo_gunplay_marks.h"
#include "neo_gunplay_spread_ghost.h"
#include "neo_spread_pivot.h"
#include "neo_gunplay_tunnel.h"
#include "neo_gunplay_shots.h"
#include "neo_ironsight_optic.h"
#include "neo_ironsights.h"
#include "neo_ironsight_profile.h"
#include "neo_hud_profile.h"
#include "neo_crosshair.h"
#include "neo_predicted_viewmodel.h"
#include "c_neo_player.h"
#include "view.h"
#include "ivieweffects.h"
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
ConVar cl_neo_gunplay_crosshair_scramble("cl_neo_gunplay_crosshair_scramble", "1", FCVAR_ARCHIVE,
	"How much each shot scrambles the layer (a jitter and a flicker); 0 = none (the Calm preset).", true, 0, true, 1);
ConVar cl_neo_gunplay_crosshair_debug("cl_neo_gunplay_crosshair_debug", "0", FCVAR_NONE,
	"Debug: on each shot, print the time since the last one and how ready the layer showed the gun just before it"
	" (1.00 when the layer's readiness matches the gun).");
ConVar cl_neo_gunplay_crosshair_family("cl_neo_gunplay_crosshair_family", "-1", FCVAR_NONE,
	"Debug: draw every gun's crosshair as this family (0 rifle, 1 SMG, 2 MG, 3 shotgun, 4 pistol, 5 scoped;"
	" -1 = each gun its own).", true, -1, true, NEO_CROSSHAIR_FAMILY__TOTAL - 1);

static constexpr float PRECISION_SIZE = 2.0f;		// the precision dot, its side
static constexpr float PRECISION_TIME = 0.05f;		// it comes and goes this quickly
static constexpr float PRECISE_SPREAD = 1e-5f;		// a cone this narrow (a tangent) is no spread at all
static constexpr float SPREAD_TIME = 0.04f;		// the spread's ease (a critically damped spring's time constant)
static constexpr float SHOT_POP = 900.0f;			// each shot kicks the spread's spring outward, pixels/s at 1080p
static constexpr float AIM_TIME = 0.15f;			// hip to aimed look
static constexpr float TRACE_TIME = 0.11f;			// coming online, as the sight ghost does
static constexpr float TUNNEL_OPACITY = 0.6f;		// of the layer's
static constexpr float SHOT_TIME = 0.08f;			// a shot scrambles the layer this long
static constexpr float FADE_HIDDEN = 0.05f;		// a screen fade darker than this hides the layer (it boots after)
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
	int shotCount = 0;
	float shotTime = -100.0f;
	float precise = 0.0f;	// 0 to 1: the precision dot showing
	float lastReady = 1.0f;	// the frame before's readiness (the debug print)
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
	NeoHudCount(NEO_HUD_COUNT_TEXT);
}

// Where the gun's knock and pivot have turned it from the aim, in screen pixels (right, down). The local player's
// viewmodel is the watched player's while spectating in first person.
static Vector2D GunDeviation(float pixelsPerTangent)
{
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	auto *pViewModel = pLocal ? dynamic_cast<C_NEOPredictedViewModel *>(pLocal->GetViewModel()) : nullptr;
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

// How much of the view shows through a screen fade (the spawn's fade in from black): 1 none, 0 fully faded.
float NeoHudFadeVisible()
{
	// A map's black screen overlay counts as fully faded (the tutorials hold env_screenoverlay's tools/toolsblack over
	// the view until you walk in: not a view fade, so the fade params don't see it).
	IMaterial *pOverlay = view ? view->GetScreenOverlayMaterial() : nullptr;
	if (pOverlay && !pOverlay->IsErrorMaterial() && V_stristr(pOverlay->GetName(), "toolsblack"))
	{
		return 0.0f;
	}
	byte r, g, b, a;
	bool bBlend;
	vieweffects->GetFadeParams(&r, &g, &b, &a, &bBlend);
	return 1.0f - a / 255.0f;
}

bool NeoHudFadedOut()
{
	return NeoHudFadeVisible() < FADE_HIDDEN;
}

void NeoGunplayPaintCrosshairLayer(C_NEOBaseCombatWeapon *pWeapon, const Color &colorIn, int x, int y, bool bCentre)
{
	NEO_HUD_PROFILE(NEO_HUD_PROFILE_CROSSHAIR, "NeoGunplayPaintCrosshairLayer");
	CNeoIronsightProfileScope ironsightHud(NEO_PROFILE_HUD);	// the ironsight bench's hud column
	// Under a screen fade (the spawn's fade in from black): the HUD paints over the view's fade, so the layer fades
	// with it, and isn't drawn at all while nearly black; drawn again, it boots and traces in as the view comes up.
	const float visible = NeoHudFadeVisible();
	if (visible < FADE_HIDDEN)
	{
		return;
	}
	Color color = colorIn;
	color[3] = static_cast<unsigned char>(RoundFloatToInt(colorIn.a() * visible));
	// Whoever's eyes the view is through: the local player, or one watched in first person.
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	if (!LayerShown(pWeapon) || !pPlayer)
	{
		return;
	}
	const NeoGunplayShots &shots = NeoGunplayWatchShots();
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);

	// Coming online: whenever the layer wasn't drawn the frame before, or the gun changed, it traces in again.
	const float now = gpGlobals->realtime;
	float dt = clamp(now - s_layer.lastTime, 0.0f, 0.1f);
	s_layer.lastTime = now;
	const bool bBoot = s_layer.lastFrame != gpGlobals->framecount - 1 || s_layer.pWeapon != pWeapon
		|| now - shots.viewChanged < 0.001f;
	if (bBoot)
	{
		dt = 0.0f;
		s_layer.bootStart = now;
		s_layer.spread = HalfInaccuracyConeInScreenPixels(pWeapon, wide / 2);
		s_layer.spreadVelocity = 0.0f;
		s_layer.aim = pPlayer->IsInAim() ? 1.0f : 0.0f;
		s_layer.pWeapon = pWeapon;
		s_layer.shotCount = shots.count;
	}
	const bool bShot = shots.count != s_layer.shotCount;
	if (bShot)
	{
		if (cl_neo_gunplay_crosshair_debug.GetBool())
		{
			Msg("[xhair] %s shot: %.3f s since the last (cycle %.3f), shown ready %.2f just before\n",
				pWeapon->GetClassname(), now - s_layer.shotTime, pWeapon->GetFireRate(), s_layer.lastReady);
		}
		s_layer.shotTime = now;
	}
	s_layer.shotCount = shots.count;
	s_layer.lastFrame = gpGlobals->framecount;

	// The spread's edge, eased (it steps shot to shot), and the hip-to-aimed look.
	const float target = HalfInaccuracyConeInScreenPixels(pWeapon, wide / 2);
	// Each shot pops it out past the spread a moment, springing back: the layer shifts with every shot.
	if (bShot)
	{
		s_layer.spreadVelocity += SHOT_POP * (tall / 1080.0f);
	}
	const float omega = 1.0f / SPREAD_TIME;
	NeoCrosshairSpring(s_layer.spread, s_layer.spreadVelocity, target, omega, dt);
	if (!IsFinite(s_layer.spread) || !IsFinite(s_layer.spreadVelocity))
	{
		s_layer.spread = target;
		s_layer.spreadVelocity = 0.0f;
	}
	s_layer.spread = Max(0.0f, s_layer.spread);
	s_layer.aim = Approach(pPlayer->IsInAim() ? 1.0f : 0.0f, s_layer.aim, dt / AIM_TIME);

	NeoCrosshairFrame frame;
	frame.pPlayer = pPlayer;
	frame.pWeapon = pWeapon;
	frame.color = color;
	frame.s = tall / 1080.0f;
	frame.pen.scale = frame.s;
	frame.pen.trace = NeoSmoothStep((now - s_layer.bootStart) / TRACE_TIME);
	frame.centre.Init(static_cast<float>(x), static_cast<float>(y));
	// The view's field of view is the local player's (spectating, the watched player's shows through it).
	const float scaledFov = DEG2RAD(ScaleFOVByWidthRatio(C_BasePlayer::GetLocalPlayer()->GetFOV(), engine->GetScreenAspectRatio() * 0.75f)) * 0.5f;
	const float pixelsPerTangent = (wide * 0.5f) / tanf(scaledFov);
	frame.deviation = GunDeviation(pixelsPerTangent) * cl_neo_gunplay_crosshair_parallax.GetFloat();
	frame.pixelsPerTangent = pixelsPerTangent;
	frame.spread = s_layer.spread;
	frame.spreadExact = target;
	frame.aim = NeoSmoothStep(s_layer.aim);
	const NeoCrosshairFamily family = NeoCrosshairFamilyOf(pWeapon);
	frame.alpha = cl_neo_gunplay_crosshair_alpha.GetFloat() * visible;
	frame.sinceBoot = now - s_layer.bootStart;
	frame.sinceShot = now - s_layer.shotTime;
	// A watched player's clip and next attack go to them alone: no magazine readouts, and the gun's readiness from
	// its last shot and its cycle.
	frame.clip = shots.bSpectating ? -1 : pWeapon->Clip1();
	frame.maxClip = shots.bSpectating ? -1 : pWeapon->GetMaxClip1();
	frame.cycle = pWeapon->GetFireRate();
	if (frame.cycle <= 0.0f)
	{
		frame.ready = 1.0f;
	}
	else if (shots.bSpectating)
	{
		frame.ready = clamp((now - s_layer.shotTime) / frame.cycle, 0.0f, 1.0f);
	}
	else
	{
		// The next attack is in the gun's predicted time, ahead of the clock the HUD draws on: measured against that,
		// the readiness ran late (the shotguns' rings still filling when they could fire). Against the predicted
		// time, between its ticks.
		C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
		const float predictedNow = pLocal->GetFinalPredictedTime() + gpGlobals->interpolation_amount * TICK_INTERVAL;
		frame.ready = clamp(1.0f - (pWeapon->m_flNextPrimaryAttack - predictedNow) / frame.cycle, 0.0f, 1.0f);
		// The shotguns: also from the shot itself. The AA13 only adds its fire rate to its next attack, which after a
		// pause is still in the past, so that alone showed it ready (or half) straight after a shot.
		const NeoSpreadPattern &pattern = NeoSpreadPivotLastPattern();
		if (pattern.pWeapon == pWeapon && predictedNow >= pattern.fired)
		{
			frame.ready = Min(frame.ready, clamp((predictedNow - pattern.fired) / frame.cycle, 0.0f, 1.0f));
		}
	}
	s_layer.lastReady = frame.ready;
	frame.dt = dt;
	frame.bBoot = bBoot;
	frame.bShot = bShot;
	// Layer 1, the aim crosshair, and the link between the layers.
	NeoGunplayAimUpdate(frame);

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

	// Aimed: the projected shooting space, behind the rest (stubbed for now, see neo_gunplay_tunnel.h).
	if (frame.aim > 0.0f)
	{
		NeoGunplayPaintTunnel(pPlayer, pWeapon->GetBulletSpread().x, color, frame.alpha * frame.aim * TUNNEL_OPACITY);
	}

	// A shot scrambles it: a jitter and a flicker, as the sight ghost's; less on fast guns, whose shots come too
	// often for it to be anything but noise.
	frame.scramble = Max(0.0f, 1.0f - frame.sinceShot / SHOT_TIME) * Min(1.0f, frame.cycle / SCRAMBLE_CYCLE)
		* cl_neo_gunplay_crosshair_scramble.GetFloat();
	frame.jitter.Init(0.0f, 0.0f);
	if (frame.scramble > 0.0f)
	{
		frame.jitter.Init(random->RandomFloat(-2.0f, 2.0f) * frame.scramble * frame.s,
			random->RandomFloat(-2.0f, 2.0f) * frame.scramble * frame.s);
		frame.alpha *= (gpGlobals->framecount & 1) ? 1.0f - 0.35f * frame.scramble : 1.0f;
	}

	// Behind the families: the spread ghost, calm and exact.
	NeoGunplayPaintSpreadGhost(frame);
	switch (family)
	{
	case NEO_CROSSHAIR_SMG:		NeoCrosshairPaintSmg(frame); break;
	case NEO_CROSSHAIR_MG:		NeoCrosshairPaintMg(frame); break;
	case NEO_CROSSHAIR_SHOTGUN:	NeoCrosshairPaintShotgun(frame); break;
	case NEO_CROSSHAIR_PISTOL:	NeoCrosshairPaintPistol(frame); break;
	case NEO_CROSSHAIR_SCOPED:	NeoCrosshairPaintScoped(frame); break;
	case NEO_CROSSHAIR_RIFLE:
	default:					NeoCrosshairPaintRifle(frame); break;
	}
	// Layer 2, the impact marks, then layer 1 on top: the aim crosshair and the bridges to the spread view.
	NeoGunplayPaintMarks(frame);
	NeoGunplayPaintAim(frame);
	NeoGhostFlush();
}
