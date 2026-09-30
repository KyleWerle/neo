#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_ironsights.h"
#include "neo_hud_spring.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Attention and placement. Each group's salience (what it senses mattering now: a hit, low integrity, sprinting, a
// reload, cloaking, standing in light) pulls it from its far home toward its near one fast, and it lets go slowly.
// Placement is a critically damped spring (no overshoot, no wobble); groups are locked to the screen while you look
// round (nothing trails the view); a slow balance eases the quieter groups sideways to keep the weight even against
// the gun; nothing enters the keep-out round the crosshair. Motion comfort limits all of it.

ConVar cl_neo_hud_motion("cl_neo_hud_motion", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's motion. 0 = still (groups never move; attention shows as size and strength), 1 = calm (short"
	" travel), 2 = full.", true, 0, true, 2);

namespace NeoCyberbrain
{
constexpr float OMEGA = 12.0f;					// the placement spring (critically damped)
constexpr float KEEPOUT_X = 300.0f, KEEPOUT_Y = 220.0f, KEEPOUT_MARGIN = 1.15f;	// pixels at 1080p round the centre
constexpr float GUN_WEIGHT = 1.2f;
constexpr float SCREEN_MARGIN = 6.0f;				// pixels at 1080p every group keeps from the screen's edges
constexpr float GUN_X = 330.0f, GUN_Y = 150.0f;	// where the viewmodel's weight sits, from the gun hand's corner (1080p)
constexpr float BALANCE_GAIN = 0.35f, BALANCE_MAX = 40.0f, BALANCE_EASE = 1.5f;
// The deep layer: a softer spring (a touch under-damped, as the racer band's layers), and the drift as you turn,
// capped at a few pixels (the racer band's sway).
constexpr float DEEP_OMEGA = 7.0f, DEEP_DAMPING = 0.7f;
constexpr float TURN_GAIN = 0.022f, TURN_MAX = 4.0f, DEEP_DEPTH = 1.3f;

static struct { Vector2D offset, vel; } s_ringDeep;

// Moves `at` toward `goal` on the deep spring.
static void DeepSpring(Vector2D &at, Vector2D &vel, const Vector2D &goal, float dt)
{
	NeoHudSpring(at, vel, goal, DEEP_OMEGA, DEEP_DAMPING, dt);
	if (!at.IsValid() || !vel.IsValid())
	{
		at = goal;
		vel.Init(0.0f, 0.0f);
	}
}

Vector2D RingDeepOffset()
{
	return s_ringDeep.offset;
}

Vector2D Inside(const Frame &f, const Vector2D &centre, const Vector2D &half)
{
	const float margin = SCREEN_MARGIN * f.s;
	const auto push = [margin](float c, float h, float size)
	{
		if (2.0f * h + 2.0f * margin >= size)
			return size * 0.5f - c;	// bigger than the screen: centred
		if (c - h < margin)
			return margin - (c - h);
		if (c + h > size - margin)
			return size - margin - (c + h);
		return 0.0f;
	};
	return Vector2D(push(centre.x, half.x, static_cast<float>(f.wide)), push(centre.y, half.y, static_cast<float>(f.tall)));
}

bool InKeepout(const Frame &f, const Vector2D &p)
{
	return fabsf(p.x - f.centre.x) < KEEPOUT_X * f.s && fabsf(p.y - f.centre.y) < KEEPOUT_Y * f.s;
}

struct Comfort { float travel, pullIn, letGo; };
static Comfort ComfortOf()
{
	// Pulling in fast; letting go slower and eased (Kyle), the seconds it takes to settle.
	static const Comfort s_levels[] = { { 0.0f, 0.3f, 2.8f }, { 0.6f, 0.3f, 2.8f }, { 1.0f, 0.22f, 2.2f } };
	return s_levels[clamp(cl_neo_hud_motion.GetInt(), 0, 2)];
}
// Something that happened at `when`, fading from its peak.
static float Pulse(float now, float when, float peak)
{
	const float age = now - when;
	return (age >= 0.0f && age < 6.0f) ? peak * expf(-age / 1.1f) : 0.0f;
}

static float Salience(const Senses &s, Group group, float now)
{
	switch (group)
	{
	case GROUP_BODY:
		return Max(Max(Pulse(now, s.hitTime, 1.0f), Pulse(now, s.landTime, 0.4f)),
			Max(s.hp < 0.5f ? 0.35f + 0.65f * (1.0f - s.hp / 0.5f) : 0.0f, Max(0.2f * s.crouch, 0.2f * fabsf(s.lean))));
	case GROUP_MOTION:
	{
		const bool bRecovering = (s.bHasSprint && s.aux < 99.5f) || (s.bHasJumps && s.aux < 90.0f);
		return Max(Max(Pulse(now, s.landTime, 0.5f), s.bSprinting ? 0.5f : 0.0f),
			Max(Max(bRecovering ? 0.35f : 0.0f, s.speed > s.runSpeed * 1.2f ? 0.6f : 0.0f), Max(0.3f * s.air, s.bMoving ? 0.1f : 0.0f)));
	}
	case GROUP_OPTICS:
		return Max(Max(Pulse(now, s.cloakChanged, 1.0f), Pulse(now, s.visionChanged, 0.7f)),
			Max(Max(Pulse(now, s.lightChanged, 0.6f), s.bCloaked ? 0.75f : 0.0f),
				Max(Max(s.bHasCloak && s.cloak < 0.98f ? 0.3f : 0.0f, s.bVision ? 0.35f : 0.0f), s.bExposed ? 0.4f : 0.0f)));
	case GROUP_WEAPON:
	{
		// The ghost's uplink in its place: in while it boots or sees someone.
		if (s.bGhost)
			return s.bGhostWorking && s.ghostBoot < 1.0f ? 0.8f : s.ghostContacts > 0 ? 0.7f : 0.4f;
		return Max(Max(Pulse(now, s.ammoChanged, 0.8f), now - s.shotTime < 1.0f ? 0.6f : 0.0f),
			Max(s.bReloading ? 0.85f : 0.0f, s.bAmmoLow || s.heatLevel > 0 ? 0.7f : 0.0f));
	}
	default:
		return 0.05f;
	}
}

void Attend(const Senses &senses, const Home homes[GROUP__COUNT], const Frame &f, float dt, bool bBoot, Place places[GROUP__COUNT])
{
	const Comfort comfort = ComfortOf();
	const auto mirror = [&](const Vector2D &p) { return f.hand > 0 ? p : Vector2D(f.wide - p.x, p.y); };
	// Attention: in fast, out slow.
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		Place &p = places[g];
		p.sal = clamp(Salience(senses, static_cast<Group>(g), f.now), 0.0f, 1.0f);
		if (bBoot)
		{
			p.att = p.attVel = 0.0f;
		}
		else if (p.sal > p.att)
		{
			// In: fast, straight toward it.
			p.att += (p.sal - p.att) * Min(1.0f, dt / comfort.pullIn);
			p.attVel = 0.0f;
		}
		else
		{
			// Out: a critically damped spring from rest, easing off, then settling over about letGo seconds.
			const float omega = 5.0f / comfort.letGo;
			NeoHudSpring(p.att, p.attVel, p.sal, omega, 1.0f, dt);
			p.att = clamp(p.att, p.sal, 1.0f);
		}
	}
	// The balance: the gun and each group, weighted by how present it is, about the screen's centre line.
	const float gunX = f.hand > 0 ? f.wide - GUN_X * f.s : GUN_X * f.s;
	float moment = GUN_WEIGHT * (gunX - f.centre.x), mass = GUN_WEIGHT;
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		const float w = homes[g].weight * (0.4f + 0.6f * places[g].att);
		moment += w * (places[g].pos.x - f.centre.x);
		mass += w;
	}
	const float shift = clamp(-moment / mass * BALANCE_GAIN, -BALANCE_MAX * f.s, BALANCE_MAX * f.s);
	// Turning right, the deep layer trails left; looking down, it trails up (still: none).
	const Vector2D turn = comfort.travel > 0.0f
		? Vector2D(clamp(senses.yawRate * TURN_GAIN, -TURN_MAX, TURN_MAX), clamp(-senses.pitchRate * TURN_GAIN, -TURN_MAX, TURN_MAX)) * (f.s * DEEP_DEPTH)
		: Vector2D(0.0f, 0.0f);
	if (bBoot || comfort.travel <= 0.0f)
	{
		s_ringDeep.offset = turn;
		s_ringDeep.vel.Init(0.0f, 0.0f);
	}
	else
	{
		DeepSpring(s_ringDeep.offset, s_ringDeep.vel, turn, dt);
	}
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		Place &p = places[g];
		const Vector2D far = mirror(homes[g].far), nearer = mirror(homes[g].nearer);
		p.balance = bBoot ? 0.0f : p.balance + (shift * (1.0f - p.att) * comfort.travel - p.balance) * Min(1.0f, dt / BALANCE_EASE);
		Vector2D target = far + (nearer - far) * (p.att * comfort.travel) + Vector2D(p.balance, 0.0f);
		// Never past the screen's edges, whatever its size or shape: the group's extent (at its scale now, moved to the
		// target) held inside, so the spring eases up to the edge rather than being stopped at it.
		Vector2D centre, half;
		GroupExtent(f, g, centre, half);
		target += Inside(f, centre + (target - p.pos), half);
		if (bBoot || !p.bPlaced || comfort.travel <= 0.0f)
		{
			p.pos = target;
			p.vel.Init(0.0f, 0.0f);
			p.bPlaced = true;
		}
		else
		{
			NeoHudSpring(p.pos, p.vel, target, OMEGA, 1.0f, dt);
			if (!p.pos.IsValid() || !p.vel.IsValid())
			{
				p.pos = target;
				p.vel.Init(0.0f, 0.0f);
			}
		}
		// The deep layer: trailing the group, drifting as you turn.
		const Vector2D deepGoal = p.pos + turn;
		if (bBoot || comfort.travel <= 0.0f)
		{
			p.deep = p.pos;
			p.deepVel.Init(0.0f, 0.0f);
		}
		else
		{
			DeepSpring(p.deep, p.deepVel, deepGoal, dt);
		}
		// Never inside the keep-out round the crosshair.
		const Vector2D e((p.pos.x - f.centre.x) / (KEEPOUT_X * f.s), (p.pos.y - f.centre.y) / (KEEPOUT_Y * f.s));
		const float d = e.Length();
		if (d < KEEPOUT_MARGIN && d > 0.001f)
		{
			p.pos.x = f.centre.x + e.x / d * KEEPOUT_MARGIN * KEEPOUT_X * f.s;
			p.pos.y = f.centre.y + e.y / d * KEEPOUT_MARGIN * KEEPOUT_Y * f.s;
		}
		// A backstop for the keep-out's push: still inside the edges, the push's speed dropped.
		GroupExtent(f, g, centre, half);
		const Vector2D inside = Inside(f, centre, half);
		if (inside.x != 0.0f)
			p.vel.x = 0.0f;
		if (inside.y != 0.0f)
			p.vel.y = 0.0f;
		p.pos += inside;
	}
}

