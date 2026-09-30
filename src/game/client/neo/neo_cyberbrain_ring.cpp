#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The surround ring: the compass as a ring on the ground round you, in perspective. Ahead is its far (top) edge,
// behind its near (bottom) edge; it opens toward a full circle as you look down, and turns with you as the world does.
// On it: the heading, the objective, squadmates, every sound you can hear (the full circle), and your own noise as
// short ticks pushed out from it (short and local: nothing flows across the view). Compact: small at the bottom
// centre. On the body: it is the body group's ground disc, so sounds land round your own feet. Glyphs, not numbers: a
// sound's kind is a mark beside its arc, your noise state an icon, the objective's distance a line's length.
// Headings follow NT's compass (neo_hud_compass.cpp): a world yaw Y reads as 180 - Y degrees, north up.

namespace NeoCyberbrain
{
static float Heading(float worldYaw) { return AngleNormalizePositive(180.0f - worldYaw); }

// A sound's kind as a small mark at p: steps two footfalls, gunfire a burst, a reload a bracket, a landing a down
// chevron, a blast a big burst.
static void KindGlyph(const Frame &f, const Vector2D &p, SoundKind kind, const Color &c, float a)
{
	const float k = f.s;
	switch (kind)
	{
	case SOUND_STEP:
		Rect(f, p + Vector2D(-4, -3) * k, p + Vector2D(-1, 1) * k, c, a);
		Rect(f, p + Vector2D(1, -1) * k, p + Vector2D(4, 3) * k, c, a);
		break;
	case SOUND_GUNFIRE:
	case SOUND_BLAST:
	{
		const float r = (kind == SOUND_BLAST ? 8.0f : 5.0f) * k;
		for (int i = 0; i < 4; ++i)
		{
			const float q = i * M_PI_F / 4.0f;
			Line(f, p - Vector2D(cosf(q), sinf(q)) * r, p + Vector2D(cosf(q), sinf(q)) * r, NEO_GHOST_LIGHT, c, a);
		}
		break;
	}
	case SOUND_RELOAD:
		Line(f, p + Vector2D(-3, -4) * k, p + Vector2D(-3, 4) * k, NEO_GHOST_LIGHT, c, a);
		Line(f, p + Vector2D(-3, -4) * k, p + Vector2D(2, -4) * k, NEO_GHOST_LIGHT, c, a);
		Line(f, p + Vector2D(-3, 4) * k, p + Vector2D(2, 4) * k, NEO_GHOST_LIGHT, c, a);
		break;
	case SOUND_LAND:
		Line(f, p + Vector2D(-4, -2) * k, p + Vector2D(0, 2) * k, NEO_GHOST_MEDIUM, c, a);
		Line(f, p + Vector2D(0, 2) * k, p + Vector2D(4, -2) * k, NEO_GHOST_MEDIUM, c, a);
		break;
	default:
		break;
	}
}

// Your noise state as an icon: a dot and 0 to 3 arcs (silent struck through, loud in amber).
static void NoiseIcon(const Frame &f, const Vector2D &p, int arcs, bool bSilent, const Color &c, float a)
{
	Rect(f, p + Vector2D(-2, -2) * f.s, p + Vector2D(2, 2) * f.s, c, a);
	for (int i = 1; i <= arcs; ++i)
	{
		Arc(f, p, Vector2D(5.0f + i * 5.0f, 5.0f + i * 5.0f) * f.s, 45.0f, 135.0f, NEO_GHOST_MEDIUM, c, a);
		Arc(f, p, Vector2D(5.0f + i * 5.0f, 5.0f + i * 5.0f) * f.s, 225.0f, 315.0f, NEO_GHOST_MEDIUM, c, a);
	}
	if (bSilent)
	{
		Arc(f, p, Vector2D(9.0f, 9.0f) * f.s, 0.0f, 360.0f, NEO_GHOST_LIGHT, c, a);
		Line(f, p + Vector2D(-7, 7) * f.s, p + Vector2D(7, -7) * f.s, NEO_GHOST_LIGHT, c, a);
	}
}

struct Ring
{
	const Frame *pF;
	Vector2D c, r;
	float outScale;
	// A point at a bearing (degrees from ahead, clockwise), pushed out by `out` pixels at 1080p.
	Vector2D At(float bearing, float out = 0.0f) const
	{
		const float q = DEG2RAD(bearing), o = out * outScale * pF->s;
		return Vector2D(c.x + (r.x + o) * sinf(q), c.y - (r.y + o * r.y / Max(r.x, 1.0f) + o * 0.25f) * cosf(q));
	}
	static float Front(float bearing) { return 0.5f + 0.5f * cosf(DEG2RAD(bearing)); }
};

void PaintRing(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const bool bBody = f.style == NEO_HUD_STYLE_BODY;
	const Ring ring = { &f, f.ringCentre, f.ringRadii, bBody ? 0.45f : 0.6f };
	const auto rel = [&](float worldYaw) { return AngleNormalize(s.yaw - worldYaw); };
	const float view = Heading(s.yaw);
	const float a = bBody ? LookOf(f, GROUP_BODY).alpha : 1.0f;

	// The ring, brighter ahead; ticks every 30 degrees of heading.
	for (int i = 0; i < 72; ++i)
	{
		const float b0 = i * 5.0f - 180.0f;
		Line(f, ring.At(b0), ring.At(b0 + 5.0f), NEO_GHOST_LIGHT, f.color, (0.1f + 0.22f * Ring::Front(b0 + 2.5f)) * a);
	}
	for (int h = 0; h < 360; h += 30)
	{
		const float b = AngleNormalize(h - view);
		Line(f, ring.At(b), ring.At(b, 10.0f), NEO_GHOST_LIGHT, f.color, 0.45f * (0.3f + 0.7f * Ring::Front(b)) * a);
	}
	static const wchar_t *s_cardinals[] = { L"N", L"E", L"S", L"W" };
	// The cardinals, pronounced (Kyle: more than the original compass): a heavy tick through the ring at each, the
	// letter large and never faded far, north with its own pointer outward.
	for (int i = 0; i < 4; ++i)
	{
		const float b = AngleNormalize(i * 90.0f - view), front = Ring::Front(b), ca = (0.5f + 0.5f * front) * a;
		Line(f, ring.At(b, -6.0f), ring.At(b, 14.0f), NEO_GHOST_HEAVY, f.color, ca);
		const Vector2D at = ring.At(b, bBody ? 26.0f : 32.0f);
		Text(f, s_cardinals[i], at.x, at.y, 0, bBody ? FONT_LABEL : FONT_VALUE_LARGE, f.color, Max(0.55f, ca));
		if (i == 0)
		{
			const Vector2D tip = ring.At(b, bBody ? 40.0f : 50.0f), base = ring.At(b, bBody ? 34.0f : 43.0f);
			const Vector2D across = Vector2D(base.y - tip.y, tip.x - base.x) * 0.8f;
			NeoGhostBegin(f.color, Alpha(f, Max(0.55f, ca)));
			const Vector2D pointer[4] = { tip, base + across, base - across, tip };
			NeoGhostFill(pointer);
		}
	}
	// The heading at the front.
	const Vector2D front = ring.At(0.0f);
	NeoGhostBegin(f.color, Alpha(f, 0.9f * a));
	const Vector2D notch[4] = { front + Vector2D(0, 2) * f.s, front + Vector2D(5, 10) * f.s, front + Vector2D(-5, 10) * f.s, front + Vector2D(0, 2) * f.s };
	NeoGhostFill(notch);
	// The objective (the ghost, or the juggernaut's marker), and squadmates.
	if (s.bObjective)
	{
		const float b = rel(s.objectiveYaw), d = 6.0f * f.s;
		const Vector2D p = ring.At(b);
		NeoGhostBegin(WARN, Alpha(f, (0.4f + 0.5f * Ring::Front(b)) * a));
		const Vector2D diamond[4] = { p + Vector2D(0, -d), p + Vector2D(d, 0), p + Vector2D(0, d), p + Vector2D(-d, 0) };
		NeoGhostFill(diamond);
		// Its distance as a line out from the ring, longer the further (log scale, 10 m to 300 m).
		const float reach = clamp(log10f(Max(s.objectiveMetres, 1.0f) / 10.0f) / log10f(30.0f), 0.0f, 1.0f);
		Line(f, ring.At(b, 8.0f), ring.At(b, 8.0f + 30.0f * reach), NEO_GHOST_LIGHT, WARN, (0.3f + 0.5f * Ring::Front(b)) * a);
	}
	for (int i = 0; i < s.mates; ++i)
	{
		const Vector2D p = ring.At(rel(s.mateYaw[i]));
		Rect(f, p - Vector2D(1.5f, 4.0f) * f.s, p + Vector2D(1.5f, 4.0f) * f.s, TEAM_OURS, 0.8f * a);
	}
	// Sounds you can hear, anywhere round you: an arc at the bearing, thicker the louder, arriving from outside.
	for (int i = 0; i < s.heardCount; ++i)
	{
		const Heard &h = s.heard[i];
		const float age = f.now - h.time, fade = 1.0f - age / 2.0f, b = rel(h.bearing);
		const Color c = h.kind == SOUND_GUNFIRE || h.kind == SOUND_BLAST ? WARN : f.color;
		const float span = bBody ? 16.0f : 12.0f;
		const NeoGhostWeight w = h.loud > 0.6f ? NEO_GHOST_HEAVY : h.loud > 0.3f ? NEO_GHOST_MEDIUM : NEO_GHOST_LIGHT;
		for (int layer = 0; layer < (h.loud > 0.6f ? 2 : 1); ++layer)
		{
			const Vector2D radii = f.ringRadii + Vector2D(layer * 3.0f, layer * 1.5f) * f.s;
			Arc(f, f.ringCentre, radii, b - span, b + span, w, c, 0.95f * fade);
		}
		if (age < 0.3f)
		{
			const Vector2D p = ring.At(b, 40.0f * (1.0f - age / 0.3f) + 6.0f);
			Rect(f, p - Vector2D(2.5f, 2.5f) * f.s, p + Vector2D(2.5f, 2.5f) * f.s, c, 0.9f);
		}
		KindGlyph(f, ring.At(b, bBody ? 22.0f : -16.0f), h.kind, c, 0.8f * fade);
	}
	// Your own noise: short ticks pushed out all round, as far as the sound carries (log scale).
	for (int i = 0; i < s.noiseCount; ++i)
	{
		const Noise &n = s.noise[i];
		const float age = f.now - n.time, reach = Min(1.0f, log10f(1.0f + n.metres) / 2.0f), grow = Min(1.0f, age / 0.2f),
			fade = 1.0f - age / 1.2f;
		const bool bLoud = n.metres >= 60.0f;
		const int count = bBody ? 12 : 16;
		for (int t = 0; t < count; ++t)
		{
			const float b = (t + 0.5f) * 360.0f / count;
			Line(f, ring.At(b, 3.0f), ring.At(b, 3.0f + 36.0f * reach * grow), bLoud ? NEO_GHOST_MEDIUM : NEO_GHOST_LIGHT,
				bLoud ? WARN : f.color, 0.7f * fade);
		}
	}
	// Your noise state as an icon on the ring's outer side, the range (while aiming) on the other.
	float loudest = 0.0f;
	for (int i = 0; i < s.noiseCount; ++i)
		loudest = Max(loudest, s.noise[i].metres);
	const int arcs = loudest >= 60.0f ? 3 : loudest >= 16.0f ? 2 : loudest > 0.0f ? 1 : 0;
	const float side = static_cast<float>(-f.hand);	// away from the gun
	const Vector2D sp(f.ringCentre.x + side * (f.ringRadii.x + (bBody ? 70.0f : 34.0f) * f.s), f.ringCentre.y);
	NoiseIcon(f, sp, arcs, s.bSilent && arcs == 0, arcs >= 2 ? WARN : f.color, (arcs > 0 || s.bSilent) ? 0.9f : 0.35f);
	static ConVarRef cl_neo_hud_kanji("cl_neo_hud_kanji");
	if (cl_neo_hud_kanji.GetBool())
	{
		Text(f, L"\u9a12\u97f3", sp.x + side * 22.0f * f.s, sp.y, f.hand > 0 ? -1 : 1, FONT_KANJI, f.color, 0.4f);
	}
	if (s.bRange)
	{
		wchar_t range[24];
		if (s.rangeMetres < 0.0f)
			V_wcsncpy(range, L"RNG ---", sizeof(range));
		else
			V_snwprintf(range, ARRAYSIZE(range), L"RNG %.0f M", s.rangeMetres);
		const Vector2D rp(f.ringCentre.x - side * (f.ringRadii.x + 30.0f * f.s), f.ringCentre.y);
		Text(f, range, rp.x, rp.y, f.hand > 0 ? 1 : -1, FONT_VALUE, f.color, 0.7f);
	}
}
} // namespace NeoCyberbrain
