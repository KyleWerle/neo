#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The surround ring: the compass as a ring on the ground round you, in perspective. Ahead is its far (top) edge,
// behind its near (bottom) edge; it opens toward a full circle as you look down, and turns with you as the world does.
// On it: the heading, the objective, the ghost's callouts, squadmates, every sound you can hear (the full circle), and your own noise as
// short ticks pushed out from it (short and local: nothing flows across the view). It says where things are, not
// everything about them: your noise state and the range live in the motion and weapon groups. Compact: small at the bottom
// centre. On the body: it is the body group's ground disc, so sounds land round your own feet. Glyphs, not numbers: a
// sound's kind is a mark beside its arc, the objective's distance a line's length.
// Headings follow NT's compass (neo_hud_compass.cpp): a world yaw Y reads as 180 - Y degrees, north up.

namespace NeoCyberbrain
{
static float Heading(float worldYaw) { return AngleNormalizePositive(180.0f - worldYaw); }

// A sound's kind as a small mark at p, readable at a glance in a fight (Kyle's picks): steps as the stride waveform's
// three-stroke burst, so a step reads the same on the ring as on your own strip; the rest as primitives that differ
// in outline alone: gunfire a sharp triangle, a reload a bracket, a landing a down wedge, a blast an octagon.
static void KindGlyph(const Frame &f, const Vector2D &p, SoundKind kind, const Color &c, float a)
{
	const float k = f.s;
	switch (kind)
	{
	case SOUND_STEP:
		Line(f, p + Vector2D(-3.0f, -3.0f) * k, p + Vector2D(-3.0f, 3.0f) * k, NEO_GHOST_MEDIUM, c, a);
		Line(f, p + Vector2D(0.0f, -5.5f) * k, p + Vector2D(0.0f, 5.5f) * k, NEO_GHOST_MEDIUM, c, a);
		Line(f, p + Vector2D(3.0f, -3.0f) * k, p + Vector2D(3.0f, 3.0f) * k, NEO_GHOST_MEDIUM, c, a);
		break;
	case SOUND_GUNFIRE:
	{
		NeoGhostBegin(c, Alpha(f, a));
		const Vector2D tri[4] = { p + Vector2D(0.0f, -6.0f) * k, p + Vector2D(5.5f, 5.0f) * k, p + Vector2D(-5.5f, 5.0f) * k,
			p + Vector2D(0.0f, -6.0f) * k };
		NeoGhostFill(tri);
		break;
	}
	case SOUND_RELOAD:
		Line(f, p + Vector2D(3.0f, -5.5f) * k, p + Vector2D(-3.0f, -5.5f) * k, NEO_GHOST_MEDIUM, c, a);
		Line(f, p + Vector2D(-3.0f, -5.5f) * k, p + Vector2D(-3.0f, 5.5f) * k, NEO_GHOST_MEDIUM, c, a);
		Line(f, p + Vector2D(-3.0f, 5.5f) * k, p + Vector2D(3.0f, 5.5f) * k, NEO_GHOST_MEDIUM, c, a);
		break;
	case SOUND_LAND:
	{
		NeoGhostBegin(c, Alpha(f, a));
		const Vector2D wedge[4] = { p + Vector2D(-6.0f, -4.0f) * k, p + Vector2D(6.0f, -4.0f) * k, p + Vector2D(0.0f, 5.0f) * k,
			p + Vector2D(0.0f, 5.0f) * k };
		NeoGhostFill(wedge);
		break;
	}
	case SOUND_BLAST:
	{
		const float r = 7.0f, q = r * 0.414f;
		const Vector2D oct[9] = { Vector2D(-q, -r), Vector2D(q, -r), Vector2D(r, -q), Vector2D(r, q), Vector2D(q, r), Vector2D(-q, r),
			Vector2D(-r, q), Vector2D(-r, -q), Vector2D(-q, -r) };
		for (int i = 0; i < 8; ++i)
			Line(f, p + oct[i] * k, p + oct[i + 1] * k, NEO_GHOST_HEAVY, c, a);
		break;
	}
	default:
		break;
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

static Vector2D Unit(const Vector2D &v)
{
	const float len = v.Length();
	return len > 1e-4f ? v / len : Vector2D(1.0f, 0.0f);
}

// A distance as a line's length out from the ring, 0 to 1 (log scale, 10 m to 300 m).
static float DistanceReach(float metres)
{
	return clamp(log10f(Max(metres, 1.0f) / 10.0f) / log10f(30.0f), 0.0f, 1.0f);
}

// The ghost's enemy callouts, as the compass's red arrows but at their real bearing (behind you too). Kyle: above
// everything else on the ring, always exact and highlighted, just outside the sound icons: drawn last, a hairline from
// the ring to the wedge at the exact bearing, the wedge pointing in on a dark backing, at full strength ahead or
// behind, fading only over the callout's time; a fresh one closes in with a bracket. The newest carries its distance
// as a line, and as metres while it's fresh (the compass always showed it).
template <typename Rel>
static void PaintCallouts(const Frame &f, const Ring &ring, const Rel &rel, float out0)
{
	const Senses &s = *f.pSenses;
	for (int pass = 0; pass < 2; ++pass)	// older first, the newest on top
	{
		for (int i = 0; i < s.calloutCount; ++i)
		{
			const bool bNewest = i == s.calloutNewest;
			if (bNewest != (pass == 1))
				continue;
			const Callout &c = s.callout[i];
			const float b = rel(c.yaw), fade = c.life;
			const Vector2D tip = ring.At(b, out0), base = ring.At(b, out0 + (bNewest ? 12.0f : 9.0f));
			const Vector2D along = Unit(base - tip), across = Unit(Vector2D(base.y - tip.y, tip.x - base.x)) * (bNewest ? 5.0f : 4.0f) * f.s;
			Line(f, ring.At(b, 0.0f), tip, NEO_GHOST_LIGHT, CRIT, 0.85f * fade);
			const float rim = 2.5f * f.s;
			NeoGhostBegin(Color(0, 0, 0, 255), Alpha(f, 0.6f * fade));
			const Vector2D backing[4] = { tip - along * rim * 1.5f, base + along * rim + across * 1.6f,
				base + along * rim - across * 1.6f, tip - along * rim * 1.5f };
			NeoGhostFill(backing);
			NeoGhostBegin(CRIT, Alpha(f, (bNewest ? 1.0f : 0.85f) * fade));
			const Vector2D wedge[4] = { tip, base + across, base - across, tip };
			NeoGhostFill(wedge);
			if (c.age < 0.25f)
			{
				const float close = 1.0f - c.age / 0.25f, gap = (6.0f + 10.0f * close) * f.s;
				const Vector2D mid = (tip + base) * 0.5f, side = Unit(across) * gap;
				Line(f, mid - side + (base - tip) * 0.6f, mid - side - (base - tip) * 0.6f, NEO_GHOST_MEDIUM, CRIT, 0.9f * close);
				Line(f, mid + side + (base - tip) * 0.6f, mid + side - (base - tip) * 0.6f, NEO_GHOST_MEDIUM, CRIT, 0.9f * close);
			}
			if (bNewest)
			{
				Line(f, ring.At(b, out0 + 15.0f), ring.At(b, out0 + 15.0f + 30.0f * DistanceReach(c.metres)), NEO_GHOST_LIGHT, CRIT,
					0.7f * fade);
				if (c.age < 3.0f)
				{
					wchar_t metres[16];
					V_snwprintf(metres, ARRAYSIZE(metres), L"%.0f M", c.metres);
					const Vector2D at = ring.At(b, out0 + 53.0f);
					Text(f, metres, at.x, at.y, 0, FONT_VALUE, CRIT, 0.85f * fade * Min(1.0f, (3.0f - c.age) / 0.5f));
				}
			}
		}
	}
}

// Which heard sound matters most (Kyle: in a real fight the marks crowded; weight them to the highest priority): danger
// first (gunfire, blasts), then someone close (steps), reloads, landings, the rest; louder and fresher above quieter
// and older.
// Kyle's order, strict: explosions, shots, landings, reloads, steps, then anything else; loudness and freshness only
// rank sounds of one kind. A teammate's sound comes after every enemy's (and never leads, below).
static float SoundPriority(const Heard &h, float now)
{
	static const float s_rank[SOUND__COUNT] = { 1.0f, 4.0f, 2.0f, 3.0f, 5.0f, 0.0f };	// SoundKind's order
	const float fresh = 1.0f - clamp((now - h.time) / 2.0f, 0.0f, 1.0f);
	return s_rank[clamp(static_cast<int>(h.kind), 0, static_cast<int>(SOUND__COUNT) - 1)]
		+ 0.99f * (0.4f + 0.6f * h.loud) * (0.5f + 0.5f * fresh) - (h.bFriendly ? 10.0f : 0.0f);
}

// The listening mode made plain (Kyle: the level alone wasn't noticed): on entering, a ping ripples out from the ring;
// in it, a sonar sweep circles the ring once every three seconds, a dashed ring of the hearing's reach breathes
// outside it, and LSTN shows at its side. All of it fades in with the mode and goes as it leaves. The comfort forms
// (HUD-REWORK.md's table): Full as built; Calm a 25 px ping, one sweep then held where it ended, the reach steady;
// Still no ping, sweep or breathing, the reach and LSTN easing in over 0.6 s (and the ring's size fixed, in the
// element).
static void PaintListening(const Frame &f, const Ring &ring)
{
	const float age = f.listenFor;
	if (age < 0.0f)
		return;
	static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
	const int motion = cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1;
	const float in = NeoSmoothStep(age / 0.6f) * f.alpha;
	const Vector2D c = f.ringCentre, r = f.ringRadii;
	const float aspect = r.y / Max(r.x, 1.0f);
	if (motion > 0 && age < 0.7f)
	{
		const float t = age / 0.7f, grow = (motion >= 2 ? 60.0f : 25.0f) * NeoSmoothStep(t) * f.s;
		Arc(f, c, r + Vector2D(grow, grow * aspect), -180.0f, 180.0f, NEO_GHOST_MEDIUM, f.color, 0.6f * (1.0f - t));
	}
	// The sweep: a bright head trailing off behind it (calm: once round, then held).
	if (motion > 0)
	{
		const float swept = motion >= 2 ? fmodf(age * 120.0f, 360.0f) : Min(age * 120.0f, 359.0f);
		const float head = swept - 180.0f;
		for (int k = 0; k < 6; ++k)
		{
			const float b1 = head - k * 7.0f, b0 = b1 - 7.0f;
			Arc(f, c, r, b0, b1, k == 0 ? NEO_GHOST_MEDIUM : NEO_GHOST_LIGHT, f.color, 0.55f * (1.0f - k / 6.0f) * in);
		}
	}
	// The hearing's reach, dashed, breathing (steady below full).
	const float breathe = motion >= 2 ? 0.8f + 0.2f * sinf(age * 2.0f) : 0.9f, out = 18.0f * f.s;
	const Vector2D reach = r + Vector2D(out, out * aspect);
	for (int d = 0; d < 36; d += 2)
		Arc(f, c, reach, d * 10.0f - 180.0f, d * 10.0f - 175.0f, NEO_GHOST_LIGHT, f.color, 0.22f * breathe * in);
	const Vector2D at = ring.At(90.0f, 30.0f);
	Text(f, L"LSTN", at.x, at.y, -1, FONT_LABEL, f.color, 0.7f * in);
}

constexpr int SOUNDS_LEADING_BUSY = 2, SOUNDS_LEADING_QUIET = 6;	// full marks at most at once, in a fight and all quiet

// The compass on the ring: the ring brighter ahead, the heading's ticks, the cardinals, your heading's notch, and the
// objective in `oc` with its distance. a: the ring's strength; L: how closely you listen (the ring comes up quiet).
static void PaintCompass(const Frame &f, const Ring &ring, float a, float L, const Color &oc)
{
	const Senses &s = *f.pSenses;
	const bool bBody = f.style == NEO_HUD_STYLE_BODY;
	const auto rel = [&](float worldYaw) { return AngleNormalize(s.yaw - worldYaw); };
	const float view = Heading(s.yaw);
	const float ringA = a * (0.45f + 0.75f * L), compassA = 0.6f + 0.4f * L;

	// The ring, brighter ahead; ticks every 30 degrees of heading.
	for (int i = 0; i < 72; ++i)
	{
		const float b0 = i * 5.0f - 180.0f;
		Line(f, ring.At(b0), ring.At(b0 + 5.0f), NEO_GHOST_LIGHT, f.color, (0.1f + 0.22f * Ring::Front(b0 + 2.5f)) * ringA);
	}
	for (int h = 0; h < 360; h += 30)
	{
		const float b = AngleNormalize(h - view);
		Line(f, ring.At(b), ring.At(b, 10.0f), NEO_GHOST_LIGHT, f.color, 0.45f * (0.3f + 0.7f * Ring::Front(b)) * ringA);
	}
	static const wchar_t *s_cardinals[] = { L"N", L"E", L"S", L"W" };
	// The cardinals, pronounced (Kyle: more than the original compass, and readable): a heavy tick through the ring at
	// each; the letter large, at full strength whatever the body's attention, on its own small dark backing so it reads
	// over the capsule and bright scenes alike; north with its own pointer outward.
	for (int i = 0; i < 4; ++i)
	{
		const float b = AngleNormalize(i * 90.0f - view), front = Ring::Front(b), ca = (0.5f + 0.5f * front) * a * compassA;
		Line(f, ring.At(b, -6.0f), ring.At(b, 14.0f), NEO_GHOST_HEAVY, f.color, ca);
		const Vector2D at = ring.At(b, bBody ? 30.0f : 34.0f);
		const float w = TextWidth(s_cardinals[i], FONT_VALUE_LARGE) * 0.5f + 5.0f * f.s, h = 11.0f * f.s;
		Rect(f, at - Vector2D(w, h), at + Vector2D(w, h), Color(0, 0, 0, 255), 0.55f * compassA);
		Text(f, s_cardinals[i], at.x, at.y, 0, FONT_VALUE_LARGE, f.color, (0.8f + 0.2f * front) * compassA);
		if (i == 0)
		{
			const Vector2D tip = ring.At(b, bBody ? 52.0f : 56.0f), base = ring.At(b, bBody ? 45.0f : 49.0f);
			const Vector2D across = Vector2D(base.y - tip.y, tip.x - base.x) * 0.8f;
			NeoGhostBegin(f.color, Alpha(f, 0.9f * compassA));
			const Vector2D pointer[4] = { tip, base + across, base - across, tip };
			NeoGhostFill(pointer);
		}
	}
	// The heading at the front.
	const Vector2D front = ring.At(0.0f);
	NeoGhostBegin(f.color, Alpha(f, 0.9f * a));
	const Vector2D notch[4] = { front + Vector2D(0, 2) * f.s, front + Vector2D(5, 10) * f.s, front + Vector2D(-5, 10) * f.s, front + Vector2D(0, 2) * f.s };
	NeoGhostFill(notch);
	// The objective (the ghost, or the juggernaut's marker), coloured by who carries it: amber loose, green with your
	// team, red with theirs.
	if (s.bObjective)
	{
		const float b = rel(s.objectiveYaw), d = 6.0f * f.s;
		const Vector2D p = ring.At(b);
		NeoGhostBegin(oc, Alpha(f, (0.4f + 0.5f * Ring::Front(b)) * a));
		const Vector2D diamond[4] = { p + Vector2D(0, -d), p + Vector2D(d, 0), p + Vector2D(0, d), p + Vector2D(-d, 0) };
		NeoGhostFill(diamond);
		Line(f, ring.At(b, 8.0f), ring.At(b, 8.0f + 30.0f * DistanceReach(s.objectiveMetres)), NEO_GHOST_LIGHT, oc,
			(0.3f + 0.5f * Ring::Front(b)) * a);
	}
}

void PaintRing(const Frame &f)
{
	const Senses &s = *f.pSenses;
	const bool bBody = f.style == NEO_HUD_STYLE_BODY;
	const Ring ring = { &f, f.ringCentre, f.ringRadii, bBody ? 0.45f : 0.6f };
	const auto rel = [&](float worldYaw) { return AngleNormalize(s.yaw - worldYaw); };
	const float a = bBody ? LookOf(f, GROUP_BODY).alpha : 1.0f;
	// Listening (Kyle: in a fight you hear less; quiet, the noise round you and your own is what you focus on): the
	// ring, the compass and the sounds come up as it quiets, and draw back in action. The callouts never do.
	const float L = f.listen;
	PaintCompass(f, ring, a, L, s.carrier == CARRIER_OURS ? TEAM_OURS : s.carrier == CARRIER_THEIRS ? CRIT : WARN);
	for (int i = 0; i < s.mates; ++i)
	{
		const Vector2D p = ring.At(rel(s.mateYaw[i]));
		Rect(f, p - Vector2D(1.5f, 4.0f) * f.s, p + Vector2D(1.5f, 4.0f) * f.s, TEAM_OURS, 0.8f * a);
	}
	// Sounds you can hear, anywhere round you: an arc at the bearing, thicker the louder, arriving from outside. Only
	// the most important in each direction leads (its full arc and glyph, a few at once); the others near it stay as a
	// faint thin arc, so a fight reads as its loudest threat, not a pile of marks.
	const float span = bBody ? 16.0f : 12.0f;
	int order[MAX_HEARD];
	float priority[MAX_HEARD];
	for (int i = 0; i < s.heardCount; ++i)
	{
		order[i] = i;
		priority[i] = SoundPriority(s.heard[i], f.now);
	}
	for (int i = 1; i < s.heardCount; ++i)	// by priority, highest first (a handful: insertion)
	{
		for (int j = i; j > 0 && priority[order[j]] > priority[order[j - 1]]; --j)
			V_swap(order[j], order[j - 1]);
	}
	bool bLeads[MAX_HEARD] = {};
	int leading = 0;
	const int leadMost = SOUNDS_LEADING_BUSY + RoundFloatToInt((SOUNDS_LEADING_QUIET - SOUNDS_LEADING_BUSY) * L);
	for (int n = 0; n < s.heardCount && leading < leadMost; ++n)
	{
		const int i = order[n];
		if (s.heard[i].bFriendly)
			break;	// ordered last: none of the rest are enemies'
		bool bClear = true;
		for (int m = 0; m < n && bClear; ++m)
		{
			if (bLeads[order[m]] && fabsf(AngleNormalize(s.heard[i].bearing - s.heard[order[m]].bearing)) < 2.0f * span + 6.0f)
				bClear = false;
		}
		if (bClear)
		{
			bLeads[i] = true;
			++leading;
		}
	}
	for (int i = 0; i < s.heardCount; ++i)
	{
		if (bLeads[i])
			continue;
		const Heard &h = s.heard[i];
		const float fade = 1.0f - (f.now - h.time) / 2.0f, b = rel(h.bearing);
		// A teammate's: shorter and fainter still, so it reads as ours and not a threat (all but gone in a fight; quiet,
		// it's part of the noise round you).
		const float reach = h.bFriendly ? 0.35f + 0.2f * L : 0.7f;
		Arc(f, f.ringCentre, f.ringRadii, b - span * reach, b + span * reach, NEO_GHOST_LIGHT, f.color,
			(h.bFriendly ? 0.04f + 0.26f * L : 0.15f + 0.3f * L) * fade);
	}
	for (int i = 0; i < s.heardCount; ++i)
	{
		if (!bLeads[i])
			continue;
		const Heard &h = s.heard[i];
		const float age = f.now - h.time, fade = 1.0f - age / 2.0f, b = rel(h.bearing);
		const Color c = h.kind == SOUND_GUNFIRE || h.kind == SOUND_BLAST ? WARN : f.color;
		const NeoGhostWeight w = h.loud > 0.6f ? NEO_GHOST_HEAVY : h.loud > 0.3f ? NEO_GHOST_MEDIUM : NEO_GHOST_LIGHT;
		for (int layer = 0; layer < (h.loud > 0.6f ? 2 : 1); ++layer)
		{
			const Vector2D radii = f.ringRadii + Vector2D(layer * 3.0f, layer * 1.5f) * f.s;
			Arc(f, f.ringCentre, radii, b - span, b + span, w, c, (0.75f + 0.2f * L) * fade);
		}
		if (age < 0.3f)
		{
			const Vector2D p = ring.At(b, 40.0f * (1.0f - age / 0.3f) + 6.0f);
			Rect(f, p - Vector2D(2.5f, 2.5f) * f.s, p + Vector2D(2.5f, 2.5f) * f.s, c, 0.9f);
		}
		KindGlyph(f, ring.At(b, bBody ? 22.0f : -16.0f), h.kind, c, (0.65f + 0.3f * L) * fade);
	}
	// Your own noise: short ticks pushed out all round, as far as the sound carries (log scale); faint in a fight, and
	// quiet, what you listen to most (further out, stronger).
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
			Line(f, ring.At(b, 3.0f), ring.At(b, 3.0f + 36.0f * (0.7f + 0.4f * L) * reach * grow),
				bLoud ? NEO_GHOST_MEDIUM : NEO_GHOST_LIGHT, bLoud ? WARN : f.color, (0.25f + 0.65f * L) * fade);
		}
	}
	PaintListening(f, ring);
	// The ghost's callouts over everything, just outside the sound icons: On the body the glyphs sit out past the ring
	// (22), so the callouts start beyond them; Compact's sit inside it, so just outside the ring.
	PaintCallouts(f, ring, rel, bBody ? 32.0f : 6.0f);
}

// Spectating (dead and watching, or a spectator): the stock compass's place, in the ring's form. Only what the stock
// compass shows (parity): the view's heading, the objective, the ghost's callouts to a dead teammate (none to a
// spectator; the model filters them as the compass does), and the range while the watched player aims. No sounds,
// mates or noise: those are your own senses, and you have none now. Always as quiet (L 1): nothing to fight.
void PaintSpectatorRing(const Frame &f, const Color &objective)
{
	const Senses &s = *f.pSenses;
	const Ring ring = { &f, f.ringCentre, f.ringRadii, 0.6f };
	const auto rel = [&](float worldYaw) { return AngleNormalize(s.yaw - worldYaw); };
	PaintCompass(f, ring, 1.0f, 1.0f, objective);
	if (s.bRange)
	{
		wchar_t range[24];
		if (s.rangeMetres < 0.0f)
			V_wcsncpy(range, L"RNG ---", sizeof(range));
		else
			V_snwprintf(range, ARRAYSIZE(range), L"RNG %.0f M", s.rangeMetres);
		const Vector2D at = ring.At(90.0f, 34.0f);
		Text(f, range, at.x, at.y, 1, FONT_VALUE, f.color, 0.9f);
	}
	PaintCallouts(f, ring, rel, 6.0f);
}
} // namespace NeoCyberbrain
