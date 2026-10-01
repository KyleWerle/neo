#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_ironsights.h"
#include "neo_hud_spring.h"
#include "neo_gunplay_crosshair.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Attention and placement. Each group's salience (what it senses mattering now: a hit, low integrity, sprinting, a
// reload, cloaking, standing in light) pulls it from its far home toward its near one fast, and it lets go slowly.
// Placement is a critically damped spring (no overshoot, no wobble); groups are locked to the screen while you look
// round (nothing trails the view); a slow balance eases the quieter groups sideways to keep the weight even against
// the gun; nothing enters the keep-out round the crosshair but a critical group (Kyle, 2026-09-30: attention "even
// into the crosshair range"), which comes on into a smaller focus zone, its whole extent kept out of a tight bound
// round the crosshair, and rides the gun's knock there: small kicks all of it, wild ones only so far. Motion comfort
// limits all of it. The priorities are neo_cyberbrain_perceive.cpp's.

ConVar cl_neo_hud_motion("cl_neo_hud_motion", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's motion. 0 = still (groups never move; attention shows as size and strength), 1 = calm (short"
	" travel), 2 = full.", true, 0, true, 2);
ConVar cl_neo_hud_drawn_extent("cl_neo_hud_drawn_extent", "1", FCVAR_NONE,
	"The cyberbrain HUD: 1 = each group's extent (its backing, rulers, edge and focus bounds) is what it drew last"
	" frame, fitting each class; 0 = the fixed sizes.", true, 0, true, 1);
ConVar cl_neo_hud_focus("cl_neo_hud_focus", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD: 1 = a group at a critical (a hit, a reload, the ghost held, a teammate down...) comes in past"
	" the keep-out, close by the crosshair; 0 = every group stops at the keep-out.", true, 0, true, 1);

