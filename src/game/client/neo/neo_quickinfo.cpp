#include "cbase.h"
#include "neo_quickinfo.h"
#include "neo_quickinfo_internal.h"
#include "neo_gunplay_shots.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "neo_gamerules.h"
#include "weapon_neobasecombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_hud_quickinfo("cl_neo_hud_quickinfo", "0", FCVAR_ARCHIVE,
	"Quick info around the crosshair: integrity, therm-optic and aux as a housing at the centre, in place of the"
	" health / therm-optic / aux panel at the screen's edge.", true, 0, true, 1);
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
static constexpr float BOOT_TIME = 0.55f;		// the housing's reveal on spawn
static constexpr float BUSY_HOLD = 2.2f;		// full for this long after any change
static constexpr float SWAY_MAX = 4.0f;			// pixels at 1080p
static constexpr float SWAY_GAIN = 0.022f;		// pixels per degree a second of turning (180 deg/s reaches the cap)
static constexpr float STEP = 1.0f / 240.0f;	// spring substeps

struct LayerSpring { float depth, omega, damping, phase; Vector2D offset, velocity; };

static struct
{
	LayerSpring layers[LAYER__COUNT] = {
		{ 0.45f, 9.0f, 0.62f, 0.0f },	// detail: far back, the least motion
		{ 0.85f, 12.0f, 0.58f, 1.7f },	// brackets
		{ 1.0f, 13.0f, 0.6f, 3.1f },	// bar
		{ 1.15f, 11.0f, 0.55f, 4.4f },	// labels
		{ 1.35f, 15.0f, 0.52f, 5.6f },	// vision dots: nearest
	};
	int lastFrame = -1;
	float lastTime = 0.0f;
	float bootTime = -100.0f;
	float viewChanged = -100.0f;
	int neoClass = -1;
	float fade = 0.0f;
	float lastChange = -100.0f;
	float hp = 1.0f, hpRaw = 1.0f, cloak = 1.0f, aux = 100.0f, auxRaw = 100.0f;
	int hpNumber = 0;
	bool bCloaked = false, bSprinting = false, bVision = false;
	float hpChangeTime = -100.0f, glitchTime = -100.0f;
	float jumpReady[2] = { -100.0f, -100.0f }, jumpSpent[2] = { -100.0f, -100.0f };
	Chip chips[MAX_CHIPS];
	int chipCount = 0;
	QAngle lastAngles;
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
// The bar's fill for integrity: support's bar holds its last 40% (the brackets hold the rest).
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
	// Turning right, the housing trails left; looking down, it trails up.
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
static void Update(C_NEO_Player *pPlayer, Kind kind, float dt, float now, bool bBoot)
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
			s_qi.lastChange = now;
		}
		if (kind == KIND_RECON)
		{
			for (int i = 0; i < 2; ++i)
			{
				const float edge = (i + 1) * JUMP_COST;
				if (s_qi.auxRaw < edge && auxRaw >= edge)
				{
					s_qi.jumpReady[i] = now;
					s_qi.lastChange = now;
				}
			}
			// A super jump spends a cell at once: the upper full cell is the one that goes.
			if (s_qi.auxRaw - auxRaw >= JUMP_COST * 0.9f)
			{
				s_qi.jumpSpent[(s_qi.auxRaw >= JUMP_COST * 2.0f) ? 1 : 0] = now;
				s_qi.lastChange = now;
			}
		}
		if (bCloaked != s_qi.bCloaked || bSprinting != s_qi.bSprinting || bVision != s_qi.bVision)
		{
			s_qi.lastChange = now;
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

	// Full on any change, a low value or an active mode; otherwise down to the floor, never out.
	const bool bBusy = hpRaw <= 0.5f || bCloaked || bSprinting || bVision || now - s_qi.bootTime < 3.4f
		|| (kind == KIND_RECON && auxRaw < JUMP_COST * 2.0f) || ((kind == KIND_ASSAULT || kind == KIND_JUGGERNAUT) && auxRaw < 99.5f)
		|| now - s_qi.lastChange < BUSY_HOLD;
	const float target = bBusy ? 1.0f : cl_neo_hud_quickinfo_floor.GetFloat();
	s_qi.fade = bBoot ? 1.0f : Approach(target, s_qi.fade, dt / ((target > s_qi.fade) ? 0.06f : 0.6f));
}
} // namespace NeoQuickInfo

bool NeoQuickInfoOn()
{
	return cl_neo_hud_quickinfo.GetBool();
}

float NeoQuickInfoDeadzone()
{
	return NeoQuickInfoOn() ? NeoQuickInfo::DEADZONE : 0.0f;
}

void NeoQuickInfoPaint(C_NEO_Player *pPlayer, const Color &color, int x, int y)
{
	using namespace NeoQuickInfo;
	// The local player's own, alive; not through a scope (its view stays as it is) and not where the rules hide
	// the panel it replaces. A watched player's therm-optic and aux never reach this client.
	if (!NeoQuickInfoOn() || !pPlayer || !pPlayer->IsLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsObserver()
		|| (NEORules() && (NEORules()->GetHiddenHudElements() & NEO_HUD_ELEMENT_HEALTH_THERMOPTIC_AUX)))
	{
		s_qi.lastFrame = -1;
		return;
	}
	auto *pWeapon = static_cast<C_NEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon());
	if (pWeapon && (pWeapon->GetNeoWepBits() & NEO_WEP_SCOPEDWEAPON) && pPlayer->IsInAim())
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
	Update(pPlayer, kind, dt, now, bBoot);
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
	frame.centre.Init(static_cast<float>(x), static_cast<float>(y));
	for (int i = 0; i < LAYER__COUNT; ++i)
	{
		frame.sway[i] = s_qi.layers[i].offset;
	}
	frame.alpha = s_qi.fade * NeoSmoothStep(Min(1.0f, boot * 1.4f));
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
	PaintHousing(frame);
	// The labels read at full strength whatever the fade.
	frame.alpha = NeoSmoothStep(Min(1.0f, boot * 1.4f));
	PaintLabels(frame);
}