Look LookOf(const Frame &f, Group group)
{
	const float a = f.pPlaces[group].att;
	return { 0.86f + 0.3f * a, 0.4f + 0.6f * a, NeoSmoothStep((a - 0.15f) / 0.35f), NeoSmoothStep((a - 0.35f) / 0.35f) };
}

// A slot's centre and half size on screen (pixels): each group round its point at its attention's scale, the ring
// round itself.
void GroupExtent(const Frame &f, int slot, Vector2D &centre, Vector2D &half)
{
	if (slot == BRIGHT_RING)
	{
		centre = f.ringCentre;
		half = f.ringRadii + Vector2D(40.0f, 30.0f) * f.s;
		return;
	}
	const float k = f.s * LookOf(f, static_cast<Group>(slot)).scale, m = static_cast<float>(f.hand);
	Vector2D offset, size;
	switch (slot)
	{
	case GROUP_BODY:
		if (f.style == NEO_HUD_STYLE_BODY)
		{
			offset.Init(0.0f, 30.0f);
			size.Init(125.0f, 80.0f);
		}
		else
		{
			offset.Init(0.0f, 4.0f);
			size.Init(84.0f, 96.0f);
		}
		break;
	// Optics: the halftone patch, its therm-optic frame and the brackets (about 47 either side), centred on it.
	case GROUP_OPTICS:	offset.Init(0.0f, 4.0f); size.Init(58.0f, 58.0f); break;
	case GROUP_WEAPON:	offset.Init(0.0f, 6.0f); size.Init(125.0f, 48.0f); break;
	case GROUP_MOTION:	offset.Init(-12.0f * (f.pPlaces[GROUP_BODY].pos.x >= f.pPlaces[GROUP_MOTION].pos.x ? 1.0f : -1.0f), 0.0f); size.Init(80.0f, 58.0f); break;
	default:			offset.Init(30.0f * m, 10.0f); size.Init(70.0f, 40.0f); break;
	}
	centre = f.pPlaces[slot].pos + offset * k;
	half = size * k;
}
} // namespace NeoCyberbrain