namespace NeoCyberbrain
{
constexpr float OMEGA = 12.0f;					// the placement spring (critically damped)
constexpr float KEEPOUT_X = 300.0f, KEEPOUT_Y = 220.0f, KEEPOUT_MARGIN = 1.15f;	// pixels at 1080p round the centre
constexpr float GUN_WEIGHT = 1.2f;
constexpr float SCREEN_MARGIN = 6.0f;				// pixels at 1080p every group keeps from the screen's edges
constexpr float GUN_X = 330.0f, GUN_Y = 150.0f;	// where the viewmodel's weight sits, from the gun hand's corner (1080p)
constexpr float BALANCE_GAIN = 0.35f, BALANCE_MAX = 40.0f, BALANCE_EASE = 1.5f;
// The deep layer: a softer spring (a touch under-damped, as the racer band's layers), and the drift as you turn,
// capped at a few pixels (the racer band's sway). Calm: critically damped and half the drift; still: none
// (HUD-REWORK.md's comfort table).
constexpr float DEEP_OMEGA = 7.0f, DEEP_DAMPING = 0.7f;
constexpr float TURN_GAIN = 0.022f, TURN_MAX = 4.0f, DEEP_DEPTH = 1.3f;
// The focus zone: a group's whole extent kept outside this ellipse round the crosshair (pixels at 1080p, about the
// spread at the hip), and the gun's knock it rides, followed in full while small, to at most KICK_MAX.
constexpr float FOCUS_X = 110.0f, FOCUS_Y = 80.0f, KICK_MAX = 14.0f;
constexpr float LAYER_HYSTERESIS = 0.03f;	// how far under a layer's floor its group drops back
constexpr float DRAWN_PAD = 3.0f;		// pixels at 1080p round what a group drew, for its extent
constexpr float FOCUS_GAP = 6.0f;		// pixels at 1080p groups keep between their extents (in focus, and stepping off)
constexpr float ORBIT_STEP = 10.0f, ORBIT_MOST = 90.0f;	// degrees a group in focus swings its approach, a step and at most

static struct { Vector2D offset, vel; } s_ringDeep;
// Moves `at` toward `goal` on the deep spring.
static void DeepSpring(Vector2D &at, Vector2D &vel, const Vector2D &goal, float dt)
{
	NeoHudSpring(at, vel, goal, DEEP_OMEGA, cl_neo_hud_motion.GetInt() >= 2 ? DEEP_DAMPING : 1.0f, dt);
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

struct Comfort { float travel, pullIn, letGo, focus; };
static Comfort ComfortOf()
{
	// Pulling in fast; letting go slower and eased (Kyle), the seconds it takes to settle.
	static const Comfort s_levels[] = { { 0.0f, 0.3f, 2.8f, 0.0f }, { 0.6f, 0.3f, 2.8f, 0.6f }, { 1.0f, 0.22f, 2.2f, 1.0f } };
	return s_levels[clamp(cl_neo_hud_motion.GetInt(), 0, 2)];
}
// Whether a group's box (centre, half) keeps clear of the focus bound round the crosshair.
static bool ClearOfFocus(const Frame &f, const Vector2D &centre, const Vector2D &half)
{
	const float nx = clamp(f.centre.x, centre.x - half.x, centre.x + half.x) - f.centre.x;
	const float ny = clamp(f.centre.y, centre.y - half.y, centre.y + half.y) - f.centre.y;
	return Square(nx / (FOCUS_X * f.s)) + Square(ny / (FOCUS_Y * f.s)) >= 1.0f;
}

// How far from the crosshair along `dir` a group's box, offset from its point by `offset`, first keeps clear of the
// focus bound (searched up to `most`).
static float FocusReach(const Frame &f, const Vector2D &dir, const Vector2D &offset, const Vector2D &half, float most)
{
	float lo = 0.0f, hi = most;
	for (int i = 0; i < 12; ++i)
	{
		const float mid = 0.5f * (lo + hi);
		if (ClearOfFocus(f, f.centre + dir * mid + offset, half))
			hi = mid;
		else
			lo = mid;
	}
	return hi;
}

// Whether two boxes (centre, half) come within `gap` of each other.
static bool Meet(const Vector2D &ca, const Vector2D &ha, const Vector2D &cb, const Vector2D &hb, float gap)
{
	return fabsf(ca.x - cb.x) < ha.x + hb.x + gap && fabsf(ca.y - cb.y) < ha.y + hb.y + gap;
}

// The way each group in focus comes in (from the crosshair, a unit vector). Several criticals at once share the zone:
// the most attended comes straight in from its near home's side; each after it swings its approach round the
// crosshair, a step either way at a time (up to a quarter turn), until its extent, at the bound, keeps clear of those
// already in and inside the screen.
static void FocusApproaches(const Frame &f, const Vector2D nearer[GROUP__COUNT], const float focus[GROUP__COUNT],
	const Place places[GROUP__COUNT], Vector2D dirs[GROUP__COUNT])
{
	int order[GROUP__COUNT], count = 0;
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		dirs[g] = nearer[g] - f.centre;
		dirs[g].NormalizeInPlace();
		if (focus[g] > 0.0f)
			order[count++] = g;
	}
	for (int i = 1; i < count; ++i)	// most attended first
	{
		for (int j = i; j > 0 && places[order[j]].att > places[order[j - 1]].att; --j)
			V_swap(order[j], order[j - 1]);
	}
	const float gap = FOCUS_GAP * f.s;
	Vector2D takenCentre[GROUP__COUNT], takenHalf[GROUP__COUNT];
	for (int n = 0; n < count; ++n)
	{
		const int g = order[n];
		Vector2D centre, half;
		GroupExtent(f, g, centre, half);
		const Vector2D offset = centre - places[g].pos;
		const float most = (nearer[g] - f.centre).Length(), base = atan2f(dirs[g].y, dirs[g].x);
		Vector2D chosen = dirs[g], chosenCentre = f.centre + dirs[g] * FocusReach(f, dirs[g], offset, half, most) + offset;
		bool bFound = false;
		for (int step = 0; step * ORBIT_STEP <= ORBIT_MOST && !bFound; ++step)
		{
			for (int side = 0; side < (step == 0 ? 1 : 2) && !bFound; ++side)
			{
				const float angle = base + DEG2RAD((side == 0 ? 1.0f : -1.0f) * step * ORBIT_STEP);
				const Vector2D dir(cosf(angle), sinf(angle));
				const Vector2D c = f.centre + dir * FocusReach(f, dir, offset, half, most) + offset;
				bool bClear = Inside(f, c, half).IsZero(0.5f);
				for (int m = 0; m < n && bClear; ++m)
					bClear = !Meet(c, half, takenCentre[m], takenHalf[m], gap);
				if (bClear)
				{
					chosen = dir;
					chosenCentre = c;
					bFound = true;
				}
			}
		}
		dirs[g] = chosen;
		takenCentre[n] = chosenCentre;
		takenHalf[n] = half;
	}
}

// Groups in focus keep off each other (two criticals at once both come in by the crosshair): where two boxes overlap
// and either is in focus, the one less in focus steps out along the shallower way, its speed that way dropped, and
// stays out of the focus bound and inside the screen.
static void SeparateFocused(const Frame &f, Place places[GROUP__COUNT])
{
	const float gap = FOCUS_GAP * f.s;
	for (int pass = 0; pass < 2; ++pass)
	{
		for (int a = 0; a < GROUP__COUNT; ++a)
		{
			for (int b = a + 1; b < GROUP__COUNT; ++b)
			{
				if (places[a].focus <= 0.0f && places[b].focus <= 0.0f)
					continue;
				Vector2D ca, ha, cb, hb;
				GroupExtent(f, a, ca, ha);
				GroupExtent(f, b, cb, hb);
				const float ox = ha.x + hb.x + gap - fabsf(ca.x - cb.x), oy = ha.y + hb.y + gap - fabsf(ca.y - cb.y);
				if (ox <= 0.0f || oy <= 0.0f)
					continue;
				const bool bMoveA = places[a].focus != places[b].focus ? places[a].focus < places[b].focus : places[a].att < places[b].att;
				const int mover = bMoveA ? a : b;
				Place &m = places[mover];
				const Vector2D away = bMoveA ? ca - cb : cb - ca;
				if (ox < oy)
				{
					m.pos.x += away.x < 0.0f ? -ox : ox;
					m.vel.x = 0.0f;
				}
				else
				{
					m.pos.y += away.y < 0.0f ? -oy : oy;
					m.vel.y = 0.0f;
				}
				Vector2D centre, half;
				GroupExtent(f, mover, centre, half);
				Vector2D dir = m.pos - f.centre;
				const float at = dir.NormalizeInPlace();
				if (at > 0.001f && !ClearOfFocus(f, centre, half))
				{
					m.pos = f.centre + dir * FocusReach(f, dir, centre - m.pos, half, at + 400.0f * f.s);
					GroupExtent(f, mover, centre, half);
				}
				m.pos += Inside(f, centre, half);
			}
		}
	}
}

// R5: every group steps off, not only those in focus. Where a pulled-in group's box, at its target, comes within the
// gap of a less attended neighbour's, the neighbour's target steps out the shallower way, so it eases off on the
// placement spring rather than jumping. Most attended first, so a group pushed while pulled in passes it on. Groups at
// rest never push (their homes are clear at near size), groups in focus keep their own pass (SeparateFocused), and only
// what's been measured counts (the link draws nothing). Each box is the group's own: the body's without the ring's
// disc, which doesn't move.
static void StepOff(const Frame &f, const Place places[GROUP__COUNT], Vector2D targets[GROUP__COUNT])
{
	const float gap = FOCUS_GAP * f.s;
	const auto box = [&](int g, Vector2D &centre, Vector2D &half)
	{
		centre = targets[g] + places[g].drawnCentre;
		half = places[g].drawnHalf + Vector2D(DRAWN_PAD, DRAWN_PAD) * f.s;
	};
	int order[GROUP__COUNT], count = 0;
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		if (places[g].bDrawn && places[g].focus <= 0.0f)
			order[count++] = g;
	}
	for (int i = 1; i < count; ++i)
	{
		for (int j = i; j > 0 && places[order[j]].att > places[order[j - 1]].att; --j)
			V_swap(order[j], order[j - 1]);
	}
	for (int i = 0; i < count; ++i)
	{
		const int a = order[i];
		if (places[a].layer == LAYER_AMBIENT)
			continue;
		for (int j = i + 1; j < count; ++j)
		{
			const int b = order[j];
			Vector2D ca, ha, cb, hb;
			box(a, ca, ha);
			box(b, cb, hb);
			const float ox = ha.x + hb.x + gap - fabsf(ca.x - cb.x), oy = ha.y + hb.y + gap - fabsf(ca.y - cb.y);
			if (ox <= 0.0f || oy <= 0.0f)
				continue;
			if (ox < oy)
				targets[b].x += cb.x < ca.x ? -ox : ox;
			else
				targets[b].y += cb.y < ca.y ? -oy : oy;
		}
	}
}

