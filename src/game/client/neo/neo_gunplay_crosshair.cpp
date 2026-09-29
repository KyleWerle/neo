#include "cbase.h"
#include "neo_gunplay_crosshair.h"
#include "neo_ghost_stroke.h"
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
ConVar cl_neo_gunplay_crosshair_alpha("cl_neo_gunplay_crosshair_alpha", "0.8", FCVAR_ARCHIVE,
	"Opacity of the crosshair layer.", true, 0, true, 1);
ConVar cl_neo_gunplay_crosshair_parallax("cl_neo_gunplay_crosshair_parallax", "1", FCVAR_ARCHIVE,
	"How much the layer's near parts (caps, post) move with the gun's knock and pivot against your crosshair.",
	true, 0, true, 3);

// Sizes in pixels at 1080p.
static constexpr float CROSS_ARM = 4.0f;			// the plain centre cross, each arm
static constexpr float GAP = 5.0f;					// beyond the spread's edge
static constexpr float HIP_CAP = 4.0f;				// "Alt": short square caps
static constexpr float AIM_CAP = 13.0f;			// "Default": long pill caps
static constexpr float HIP_POST = 5.0f;
static constexpr float AIM_POST = 9.0f;
static constexpr float TICK_WIDTH = 5.0f;			// the ladder's ticks
static constexpr float TICK_STEP = 3.0f;
static constexpr int MAX_TICKS = 12;				// more rounds than this: a tick stands for a group
static constexpr float LADDER_PARALLAX = 0.4f;		// the ladder sits further out than the caps
static constexpr float SPREAD_TIME = 0.04f;		// the spread's ease (a critically damped spring's time constant)
static constexpr float AIM_TIME = 0.15f;			// hip to aimed look
static constexpr float TRACE_TIME = 0.11f;			// coming online, as the sight ghost does
static constexpr float TYPE_DELAY = 0.04f;			// then the readout types in
static constexpr float TYPE_TIME = 0.16f;
static constexpr float TUNNEL_OPACITY = 0.6f;		// of the layer's
static constexpr float READOUT_GAP = 4.0f;
static constexpr float READY_WIDTH = 10.0f;		// the ready bar under the readout, on slow guns
static constexpr float READY_CYCLE = 0.25f;		// guns cycling this slowly or slower get it
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
} s_layer;

// The readout's font, as the sight ghost's.
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

bool NeoGunplayReplacesCrosshair(C_NEOBaseCombatWeapon *pWeapon, int crosshairStyle)
{
	return LayerShown(pWeapon) && crosshairStyle == CROSSHAIR_STYLE_DEFAULT;
}

