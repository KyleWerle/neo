#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Perception: each group's priority in layers (Kyle, 2026-09-30: "each node has a multi layered state where some
// effects are really high priority and other stats in them maybe add up to a total perception priority"). Every
// signal a group senses sits in one of four layers, ambient, notable, urgent and critical, each a band of the
// priority (0 to 0.25, to 0.55, to FOCUS_FROM, to 1). Signals in one layer add up within its band (each fills part of
// what's left, so it never overflows it); a full layer below spills a share in, so a pile of small things lifts a
// group within its band but never into the next (a full band stops a little short of it, so one strong notable
// signal, sprinting, doesn't read as urgent). A layer takes hold as its strongest signal passes a fifth, so a pulse
// fading out eases the group down through the bands rather than dropping it. Critical is what brings a group into
// the focus zone by the crosshair (neo_cyberbrain_attention.cpp).

namespace NeoCyberbrain
{
static const float s_floor[LAYER__COUNT + 1] = { 0.0f, LAYER_FLOOR[LAYER_NOTABLE], LAYER_FLOOR[LAYER_URGENT],
	LAYER_FLOOR[LAYER_CRITICAL], 1.0f };
constexpr float SPILL = 0.35f;		// how much of a full layer below fills this one
constexpr float HOLD = 0.2f;		// a layer's strongest signal at this takes its band fully
constexpr float HEADROOM = 0.05f;	// a full layer tops out this far under the next one's floor (sprinting alone was P2)

class Perception
{
public:
	void Add(Layer layer, float strength)
	{
		strength = clamp(strength, 0.0f, 1.0f);
		m_rest[layer] *= 1.0f - strength;
		m_peak[layer] = Max(m_peak[layer], strength);
	}
	float Priority() const
	{
		float p = 0.0f;
		for (int k = 0; k < LAYER__COUNT; ++k)
		{
			if (m_peak[k] <= 0.0f)
				continue;
			const float below = k > 0 ? p / s_floor[k] : 0.0f;
			const float fill = 1.0f - m_rest[k] * (1.0f - SPILL * below);
			const float top = k + 1 < LAYER__COUNT ? s_floor[k + 1] - HEADROOM : 1.0f;
			const float band = s_floor[k] + (top - s_floor[k]) * fill;
			p = Max(p, p + (band - p) * Min(1.0f, m_peak[k] / HOLD));
		}
		return p;
	}
private:
	float m_rest[LAYER__COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float m_peak[LAYER__COUNT] = {};
};

// Something that happened at `when`, fading from its peak.
static float Pulse(float now, float when, float peak)
{
	const float age = now - when;
	return (age >= 0.0f && age < 6.0f) ? peak * expf(-age / 1.1f) : 0.0f;
}

float Perceive(const Senses &s, Group group, float now)
{
	Perception p;
	switch (group)
	{
	case GROUP_BODY:
		p.Add(LAYER_CRITICAL, Pulse(now, s.hitTime, 1.0f));
		p.Add(LAYER_CRITICAL, (0.25f - s.hp) / 0.15f);
		p.Add(LAYER_URGENT, (0.5f - s.hp) / 0.25f);
		p.Add(LAYER_NOTABLE, Pulse(now, s.landTime, 0.8f));
		p.Add(LAYER_AMBIENT, 0.6f * s.crouch);
		p.Add(LAYER_AMBIENT, 0.6f * fabsf(s.lean));
		break;
	case GROUP_MOTION:
		p.Add(LAYER_URGENT, s.speed > s.runSpeed * 1.2f ? 1.0f : 0.0f);
		p.Add(LAYER_URGENT, (s.bHasSprint || s.bHasJumps) && s.aux < 10.0f ? 0.8f : 0.0f);	// spent
		p.Add(LAYER_NOTABLE, s.bSprinting ? 1.0f : 0.0f);
		p.Add(LAYER_NOTABLE, (s.bHasSprint && s.aux < 99.5f) || (s.bHasJumps && s.aux < 90.0f) ? 0.6f : 0.0f);
		p.Add(LAYER_NOTABLE, 0.8f * s.air);
		p.Add(LAYER_NOTABLE, Pulse(now, s.landTime, 1.0f));
		p.Add(LAYER_AMBIENT, s.bMoving ? 0.6f : 0.0f);
		break;
	case GROUP_OPTICS:
		// Cloaking pulls it in, never into the focus zone (Kyle, 2026-10-01: "the cloak was a bit too in my face");
		// staying cloaked is notable, not urgent.
		p.Add(LAYER_URGENT, Pulse(now, s.cloakChanged, 1.0f));
		p.Add(LAYER_URGENT, Pulse(now, s.visionChanged, 1.0f));
		p.Add(LAYER_NOTABLE, s.bCloaked ? 0.5f : 0.0f);
		p.Add(LAYER_NOTABLE, Pulse(now, s.lightChanged, 0.8f));
		p.Add(LAYER_NOTABLE, s.bVision ? 0.6f : 0.0f);
		p.Add(LAYER_NOTABLE, s.bExposed ? 0.8f : 0.0f);
		p.Add(LAYER_AMBIENT, s.bHasCloak && s.cloak < 0.98f ? 0.5f : 0.0f);
		break;
	case GROUP_WEAPON:
		// Holding the ghost is always critical (Kyle: it turns into your weapon when it's equipped); booting, or
		// seeing someone, fills it further.
		if (s.bGhost)
		{
			p.Add(LAYER_CRITICAL, 0.6f);
			p.Add(LAYER_CRITICAL, s.bGhostWorking && s.ghostBoot < 1.0f ? 0.8f : 0.0f);
			p.Add(LAYER_CRITICAL, s.ghostContacts > 0 ? 0.7f : 0.0f);
			break;
		}
		p.Add(LAYER_CRITICAL, s.bReloading ? 0.8f : 0.0f);
		p.Add(LAYER_CRITICAL, s.heatLevel >= 2 ? 1.0f : 0.0f);
		p.Add(LAYER_CRITICAL, s.ammo.bShown && s.ammo.rounds == 0 ? 1.0f : 0.0f);
		p.Add(LAYER_URGENT, s.bAmmoLow ? 0.8f : 0.0f);
		p.Add(LAYER_URGENT, s.heatLevel == 1 ? 0.7f : 0.0f);
		p.Add(LAYER_NOTABLE, now - s.shotTime < 1.0f ? 0.8f : 0.0f);
		p.Add(LAYER_NOTABLE, Pulse(now, s.ammoChanged, 0.8f));
		break;
	default:
		// The link draws nothing (the party view carries a mate's death in the squad list): no attention.
		break;
	}
	return p.Priority();
}

// The action round you (Kyle, 2026-09-30: "the more action around you, you focus less on noise. when its quiet you
// really focus on the noise you and your surroundings are making"; the compass the same): being hit, firing, shots
// and blasts heard from the enemy, the ghost showing someone, sprinting, reloading, each filling part of what's left.
// Steps and the quieter sounds don't count: those are what a quiet moment listens for.
float Action(const Senses &s, float now)
{
	float calm = 1.0f;
	const auto add = [&calm](float x) { calm *= 1.0f - clamp(x, 0.0f, 1.0f); };
	add(Pulse(now, s.hitTime, 1.0f));
	add(now - s.shotTime < 1.5f ? 0.9f : 0.0f);
	for (int i = 0; i < s.heardCount; ++i)
	{
		const Heard &h = s.heard[i];
		if (!h.bFriendly && (h.kind == SOUND_GUNFIRE || h.kind == SOUND_BLAST))
			add((0.5f + 0.5f * h.loud) * (1.0f - (now - h.time) / 2.0f));
	}
	add(s.ghostContacts > 0 ? 0.5f : 0.0f);
	add(s.bSprinting ? 0.35f : 0.0f);
	add(s.bReloading ? 0.3f : 0.0f);
	return 1.0f - calm;
}
// Listening, a mode of its own (Kyle: "wasn't really noticing a defined listening mode"): a second of quiet after the
// last action, then it settles in over two; action pulls it straight back. In from 0.8, out under 0.6.
constexpr float LISTEN_AFTER = 1.0f, LISTEN_SETTLE = 2.0f;	// seconds
constexpr float LISTEN_RISE = 0.5f, LISTEN_FALL = 0.15f;	// seconds the level eases over, up and down
constexpr float LISTEN_ENTER = 0.8f, LISTEN_LEAVE = 0.6f;
static struct
{
	float level = 1.0f;
	float lastAction = -100.0f;
	float since = -100.0f;	// when the mode began
	bool bIn = false;
} s_listen;

float Listening()
{
	return s_listen.level;
}

float ListeningFor(float now)
{
	return s_listen.bIn ? now - s_listen.since : -1.0f;
}

void Listen(const Senses &senses, float now, float dt, bool bBoot)
{
	const float action = Action(senses, now);
	if (bBoot)
		s_listen.lastAction = now - 100.0f;
	else if (action > 0.2f)
		s_listen.lastAction = now;
	const float goal = (1.0f - action) * NeoSmoothStep((now - s_listen.lastAction - LISTEN_AFTER) / LISTEN_SETTLE);
	s_listen.level = bBoot ? goal
		: s_listen.level + (goal - s_listen.level) * Min(1.0f, dt / (goal > s_listen.level ? LISTEN_RISE : LISTEN_FALL));
	if (!s_listen.bIn && s_listen.level >= LISTEN_ENTER)
	{
		s_listen.bIn = true;
		s_listen.since = bBoot ? now - 100.0f : now;	// on a boot straight in, without the ping
	}
	else if (s_listen.bIn && s_listen.level < LISTEN_LEAVE)
	{
		s_listen.bIn = false;
	}
}

} // namespace NeoCyberbrain
