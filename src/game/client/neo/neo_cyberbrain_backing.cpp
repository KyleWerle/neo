#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "c_neo_player.h"
#include "neo_ironsights.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Readability on bright scenes (the firing range, daylight maps). How bright the scene is behind each group and the
// ring comes from the racer band's sampler (neo_quickinfo_backing.cpp: one ray, the world's light where it lands, the
// view's exposure), one ray a frame round the slots, eased. Behind each: a feathered dark backing, stronger the
// brighter it is (cl_neo_hud_backing); over it, the strokes get a dark outline and text a dark edge as the scene brightens.

extern ConVar cl_neo_hud_backing;

namespace NeoCyberbrain
{
constexpr int SLOTS = GROUP__COUNT + 1, SPOTS = 3;
constexpr float BACK_MIN = 0.1f, BACK_MAX = 0.75f;	// the backing's opacity in the dark, and in daylight (Kyle: more on bright scenes)
constexpr float DIM = 0.2f, BRIGHT = 0.75f;			// the brightness it ramps over
constexpr float EASE = 0.4f;
constexpr float FEATHER = 40.0f;

static struct
{
	float samples[SLOTS][SPOTS] = {};
	float bright[SLOTS] = { 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f };
	int next = 0;
} s_bright;

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

void MeasureBrightness(C_NEO_Player *pPlayer, Frame &f, float dt, bool bBoot)
{
	for (int slot = 0; slot < SLOTS; ++slot)
	{
		const bool bSample = bBoot || s_bright.next % SLOTS == slot;
		if (bSample)
		{
			Vector2D centre, half;
			GroupExtent(f, slot, centre, half);
			for (int spot = 0; spot < SPOTS; ++spot)
			{
				if (bBoot || (s_bright.next / SLOTS) % SPOTS == spot)
				{
					const Vector2D at = centre + Vector2D((spot - 1) * half.x * 0.6f, 0.0f);
					s_bright.samples[slot][spot] = NeoQuickInfo::SceneBrightness(pPlayer, at);
				}
			}
		}
		// The bright spots count most: a patch of sky behind half a group still needs the backing.
		float mean = 0.0f, most = 0.0f;
		for (const float sample : s_bright.samples[slot])
		{
			mean += sample;
			most = Max(most, sample);
		}
		const float target = 0.5f * (mean / SPOTS) + 0.5f * most;
		s_bright.bright[slot] = bBoot ? target : s_bright.bright[slot] + (target - s_bright.bright[slot]) * Min(1.0f, dt / EASE);
		f.bright[slot] = s_bright.bright[slot];
	}
	s_bright.next = (s_bright.next + 1) % (SLOTS * SPOTS);
}

// A rounded rectangle (a superellipse), solid inside, fading out over the feather.
static void Blob(const Frame &f, const Vector2D &centre, const Vector2D &half, float feather, float alpha)
{
	constexpr int RING = 28;
	Vector2D in[RING], out[RING];
	for (int i = 0; i < RING; ++i)
	{
		const float q = 2.0f * M_PI_F * i / RING, c = cosf(q), s = sinf(q);
		const Vector2D shape((c < 0.0f ? -1.0f : 1.0f) * sqrtf(fabsf(c)), (s < 0.0f ? -1.0f : 1.0f) * sqrtf(fabsf(s)));
		in[i].Init(centre.x + shape.x * half.x, centre.y + shape.y * half.y);
		out[i].Init(centre.x + shape.x * (half.x + feather), centre.y + shape.y * (half.y + feather));
	}
	NeoGhostBegin(Color(0, 0, 0, 255), clamp(RoundFloatToInt(255.0f * alpha), 0, 255));
	static const float s_solid[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, s_edge[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	for (int i = 0; i < RING; ++i)
	{
		const int j = (i + 1) % RING;
		const Vector2D fan[4] = { centre, in[i], in[j], in[j] };
		NeoGhostFillShaded(fan, s_solid);
		const Vector2D edge[4] = { in[i], in[j], out[j], out[i] };
		NeoGhostFillShaded(edge, s_edge);
	}
}

void PaintBackings(const Frame &f)
{
	const float strength = cl_neo_hud_backing.GetFloat();
	if (strength <= 0.0f)
	{
		return;
	}
	for (int slot = 0; slot < SLOTS; ++slot)
	{
		// The ring on the body shares the body's backing.
		if (slot == BRIGHT_RING && f.style == NEO_HUD_STYLE_BODY)
		{
			continue;
		}
		if (slot == GROUP_WEAPON && !f.pSenses->ammo.bShown)
		{
			continue;
		}
		Vector2D centre, half;
		GroupExtent(f, slot, centre, half);
		const float att = slot == BRIGHT_RING ? 0.5f : f.pPlaces[slot].att;
		const float alpha = Lerp(NeoSmoothStep((f.bright[slot] - DIM) / (BRIGHT - DIM)), BACK_MIN, BACK_MAX) * strength
			* (0.75f + 0.25f * att) * f.alpha;
		Blob(f, centre, half, FEATHER * f.s, alpha);
	}
}

Frame ForGroup(const Frame &f, int slot)
{
	Frame g = f;
	g.contrast = NeoSmoothStep((f.bright[slot] - 0.3f) / 0.45f);
	NeoGhostOutline(0.75f * g.contrast);
	return g;
}
} // namespace NeoCyberbrain
