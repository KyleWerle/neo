#include "cbase.h"
#include "neo_quickinfo.h"
#include "neo_cyberbrain.h"
#include "neo_quickinfo_internal.h"
#include "neo_gunplay_shots.h"
#include "neo_gunplay_crosshair.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "neo_gamerules.h"
#include "weapon_neobasecombatweapon.h"
#include "neo_ironsight_profile.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_hud_quickinfo_floor("cl_neo_hud_quickinfo_floor", "0.3", FCVAR_ARCHIVE,
	"The quick info's opacity at rest (it rises to full on any change, a low value or an active mode).", true, 0, true, 1);
ConVar cl_neo_hud_quickinfo_detail("cl_neo_hud_quickinfo_detail", "1", FCVAR_ARCHIVE,
	"The quick info's instrument detail: graduations, channel codes, registration marks, the number's flicker, the"
	" hollow cloak, the failing signal at critical integrity.", true, 0, true, 1);
ConVar cl_neo_hud_quickinfo_sway("cl_neo_hud_quickinfo_sway", "1", FCVAR_ARCHIVE,
	"The quick info's layered sway: each layer trailing your turns by its depth, breathing, bobbing when sprinting"
	" (0 = still).", true, 0, true, 1);

namespace NeoQuickInfo
{
static constexpr float BOOT_TIME = 0.55f;		// the band's reveal on spawn
static constexpr float BUSY_HOLD = 2.2f;		// a part stays full this long after it changes
static constexpr float BOOT_HOLD = 3.4f;		// everything stays full this long after a boot (the labels read)
static constexpr float FADE_UP = 0.06f, FADE_DOWN = 0.6f;	// a part's rise and settle, seconds
static constexpr float SWAY_MAX = 4.0f;			// pixels at 1080p
static constexpr float SWAY_GAIN = 0.022f;		// pixels per degree a second of turning (180 deg/s reaches the cap)
static constexpr float STEP = 1.0f / 240.0f;	// spring substeps

struct LayerSpring { float depth, omega, damping, phase; Vector2D offset, velocity; };

static struct
{
	LayerSpring layers[LAYER__COUNT] = {
		{ 0.45f, 9.0f, 0.62f, 0.0f },	// detail: far back, the least motion
		{ 0.85f, 12.0f, 0.58f, 1.7f },	// wing frames
		{ 1.0f, 13.0f, 0.6f, 3.1f },	// bar
		{ 1.15f, 11.0f, 0.55f, 4.4f },	// labels
		{ 1.35f, 15.0f, 0.52f, 5.6f },	// vision dots: nearest
	};
	int lastFrame = -1;
	float lastTime = 0.0f;
	float bootTime = -100.0f;
	float viewChanged = -100.0f;
	int neoClass = -1;
	float chassis = 1.0f;			// the steady parts' fade (full only while booting)
	float fade[PART__COUNT] = {};
	float changed[PART__COUNT] = { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
	float hp = 1.0f, hpRaw = 1.0f, cloak = 1.0f, aux = 100.0f, auxRaw = 100.0f;
	int hpNumber = 0;
	bool bCloaked = false, bSprinting = false, bVision = false;
	float hpChangeTime = -100.0f, glitchTime = -100.0f;
	float jumpReady[2] = { -100.0f, -100.0f }, jumpSpent[2] = { -100.0f, -100.0f };
	Chip chips[MAX_CHIPS];
	int chipCount = 0;
	QAngle lastAngles;
	int wantedFrame = -1;		// the last frame it was meant to draw (a fade may have held it back)
	wchar_t ammoKey[96] = L"";
} s_qi;

static Kind KindOf(int neoClass)
{
	switch (neoClass)
	{
	case NEO_CLASS_RECON:		return KIND_RECON;
	case NEO_CLASS_SUPPORT:		return KIND_SUPPORT;
	case NEO_CLASS_JUGGERNAUT:	return KIND_JUGGERNAUT;
	default:					return KIND_ASSAULT;	// assault and the VIP
	}
}
static const char *VisionName(int neoClass)
{
	switch (neoClass)
	{
	case NEO_CLASS_RECON:	return "NIGHT VISION";
	case NEO_CLASS_ASSAULT:	return "MOTION VISION";
	case NEO_CLASS_SUPPORT:	return "THERMAL";
	default:				return nullptr;
	}
}
// The bar's fill for integrity: support's bar holds its last 40% (the wings hold the rest).
static float BarFraction(Kind kind, float hp)
{
	return (kind == KIND_SUPPORT) ? clamp(hp / 0.4f, 0.0f, 1.0f) : hp;
}

// Every layer trails the view's turns by its depth (capped), breathes slowly out of phase, and bobs while
// sprinting with the bob travelling through the depths; each on its own slightly under-damped spring.
static void Sway(C_NEO_Player *pPlayer, float dt, float now, bool bBoot)
{
	const QAngle angles = pPlayer->EyeAngles();
	Vector2D rate(0.0f, 0.0f);
	if (!bBoot && dt > 0.0f)
	{
		rate.x = AngleNormalize(angles.y - s_qi.lastAngles.y) / dt;	// yaw: positive turning left
		rate.y = AngleNormalize(angles.x - s_qi.lastAngles.x) / dt;	// pitch: positive looking down
	}
	s_qi.lastAngles = angles;
	const bool bOn = cl_neo_hud_quickinfo_sway.GetBool();
	// Turning right, the band trails left; looking down, it trails up.
	const Vector2D trail(clamp(rate.x * SWAY_GAIN, -SWAY_MAX, SWAY_MAX), clamp(-rate.y * SWAY_GAIN, -SWAY_MAX, SWAY_MAX));
	for (LayerSpring &layer : s_qi.layers)
	{
		if (!bOn || bBoot)
		{
			layer.offset.Init(0.0f, 0.0f);
			layer.velocity.Init(0.0f, 0.0f);
			if (!bOn)
			{
				continue;
			}
		}
		const Vector2D breath(0.35f * sinf(now * 0.55f + layer.phase), 0.5f * sinf(now * 0.8f + layer.phase * 1.3f));
		const float bob = s_qi.bSprinting ? sinf(now * 11.0f - layer.depth * 1.6f) * 0.9f : 0.0f;
		const Vector2D goal = (trail + breath + Vector2D(0.0f, bob)) * layer.depth;
		const float omega = layer.omega, damping = layer.damping;
		for (float left = dt; left > 0.0f; left -= STEP)
		{
			const float h = Min(left, STEP);
			layer.velocity += ((goal - layer.offset) * (omega * omega) - layer.velocity * (2.0f * damping * omega)) * h;
			layer.offset += layer.velocity * h;
		}
		if (!layer.offset.IsValid() || !layer.velocity.IsValid())
		{
			layer.offset.Init(0.0f, 0.0f);
			layer.velocity.Init(0.0f, 0.0f);
		}
	}
}

// The values, eased, and what changed: chips and the hitch on a hit, a jump cell locking or spent.
static void Update(C_NEO_Player *pPlayer, Kind kind, float dt, float now, bool bBoot, bool bAmmoBusy)
{
	static ConVarRef cl_neo_hud_health_mode("cl_neo_hud_health_mode");
	const float hpRaw = clamp(static_cast<float>(pPlayer->GetHealth()) / Max(1, pPlayer->GetMaxHealth()), 0.0f, 1.0f);
	const float cloakRaw = clamp(pPlayer->CloakPower_CurrentVisualPercentage() / 100.0f, 0.0f, 1.0f);
	const float auxRaw = clamp(pPlayer->m_HL2Local.m_flSuitPower, 0.0f, 100.0f);
	const bool bCloaked = pPlayer->IsCloaked(), bSprinting = pPlayer->IsSprinting(), bVision = pPlayer->IsInVision();
	if (bBoot)
	{
		s_qi.hp = s_qi.hpRaw = hpRaw;
		s_qi.cloak = cloakRaw;
		s_qi.aux = s_qi.auxRaw = auxRaw;
		s_qi.chipCount = 0;
	}
	else
	{
		if (hpRaw < s_qi.hpRaw - 0.001f)
		{
			// A hit: a chip of what was lost, the hitch, the number's flicker.
			if (s_qi.chipCount == MAX_CHIPS)
			{
				V_memmove(s_qi.chips, s_qi.chips + 1, sizeof(Chip) * (MAX_CHIPS - 1));
				--s_qi.chipCount;
			}
			const float from = BarFraction(kind, s_qi.hpRaw), to = BarFraction(kind, hpRaw);
			if (from > to)
			{
				s_qi.chips[s_qi.chipCount++] = { from, to, now };
			}
			s_qi.glitchTime = now;
		}
		if (fabsf(hpRaw - s_qi.hpRaw) > 0.001f)
		{
			s_qi.hpChangeTime = now;
			s_qi.changed[PART_INTEGRITY] = now;
		}
		if (kind == KIND_RECON)
		{
			for (int i = 0; i < 2; ++i)
			{
				const float edge = (i + 1) * JUMP_COST;
				if (s_qi.auxRaw < edge && auxRaw >= edge)
				{
					s_qi.jumpReady[i] = now;
					s_qi.changed[PART_RIGHT] = now;
				}
			}
			// A super jump spends a cell at once: the upper full cell is the one that goes.
			if (s_qi.auxRaw - auxRaw >= JUMP_COST * 0.9f)
			{
				s_qi.jumpSpent[(s_qi.auxRaw >= JUMP_COST * 2.0f) ? 1 : 0] = now;
				s_qi.changed[PART_RIGHT] = now;
			}
		}
		if (bCloaked != s_qi.bCloaked)
		{
			s_qi.changed[PART_LEFT] = now;	// the therm-optic (recon, assault)
		}
		if (bVision != s_qi.bVision)
		{
			s_qi.changed[PART_VISION] = now;
		}
	}
	s_qi.hpRaw = hpRaw;
	s_qi.auxRaw = auxRaw;
	s_qi.bCloaked = bCloaked;
	s_qi.bSprinting = bSprinting;
	s_qi.bVision = bVision;
	s_qi.hpNumber = pPlayer->GetDisplayedHealth(cl_neo_hud_health_mode.GetInt());
	s_qi.hp += (hpRaw - s_qi.hp) * Min(1.0f, dt / 0.08f);
	s_qi.cloak += (cloakRaw - s_qi.cloak) * Min(1.0f, dt / 0.06f);
	s_qi.aux += (auxRaw - s_qi.aux) * Min(1.0f, dt / 0.06f);
	int kept = 0;
	for (int i = 0; i < s_qi.chipCount; ++i)
	{
		if (now - s_qi.chips[i].time < 0.45f)
		{
			s_qi.chips[kept++] = s_qi.chips[i];
		}
	}
	s_qi.chipCount = kept;

	// Each part: full while it's used, changing, low or active, and a moment after; otherwise down to the floor, never
	// out. Everything is full for a while after a boot, then settles part by part.
	const bool bBooting = now - s_qi.bootTime < BOOT_HOLD;
	const auto held = [&](Part part) { return bBooting || now - s_qi.changed[part] < BUSY_HOLD; };
	bool busy[PART__COUNT];
	busy[PART_INTEGRITY] = held(PART_INTEGRITY) || hpRaw <= 0.5f;
	const bool bCloakBusy = bCloaked || cloakRaw < 0.995f;	// cloaked, or recharging after
	const bool bSprintBusy = bSprinting || auxRaw < 99.5f;	// sprinting, or recovering after
	switch (kind)
	{
	case KIND_RECON:		busy[PART_LEFT] = bCloakBusy; busy[PART_RIGHT] = auxRaw < JUMP_COST * 2.0f; break;
	case KIND_ASSAULT:		busy[PART_LEFT] = bCloakBusy; busy[PART_RIGHT] = bSprintBusy; break;
	case KIND_SUPPORT:		busy[PART_LEFT] = busy[PART_RIGHT] = busy[PART_INTEGRITY]; break;	// the wings are armour
	case KIND_JUGGERNAUT:	busy[PART_LEFT] = busy[PART_RIGHT] = bSprintBusy; break;
	}
	busy[PART_LEFT] = busy[PART_LEFT] || held(PART_LEFT);
	busy[PART_RIGHT] = busy[PART_RIGHT] || held(PART_RIGHT);
	busy[PART_AMMO] = held(PART_AMMO) || bAmmoBusy;
	busy[PART_VISION] = held(PART_VISION) || bVision;
	const float floor = cl_neo_hud_quickinfo_floor.GetFloat();
	for (int i = 0; i < PART__COUNT; ++i)
	{
		const float target = busy[i] ? 1.0f : floor;
		s_qi.fade[i] = bBoot ? 1.0f : Approach(target, s_qi.fade[i], dt / ((target > s_qi.fade[i]) ? FADE_UP : FADE_DOWN));
	}
	const float chassis = bBooting ? 1.0f : floor;
	s_qi.chassis = bBoot ? 1.0f : Approach(chassis, s_qi.chassis, dt / FADE_DOWN);
}
} // namespace NeoQuickInfo

bool NeoQuickInfoOn()
{
	return NeoHudStyleCurrent() == NEO_HUD_STYLE_RACER;	// the racer band (cl_neo_hud_style 3)
}

bool NeoQuickInfoShowing()
{
	return NeoQuickInfoOn() && NeoQuickInfo::s_qi.wantedFrame >= gpGlobals->framecount - 1;
}

void NeoQuickInfoPaint(C_NEO_Player *pPlayer, const Color &color)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_HUD, "NeoQuickInfoPaint");
	using namespace NeoQuickInfo;
	// The local player's own, alive, and not where the rules hide the panel it replaces; through a scope too (the
	// stock panels stay up there). A watched player's therm-optic and aux never reach this client.
	if (!NeoQuickInfoOn() || !pPlayer || !pPlayer->IsLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsObserver()
		|| (NEORules() && (NEORules()->GetHiddenHudElements() & NEO_HUD_ELEMENT_HEALTH_THERMOPTIC_AUX)))
	{
		s_qi.lastFrame = -1;
		return;
	}
	s_qi.wantedFrame = gpGlobals->framecount;
	// Under a fade to black it waits, and boots (typing its labels) as the view comes back up.
	const float visible = NeoHudFadeVisible();
	if (NeoHudFadedOut())
	{
		s_qi.lastFrame = -1;
		return;
	}
	const float now = gpGlobals->realtime;
	const Kind kind = KindOf(pPlayer->GetClass());
	// Boots (and types its labels) on every spawn, on a class change, and whenever it wasn't drawn the frame before.
	const float viewChanged = NeoGunplayWatchShots().viewChanged;
	const bool bBoot = s_qi.lastFrame != gpGlobals->framecount - 1 || viewChanged != s_qi.viewChanged
		|| pPlayer->GetClass() != s_qi.neoClass;
	float dt = clamp(now - s_qi.lastTime, 0.0f, 0.1f);
	if (bBoot)
	{
		dt = 0.0f;
		s_qi.bootTime = now;
		s_qi.viewChanged = viewChanged;
		s_qi.neoClass = pPlayer->GetClass();
	}
	s_qi.lastTime = now;
	s_qi.lastFrame = gpGlobals->framecount;
	// The ammo: a shot, a reload or a switch brings its part up; so does a low magazine or a hot BALC.
	Ammo ammo;
	ReadAmmo(pPlayer, ammo);
	wchar_t key[ARRAYSIZE(s_qi.ammoKey)];
	V_snwprintf(key, ARRAYSIZE(key), L"%ls|%d|%ls|%d|%ls", ammo.name, ammo.rounds, ammo.mags, RoundFloatToInt(ammo.heat * 20.0f),
		ammo.pMode ? ammo.pMode : L"");
	if (!bBoot && V_wcscmp(key, s_qi.ammoKey) != 0)
	{
		s_qi.changed[PART_AMMO] = now;
	}
	V_wcsncpy(s_qi.ammoKey, key, sizeof(s_qi.ammoKey));
	const bool bAmmoBusy = (ammo.maxRounds > 1 && ammo.rounds <= ammo.maxRounds / 5) || (ammo.bHeat && ammo.heat > 0.05f);
	Update(pPlayer, kind, dt, now, bBoot, bAmmoBusy);
	Sway(pPlayer, dt, now, bBoot);

	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	QuickFrame frame;
	frame.kind = kind;
	frame.color = color;
	frame.s = tall / 1080.0f;
	const float boot = clamp((now - s_qi.bootTime) / BOOT_TIME, 0.0f, 1.0f);
	frame.pen.scale = frame.s;
	frame.pen.trace = NeoSmoothStep(boot);
	// Fixed to the screen, not the aim: the crosshair roams free above it.
	frame.centre.Init(wide * 0.5f, tall * 0.5f);
	for (int i = 0; i < LAYER__COUNT; ++i)
	{
		frame.sway[i] = s_qi.layers[i].offset;
	}
	frame.reveal = NeoSmoothStep(Min(1.0f, boot * 1.4f)) * visible;
	frame.alpha = s_qi.chassis * frame.reveal;
	for (int i = 0; i < PART__COUNT; ++i)
	{
		frame.parts[i] = s_qi.fade[i] * frame.reveal;
	}
	frame.now = now;
	frame.bDetail = cl_neo_hud_quickinfo_detail.GetBool();
	frame.hp = s_qi.hp;
	frame.hpNumber = s_qi.hpNumber;
	frame.hpChangeTime = s_qi.hpChangeTime;
	frame.glitchTime = s_qi.glitchTime;
	frame.pChips = s_qi.chips;
	frame.chips = s_qi.chipCount;
	frame.cloak = s_qi.cloak;
	frame.bCloaked = s_qi.bCloaked;
	frame.aux = s_qi.aux;
	frame.bSprinting = s_qi.bSprinting;
	frame.bVision = s_qi.bVision;
	frame.pVisionName = VisionName(pPlayer->GetClass());
	frame.bHasVision = frame.pVisionName != nullptr;
	for (int i = 0; i < 2; ++i)
	{
		frame.jumpReady[i] = s_qi.jumpReady[i];
		frame.jumpSpent[i] = s_qi.jumpSpent[i];
	}
	frame.labels = now - s_qi.bootTime;
	frame.ammo = ammo;
	// The band's dark backing, behind everything (the far layer), darker the brighter the scene behind it; a little
	// lighter while the band rests.
	{
		Vector2D spots[MAX_BACKING_SPOTS];
		for (int i = 0; i < MAX_BACKING_SPOTS; ++i)
		{
			spots[i] = frame.centre + Vector2D((i - 2) * 160.0f, BAND_Y) * frame.s;
		}
		float active = 0.0f;
		for (const float part : frame.parts)
		{
			active = Max(active, part);
		}
		const float alpha = BackingAlpha(BACKING_BAND, pPlayer, spots, MAX_BACKING_SPOTS, dt, bBoot) * (0.5f + 0.5f * active)
			* frame.reveal;
		PaintBacking(frame, LAYER_DETAIL, Vector2D(0.0f, BAND_Y + 5.0f), Vector2D(400.0f, 62.0f), Vector2D(60.0f, 40.0f), alpha);
	}
	PaintBand(frame);
	PaintAmmo(WithAlpha(frame, frame.parts[PART_AMMO]));
	PaintSpeed(frame, pPlayer, dt, bBoot, cl_neo_hud_quickinfo_floor.GetFloat());
	// The labels read at full strength whatever the fade.
	frame.alpha = frame.reveal;
	PaintLabels(frame);
}