void Attend(const Senses &senses, const Home homes[GROUP__COUNT], const Frame &f, float dt, bool bBoot, Place places[GROUP__COUNT])
{
	const Comfort comfort = ComfortOf();
	Listen(senses, f.now, dt, bBoot);
	const auto mirror = [&](const Vector2D &p) { return f.hand > 0 ? p : Vector2D(f.wide - p.x, p.y); };
	// Attention: in fast, out slow.
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		Place &p = places[g];
		p.sal = clamp(Perceive(senses, static_cast<Group>(g), f.now), 0.0f, 1.0f);
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
		// Its layer, by its attention: up on reaching a layer's floor, down a little under it (no flicker at the line).
		int layer = p.layer;
		while (layer < LAYER_CRITICAL && p.att >= LAYER_FLOOR[layer + 1])
			++layer;
		while (layer > LAYER_AMBIENT && p.att < LAYER_FLOOR[layer] - LAYER_HYSTERESIS)
			--layer;
		if (bBoot)
			layer = LAYER_AMBIENT;
		if (layer != p.layer)
		{
			p.lastLayer = p.layer;
			p.layer = layer;
			p.layerChanged = bBoot ? -100.0f : f.now;
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
		? Vector2D(clamp(senses.yawRate * TURN_GAIN, -TURN_MAX, TURN_MAX), clamp(-senses.pitchRate * TURN_GAIN, -TURN_MAX, TURN_MAX))
			* (f.s * DEEP_DEPTH * (comfort.travel >= 1.0f ? 1.0f : 0.5f))
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
	// How far each is in focus, and the way each comes in.
	Vector2D nearHomes[GROUP__COUNT], approach[GROUP__COUNT];
	float focusGoals[GROUP__COUNT];
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		nearHomes[g] = homes[g].bLeft ? homes[g].nearer : mirror(homes[g].nearer);
		focusGoals[g] = cl_neo_hud_focus.GetBool() && !bBoot
			? clamp((places[g].att - FOCUS_FROM) / (1.0f - FOCUS_FROM), 0.0f, 1.0f) * comfort.focus : 0.0f;
	}
	FocusApproaches(f, nearHomes, focusGoals, places, approach);
	Vector2D targets[GROUP__COUNT];
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		Place &p = places[g];
		const Vector2D far = homes[g].bLeft ? homes[g].far : mirror(homes[g].far);
		const Vector2D nearer = nearHomes[g];
		p.balance = bBoot ? 0.0f : p.balance + (shift * (1.0f - p.att) * comfort.travel - p.balance) * Min(1.0f, dt / BALANCE_EASE);
		Vector2D target = far + (nearer - far) * (Min(1.0f, p.att / FOCUS_FROM) * comfort.travel) + Vector2D(p.balance, 0.0f);
		// A critical: on in toward the crosshair, from the side its near home is on (swung round if others are in
		// already), until its extent meets the focus bound; there it rides the gun's knock, followed straight (not
		// through the spring).
		p.focus = focusGoals[g];
		Vector2D kick(0.0f, 0.0f);
		if (p.focus > 0.0f)
		{
			Vector2D centre, half;
			GroupExtent(f, g, centre, half);
			const Vector2D dir = approach[g];
			const float reach = FocusReach(f, dir, centre - p.pos, half, (nearer - f.centre).Length());
			target += (f.centre + dir * reach - target) * p.focus;
			const Vector2D knock = NeoGunplayGunKnock(f.wide);
			const float length = knock.Length(), limit = KICK_MAX * f.s;
			if (length > 0.001f)
				kick = knock * (limit * tanhf(length / limit) / length * p.focus);
		}
		target += kick;
		if (!bBoot && p.bPlaced)
			p.pos += kick - p.kick;
		p.kick = kick;
		// Never past the screen's edges, whatever its size or shape: the group's extent (at its scale now, moved to the
		// target) held inside, so the spring eases up to the edge rather than being stopped at it.
		Vector2D centre, half;
		GroupExtent(f, g, centre, half);
		targets[g] = target + Inside(f, centre + (target - p.pos), half);
	}
	// Still: nothing changes place but data motion, so nothing steps off (the homes are clear at near size).
	if (!bBoot && comfort.travel > 0.0f)
		StepOff(f, places, targets);
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		Place &p = places[g];
		Vector2D target = targets[g];
		// A step-off can push it back over an edge: held inside again.
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
		// Never inside the keep-out round the crosshair (a group in focus: the keep-out shrinking away as it comes in),
		// and never over the focus bound at all.
		const float margin = KEEPOUT_MARGIN * (1.0f - p.focus);
		const Vector2D e((p.pos.x - f.centre.x) / (KEEPOUT_X * f.s), (p.pos.y - f.centre.y) / (KEEPOUT_Y * f.s));
		const float d = e.Length();
		if (d < margin && d > 0.001f)
		{
			p.pos.x = f.centre.x + e.x / d * margin * KEEPOUT_X * f.s;
			p.pos.y = f.centre.y + e.y / d * margin * KEEPOUT_Y * f.s;
		}
		{
			Vector2D centre, half;
			GroupExtent(f, g, centre, half);
			Vector2D dir = p.pos - f.centre;
			const float at = dir.NormalizeInPlace();
			if (at > 0.001f && !ClearOfFocus(f, centre, half))
				p.pos = f.centre + dir * FocusReach(f, dir, centre - p.pos, half, at + 400.0f * f.s);
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
	SeparateFocused(f, places);
}

