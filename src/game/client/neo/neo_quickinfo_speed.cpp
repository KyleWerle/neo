#include "cbase.h"
#include "neo_quickinfo_internal.h"
#include "c_neo_player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The speed graph (QUICKINFO.md; Agiel's idea), bottom left where the health panel was: the last eight seconds of
// your horizontal speed in metres a second, lines at your class's walk, run and sprint speeds (with the weapon in
// hand), the current speed above. Its top follows the fastest speed in view, growing quickly and settling slowly,
// so a bunny hop's climb always fits. Part of the band's setting; no server gating (it's only your own speed).

namespace NeoQuickInfo
{
constexpr float METRES_PER_UNIT = 0.0254f;	// as the range readout
constexpr float SPAN = 8.0f;				// seconds shown
constexpr float SAMPLE = 0.1f;				// a sample this often, the fastest speed since the last (a hop's peak stays)
constexpr int SAMPLES = 96;				// more than SPAN / SAMPLE
// Pixels at 1080p from the screen's bottom left corner (y down, so negative is up).
constexpr float GX0 = 96.0f, GX1 = 456.0f, GY0 = -105.0f, GY1 = -17.0f;
constexpr float HEADROOM = 1.15f;			// the top sits this far over the fastest speed in view
constexpr float TOP_FLOOR = 1.4f;			// and never under the class's fastest line times this
constexpr float GROW = 0.12f, SHRINK = 0.8f;	// the top's ease up and back down, seconds
constexpr float MOVING = 0.5f;				// m/s: below this the graph fades to the floor
constexpr float LABEL_GAP = 12.0f;			// a line's label hides when it would sit closer than this to the one above

static struct
{
	float samples[SAMPLES];
	int head = 0, count = 0;				// head: the next slot
	float sinceSample = 0.0f;
	float pending = 0.0f;					// the fastest speed since the last sample
	float top = 0.0f;
	float fade = 1.0f;
} s_speed;

static void Push(float speed)
{
	s_speed.samples[s_speed.head] = speed;
	s_speed.head = (s_speed.head + 1) % SAMPLES;
	s_speed.count = Min(s_speed.count + 1, SAMPLES);
}
// The i-th newest sample (0 the newest).
static float Newest(int i)
{
	return s_speed.samples[(s_speed.head - 1 - i + SAMPLES * 2) % SAMPLES];
}

void PaintSpeed(const QuickFrame &frame, C_NEO_Player *pPlayer, float dt, bool bBoot, float floorAlpha)
{
	const float speed = pPlayer->GetAbsVelocity().Length2D() * METRES_PER_UNIT;
	if (bBoot)
	{
		s_speed.head = s_speed.count = 0;
		s_speed.sinceSample = 0.0f;
		s_speed.pending = 0.0f;
		s_speed.top = 0.0f;
		s_speed.fade = 1.0f;
	}
	s_speed.sinceSample += dt;
	s_speed.pending = Max(s_speed.pending, speed);
	if (s_speed.count == 0)
	{
		Push(speed);
	}
	while (s_speed.sinceSample >= SAMPLE)
	{
		s_speed.sinceSample -= SAMPLE;
		Push(s_speed.pending);
		s_speed.pending = speed;
	}
	const int shown = Min(s_speed.count, static_cast<int>(SPAN / SAMPLE));

	// The class's lines, with the weapon in hand: walk (as crouched), run, and sprint for those who can.
	const bool bSprints = pPlayer->GetClass() != NEO_CLASS_SUPPORT;
	float refs[3] = { pPlayer->GetCrouchSpeed_WithActiveWepEncumberment() * METRES_PER_UNIT,
		pPlayer->GetNormSpeed_WithActiveWepEncumberment() * METRES_PER_UNIT,
		bSprints ? pPlayer->GetSprintSpeed_WithActiveWepEncumberment() * METRES_PER_UNIT : 0.0f };
	const int refCount = bSprints ? 3 : 2;
	float peak = 0.0f;
	for (int i = 0; i < shown; ++i)
	{
		peak = Max(peak, Newest(i));
	}
	const float target = Max(refs[refCount - 1] * TOP_FLOOR, peak * HEADROOM);
	s_speed.top = (s_speed.top <= 0.0f) ? target
		: s_speed.top + (target - s_speed.top) * Min(1.0f, dt / ((target > s_speed.top) ? GROW : SHRINK));
	const float top = Max(0.1f, s_speed.top);
	s_speed.fade = Approach((speed > MOVING) ? 1.0f : floorAlpha, s_speed.fade, dt / ((speed > MOVING) ? 0.06f : 0.6f));

	// Its own frame: from the screen's bottom left, still (no sway), its own fade.
	QuickFrame g = frame;
	g.centre.Init(0.0f, frame.centre.y * 2.0f);
	for (Vector2D &sway : g.sway)
	{
		sway.Init(0.0f, 0.0f);
	}
	g.alpha = s_speed.fade * frame.reveal;
	const auto y = [&](float v) { return GY1 - (GY1 - GY0) * Min(1.0f, v / top); };

	// The floor (the time axis) with a tick a second, longer every four; a registration cross at each corner, the
	// left pair out past the speed labels.
	Line(g, LAYER_DETAIL, GX0, GY1, GX1, GY1, NEO_GHOST_MEDIUM, g.color, 0.7f);
	for (int k = 1; k <= static_cast<int>(SPAN); ++k)
	{
		const float x = GX1 - (GX1 - GX0) * k / SPAN;
		Line(g, LAYER_DETAIL, x, GY1, x, GY1 + ((k % 4) ? 3.0f : 6.0f), NEO_GHOST_LIGHT, g.color, 0.4f);
	}
	Cross(g, LAYER_DETAIL, GX0 - 48.0f, GY0 - 4.0f, 0.45f);
	Cross(g, LAYER_DETAIL, GX1 + 10.0f, GY0 - 4.0f, 0.45f);
	Cross(g, LAYER_DETAIL, GX0 - 48.0f, GY1 + 4.0f, 0.45f);
	Cross(g, LAYER_DETAIL, GX1 + 10.0f, GY1 + 4.0f, 0.45f);

	// The class's lines, labelled on the left; squeezed by a high top, a label too close to the one above hides
	// (the fastest, sprint's, stays).
	wchar_t text[24];
	V_snwprintf(text, ARRAYSIZE(text), L"%.1f", top);
	Text(g, LAYER_DETAIL, text, V_wcslen(text), GX0 - 8.0f, GY0, -1, FONT_SMALLER, g.color, 0.6f);
	float above = GY0 + LABEL_GAP;
	for (int i = refCount - 1; i >= 0; --i)
	{
		const float ry = y(refs[i]);
		Line(g, LAYER_DETAIL, GX0, ry, GX1, ry, NEO_GHOST_LIGHT, g.color, 0.3f);
		if (ry - above >= LABEL_GAP)
		{
			V_snwprintf(text, ARRAYSIZE(text), L"%.1f", refs[i]);
			Text(g, LAYER_DETAIL, text, V_wcslen(text), GX0 - 8.0f, ry, -1, FONT_SMALLER, g.color, 0.6f);
			above = ry;
		}
	}
	Text(g, LAYER_DETAIL, L"SPD.CH3", 7, GX0, GY0 - 16.0f, 1, FONT_SMALLER, g.color, 0.6f);
	V_snwprintf(text, ARRAYSIZE(text), L"%.1f M/S", speed);
	Text(g, LAYER_BAR, text, V_wcslen(text), GX1, GY0 - 16.0f, -1, FONT_LARGE, g.color, 0.95f);

	// The trace, newest at the right edge (the live speed there), a stroke a sample: 80 over the eight seconds.
	float xPrev = GX1, yPrev = y(speed);
	for (int i = 0; i < shown; ++i)
	{
		const float age = s_speed.sinceSample + i * SAMPLE;
		if (age > SPAN)
		{
			break;
		}
		const float x = GX1 - (GX1 - GX0) * age / SPAN, yy = y(Newest(i));
		Line(g, LAYER_BAR, xPrev, yPrev, x, yy, NEO_GHOST_HEAVY, g.color, 1.0f);
		xPrev = x;
		yPrev = yy;
	}
	NeoGhostFlush();
}
} // namespace NeoQuickInfo
