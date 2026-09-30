#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The perception layers made visible (Kyle, 2026-09-30: "there should also be graphical element shifts as perceptual
// layer changes"): marks round each group's extent that build up layer by layer. Ambient: none. Notable: short
// corner ticks. Urgent: longer brackets, a rail along the top and the layer's code. Critical: heavy brackets doubled
// inside, the code in the critical colour, pulsing its first moments. Rising a layer, the brackets snap in from
// further out and a scan runs down the group; falling one, the old layer's marks drift out and fade.

namespace NeoCyberbrain
{
constexpr float SNAP_FOR = 0.18f;		// seconds the brackets take to snap in on a rise
constexpr float SCAN_FOR = 0.25f;		// seconds the scan takes down the group
constexpr float FALL_FOR = 0.35f;		// seconds the old marks take to drift out and go
constexpr float PULSE_FOR = 1.5f;		// seconds a new critical pulses
constexpr float PAD = 5.0f;				// pixels at 1080p between the extent and its marks

// One layer's marks round the box (a top left, b bottom right), `out` pixels further out than their place.
static void Marks(const Frame &f, int layer, const Vector2D &a, const Vector2D &b, float out, float alpha)
{
	if (layer <= LAYER_AMBIENT || alpha <= 0.01f)
		return;
	const float s = f.s;
	const Vector2D p0 = a - Vector2D(out, out), p1 = b + Vector2D(out, out);
	const float arm = (layer == LAYER_NOTABLE ? 6.0f : layer == LAYER_URGENT ? 12.0f : 16.0f) * s;
	const NeoGhostWeight w = layer == LAYER_NOTABLE ? NEO_GHOST_LIGHT : layer == LAYER_URGENT ? NEO_GHOST_MEDIUM : NEO_GHOST_HEAVY;
	const float strength = layer == LAYER_NOTABLE ? 0.35f : layer == LAYER_URGENT ? 0.6f : 0.85f;
	const Vector2D corners[4] = { p0, Vector2D(p1.x, p0.y), p1, Vector2D(p0.x, p1.y) };
	for (int i = 0; i < 4; ++i)
	{
		const Vector2D c = corners[i];
		const float dx = (i == 0 || i == 3) ? 1.0f : -1.0f, dy = (i < 2) ? 1.0f : -1.0f;
		Line(f, c, c + Vector2D(dx * arm, 0.0f), w, f.color, strength * alpha);
		Line(f, c, c + Vector2D(0.0f, dy * arm), w, f.color, strength * alpha);
		if (layer >= LAYER_CRITICAL)
		{
			// Doubled inside.
			const Vector2D in = c + Vector2D(dx, dy) * (3.0f * s);
			Line(f, in, in + Vector2D(dx * arm * 0.5f, 0.0f), NEO_GHOST_LIGHT, f.color, 0.6f * alpha);
			Line(f, in, in + Vector2D(0.0f, dy * arm * 0.5f), NEO_GHOST_LIGHT, f.color, 0.6f * alpha);
		}
	}
	if (layer >= LAYER_URGENT)
	{
		// The rail along the top, between the brackets, and the layer's code over its outer end.
		for (float x = p0.x + arm + 4.0f * s; x < p1.x - arm - 4.0f * s; x += 5.0f * s)
			Line(f, Vector2D(x, p0.y), Vector2D(Min(x + 2.0f * s, p1.x - arm - 4.0f * s), p0.y), NEO_GHOST_LIGHT, f.color, 0.3f * alpha);
		const bool bRight = f.hand < 0;	// the side away from the gun
		Text(f, layer >= LAYER_CRITICAL ? L"P3" : L"P2", bRight ? p1.x : p0.x, p0.y - 7.0f * s, bRight ? -1 : 1, FONT_LABEL,
			layer >= LAYER_CRITICAL ? CRIT : f.color, 0.85f * alpha);
	}
}

void PaintLayer(const Frame &f, Group group)
{
	const Place &p = f.pPlaces[group];
	if (group == GROUP_WEAPON && !f.pSenses->ammo.bShown && !f.pSenses->bGhost)
		return;
	Vector2D centre, half;
	GroupExtent(f, group, centre, half);
	const Vector2D pad(PAD * f.s, PAD * f.s);
	const Vector2D a = centre - half - pad, b = centre + half + pad;
	const float age = f.now - p.layerChanged, alpha = f.alpha;
	const bool bRose = p.layer > p.lastLayer;
	if (bRose)
	{
		const float snap = NeoSmoothStep(age / SNAP_FOR);
		float strength = 1.0f;
		if (p.layer >= LAYER_CRITICAL && age < PULSE_FOR)
			strength = 0.75f + 0.25f * cosf(age * 2.0f * M_PI_F * 2.0f);
		Marks(f, p.layer, a, b, (1.0f - snap) * 18.0f * f.s, (0.4f + 0.6f * snap) * strength * alpha);
		if (age < SCAN_FOR)
		{
			const float y = a.y + (b.y - a.y) * (age / SCAN_FOR);
			Line(f, Vector2D(a.x, y), Vector2D(b.x, y), NEO_GHOST_LIGHT, f.color, 0.6f * (1.0f - age / SCAN_FOR) * alpha);
		}
		return;
	}
	Marks(f, p.layer, a, b, 0.0f, alpha);
	if (age < FALL_FOR)
	{
		const float t = age / FALL_FOR;
		Marks(f, p.lastLayer, a, b, t * 12.0f * f.s, (1.0f - t) * alpha);
	}
}
} // namespace NeoCyberbrain