void NeoGunplayPaintCrosshairLayer(C_NEOBaseCombatWeapon *pWeapon, const Color &color, int x, int y, bool bCentre)
{
	auto *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (!LayerShown(pWeapon) || !pPlayer)
	{
		return;
	}
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	const float s = tall / 1080.0f;

	// Coming online: whenever the layer wasn't drawn the frame before, or the gun changed, it traces in again.
	const float now = gpGlobals->realtime;
	const float dt = clamp(now - s_layer.lastTime, 0.0f, 0.1f);
	s_layer.lastTime = now;
	if (s_layer.lastFrame != gpGlobals->framecount - 1 || s_layer.pWeapon != pWeapon)
	{
		s_layer.bootStart = now;
		s_layer.spread = static_cast<float>(HalfInaccuracyConeInScreenPixels(pWeapon, wide / 2));
		s_layer.spreadVelocity = 0.0f;
		s_layer.aim = pPlayer->IsInAim() ? 1.0f : 0.0f;
		s_layer.pWeapon = pWeapon;
		s_layer.lastClip = pWeapon->Clip1();
	}
	// A shot (the clip drops) scrambles the layer a moment.
	if (pWeapon->Clip1() < s_layer.lastClip)
	{
		s_layer.shotTime = now;
	}
	s_layer.lastClip = pWeapon->Clip1();
	s_layer.lastFrame = gpGlobals->framecount;
	NeoGhostPen pen;
	pen.scale = s;
	pen.trace = NeoSmoothStep((now - s_layer.bootStart) / TRACE_TIME);

	// The spread's edge, eased (it steps shot to shot), and the hip-to-aimed look.
	const float target = static_cast<float>(HalfInaccuracyConeInScreenPixels(pWeapon, wide / 2));
	const float omega = 1.0f / SPREAD_TIME;
	s_layer.spreadVelocity += ((target - s_layer.spread) * omega * omega - 2.0f * omega * s_layer.spreadVelocity) * dt;
	s_layer.spread = Max(0.0f, s_layer.spread + s_layer.spreadVelocity * dt);
	s_layer.aim = Approach(pPlayer->IsInAim() ? 1.0f : 0.0f, s_layer.aim, dt / AIM_TIME);
	const float aim = NeoSmoothStep(s_layer.aim);

	// The near parts ride the gun's turn from the aim (knock, pivot); the centre stays.
	const float scaledFov = DEG2RAD(ScaleFOVByWidthRatio(pPlayer->GetFOV(), engine->GetScreenAspectRatio() * 0.75f)) * 0.5f;
	const Vector2D deviation = GunDeviation(pPlayer, (wide * 0.5f) / tanf(scaledFov))
		* cl_neo_gunplay_crosshair_parallax.GetFloat();
	const Vector2D centre(static_cast<float>(x), static_cast<float>(y));
	float alpha = cl_neo_gunplay_crosshair_alpha.GetFloat();

	// The plain centre cross in place of the Default crosshair: steady, full strength, traced in with the rest.
	if (bCentre)
	{
		NeoGhostBegin(color, color.a());
		const float arm = CROSS_ARM * s;
		NeoGhostStroke(pen, centre - Vector2D(arm, 0.0f), centre + Vector2D(arm, 0.0f), NEO_GHOST_MEDIUM);
		NeoGhostStroke(pen, centre - Vector2D(0.0f, arm), centre + Vector2D(0.0f, arm), NEO_GHOST_MEDIUM);
	}

	// Aimed: the projected shooting space, behind the rest.
	if (aim > 0.0f)
	{
		NeoGunplayPaintTunnel(pPlayer, pWeapon->GetBulletSpread().x, color, alpha * aim * TUNNEL_OPACITY);
	}

	// A shot scrambles it: a jitter and a flicker, as the sight ghost's; less on fast guns, whose shots come too
	// often for it to be anything but noise.
	Vector2D jitter(0.0f, 0.0f);
	const float scramble = (1.0f - (now - s_layer.shotTime) / SHOT_TIME) * Min(1.0f, pWeapon->GetFireRate() / SCRAMBLE_CYCLE);
	if (scramble > 0.0f)
	{
		jitter.Init(random->RandomFloat(-2.0f, 2.0f) * scramble * s, random->RandomFloat(-2.0f, 2.0f) * scramble * s);
		alpha *= (gpGlobals->framecount & 1) ? 1.0f - 0.65f * scramble : 1.0f;
	}
	const Vector2D near = centre + deviation + jitter;
	const float edge = s_layer.spread + GAP * s;

	NeoGhostBegin(color, RoundFloatToInt(255.0f * alpha));

	// End caps on the horizontal: short and square at the hip, long pills aimed.
	const float cap = Lerp(aim, HIP_CAP, AIM_CAP) * s;
	const Vector2D readoutAt = near + Vector2D(edge + cap + READOUT_GAP * s, 0.0f);
	for (int side = -1; side <= 1; side += 2)
	{
		NeoGhostStroke(pen, near + Vector2D(side * edge, 0.0f), near + Vector2D(side * (edge + cap), 0.0f), NEO_GHOST_HEAVY);
	}
	// The post below, heavier aimed.
	const float post = Lerp(aim, HIP_POST, AIM_POST) * s;
	NeoGhostStroke(pen, near + Vector2D(0.0f, edge), near + Vector2D(0.0f, edge + post),
		(aim >= 0.5f) ? NEO_GHOST_HEAVY : NEO_GHOST_MEDIUM);

	// Aimed: the ladder above is the magazine, a tick a round (or a group of them), clearing from the top.
	const int clip = pWeapon->Clip1(), maxClip = pWeapon->GetMaxClip1();
	if (aim > 0.0f && clip > 0 && maxClip > 0)
	{
		const int perTick = (maxClip + MAX_TICKS - 1) / MAX_TICKS;
		const int ticks = (clip + perTick - 1) / perTick;
		const Vector2D ladder = centre + deviation * LADDER_PARALLAX + jitter;
		NeoGhostPen tickPen = pen;
		tickPen.trace = pen.trace * aim;
		for (int i = 0; i < ticks; ++i)
		{
			const float at = edge + (i + 1) * TICK_STEP * s;
			NeoGhostStroke(tickPen, ladder + Vector2D(-TICK_WIDTH * 0.5f * s, -at), ladder + Vector2D(TICK_WIDTH * 0.5f * s, -at),
				NEO_GHOST_LIGHT);
		}
	}

	if (aim <= 0.0f)
	{
		return;
	}
	// Aimed: the readout beside the right cap, typed in behind a cursor: the cone's full width in degrees.
	const vgui::HFont font = ReadoutFont();
	const float typed = Min(aim, clamp((now - s_layer.bootStart - TYPE_DELAY) / TYPE_TIME, 0.0f, 1.0f));
	if (font != vgui::INVALID_FONT && typed > 0.0f)
	{
		wchar_t text[16];
		V_snwprintf(text, ARRAYSIZE(text) - 1, L"%.1f", RAD2DEG(2.0f * atanf(pWeapon->GetBulletSpread().x)));
		const int length = V_wcslen(text);
		int count = Min(length, static_cast<int>(ceilf(length * typed)));
		if (typed < 1.0f)
		{
			text[count++] = L'_';
		}
		vgui::surface()->DrawSetTextFont(font);
		vgui::surface()->DrawSetTextColor(color.r(), color.g(), color.b(), RoundFloatToInt(255.0f * alpha * aim * 0.7f));
		vgui::surface()->DrawSetTextPos(RoundFloatToInt(readoutAt.x), RoundFloatToInt(readoutAt.y) - vgui::surface()->GetFontTall(font) / 2);
		vgui::surface()->DrawPrintText(text, count);
	}
	// Slow guns: a ready bar under it, filling as the gun cycles to its next shot.
	const float cycle = pWeapon->GetFireRate();
	if (cycle >= READY_CYCLE && font != vgui::INVALID_FONT)
	{
		const float ready = clamp(1.0f - (pWeapon->m_flNextPrimaryAttack - gpGlobals->curtime) / cycle, 0.0f, 1.0f);
		const Vector2D bar = readoutAt + Vector2D(0.0f, vgui::surface()->GetFontTall(font) * 0.5f + 2.0f * s);
		NeoGhostBegin(color, RoundFloatToInt(255.0f * alpha * aim * (ready >= 1.0f ? 1.0f : 0.5f)));
		NeoGhostStroke(pen, bar, bar + Vector2D(READY_WIDTH * s * ready, 0.0f), NEO_GHOST_MEDIUM);
	}
}
