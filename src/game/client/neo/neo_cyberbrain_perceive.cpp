#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Perception: each group's priority in layers (Kyle, 2026-09-30: "each node has a multi layered state where some
// effects are really high priority and other stats in them maybe add up to a total perception priority"). Every
// signal a group senses sits in one of four layers, ambient, notable, urgent and critical, each a band of the
// priority (0 to 0.25, to 0.55, to FOCUS_FROM, to 1). Signals in one layer add up within its band (each fills part of
// what's left, so it never overflows it); a full layer below spills a share in, so a pile of small things lifts a
// group within its band but never into the next. A layer takes hold as its strongest signal passes a fifth, so a
// pulse fading out eases the group down through the bands rather than dropping it. Critical is what brings a group
// into the focus zone by the crosshair (neo_cyberbrain_attention.cpp).

namespace NeoCyberbrain
{
enum Layer { LAYER_AMBIENT, LAYER_NOTABLE, LAYER_URGENT, LAYER_CRITICAL, LAYER__COUNT };
static const float s_floor[LAYER__COUNT + 1] = { 0.0f, 0.25f, 0.55f, FOCUS_FROM, 1.0f };
constexpr float SPILL = 0.35f;		// how much of a full layer below fills this one
constexpr float HOLD = 0.2f;		// a layer's strongest signal at this takes its band fully

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
			const float band = s_floor[k] + (s_floor[k + 1] - s_floor[k]) * fill;
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
		p.Add(LAYER_CRITICAL, Pulse(now, s.cloakChanged, 1.0f));
		p.Add(LAYER_URGENT, Pulse(now, s.visionChanged, 1.0f));
		p.Add(LAYER_URGENT, s.bCloaked ? 0.5f : 0.0f);
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
		// The link: critical when a teammate dies (Kyle); the last one standing, notable; otherwise it idles.
		p.Add(LAYER_CRITICAL, Pulse(now, s.mateDiedTime, 1.0f));
		p.Add(LAYER_NOTABLE, s.squadTotal > 0 && s.squadAlive == 0 ? 0.8f : 0.0f);
		p.Add(LAYER_AMBIENT, 0.2f);
		break;
	}
	return p.Priority();
}
} // namespace NeoCyberbrain