Look LookOf(const Frame &f, Group group)
{
	const float a = f.pPlaces[group].att;
	// In focus a touch smaller again, to keep clear of what you're aiming at.
	return { 0.86f + 0.3f * a - 0.14f * f.pPlaces[group].focus, 0.4f + 0.6f * a, NeoSmoothStep((a - 0.15f) / 0.35f), NeoSmoothStep((a - 0.35f) / 0.35f) };
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
	// What it drew last frame, where it's been measured (each class's parts differ, and change).
	const Place &p = f.pPlaces[slot];
	if (p.bDrawn && cl_neo_hud_drawn_extent.GetBool())
	{
		centre = p.pos + p.drawnCentre;
		half = p.drawnHalf + Vector2D(DRAWN_PAD, DRAWN_PAD) * f.s;
		// On the body the ring's disc is the body's too (it shares the body's backing), drawn after it.
		if (slot == GROUP_BODY && f.style == NEO_HUD_STYLE_BODY && f.ringRadii.x > 0.0f)
		{
			const Vector2D lo(Min(centre.x - half.x, f.ringCentre.x - f.ringRadii.x), Min(centre.y - half.y, f.ringCentre.y - f.ringRadii.y));
			const Vector2D hi(Max(centre.x + half.x, f.ringCentre.x + f.ringRadii.x), Max(centre.y + half.y, f.ringCentre.y + f.ringRadii.y));
			centre = (lo + hi) * 0.5f;
			half = (hi - lo) * 0.5f;
		}
		return;
	}
	// Not yet: its usual size.
	const float k = f.s * LookOf(f, static_cast<Group>(slot)).scale;
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
	// The link draws nothing (the party view is in the squad list): no extent to keep clear.
	default:			offset.Init(0.0f, 0.0f); size.Init(0.0f, 0.0f); break;
	}
	centre = f.pPlaces[slot].pos + offset * k;
	half = size * k;
}
} // namespace NeoCyberbrain
