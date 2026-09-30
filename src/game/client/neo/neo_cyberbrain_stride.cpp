#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The stride waveform (Kyle's pick, merging the gait with your noise): a strip over the speed trace where each of your
// footsteps lands live as a small mirrored burst of strokes, its height how far the step carried; silent steps (no
// sound, so counted from the distance you cover) land as a flat dash; any other sound you make (a shot, a landing, a
// reload) as a wider, taller burst. Spacing is cadence: the newest mark at the right, the older ones at their real
// time gaps, so the strip shifts only when a mark lands (motion only for events); each mark falls in with a small
// bounce and old ones fade out. Amber from 16 m.

namespace NeoCyberbrain
{
constexpr int STRIDE_MARKS = 24;
constexpr float STRIDE_PX_PER_SEC = 40.0f;		// pixels at 1080p between marks a second apart
constexpr float STRIDE_METRES = 0.85f;			// a silent stride
constexpr float STRIDE_FADE_FROM = 2.5f, STRIDE_FADE_FOR = 1.0f;
constexpr float STRIDE_HEIGHT = 11.0f;			// a burst's half height at full loudness, pixels at 1080p

enum MarkKind { MARK_STEP, MARK_SILENT, MARK_OTHER };
struct Mark { float time; float level; MarkKind kind; bool bLoud; unsigned int seed; };

static struct
{
	Mark marks[STRIDE_MARKS];
	int count = 0;
	float lastNoise = -100.0f;		// the newest of your noises already taken
	float lastStep = -100.0f;		// the last step of any kind
	float walked = 0.0f;			// metres covered since the last step
	float lastTime = -100.0f;
} s_stride;

static unsigned int StrideMix(unsigned int x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static float StrideUnit(unsigned int x)
{
	return (StrideMix(x) & 0xffffu) / 65535.0f;
}

static void Land(float time, float level, MarkKind kind, bool bLoud)
{
	if (s_stride.count == STRIDE_MARKS)
	{
		for (int i = 1; i < STRIDE_MARKS; ++i)
			s_stride.marks[i - 1] = s_stride.marks[i];
		--s_stride.count;
	}
	s_stride.marks[s_stride.count++] = { time, level, kind, bLoud, StrideMix(static_cast<unsigned int>(time * 1000.0f)) };
	if (kind != MARK_OTHER)
	{
		s_stride.lastStep = time;
		s_stride.walked = 0.0f;
	}
}

// Takes your new noises, and counts silent strides from the distance covered on the ground.
static void Listen(const Senses &s, float now)
{
	const float dt = (now < s_stride.lastTime || now - s_stride.lastTime > 0.5f) ? 0.0f : now - s_stride.lastTime;
	s_stride.lastTime = now;
	float newest = s_stride.lastNoise;
	for (int i = 0; i < s.noiseCount; ++i)
	{
		const Noise &n = s.noise[i];
		if (n.time <= s_stride.lastNoise)
			continue;
		newest = Max(newest, n.time);
		const float level = clamp(log10f(1.0f + n.metres) / 2.0f, 0.2f, 1.0f);
		Land(n.time, level, n.kind == SOUND_STEP ? MARK_STEP : MARK_OTHER, n.metres >= 16.0f);
	}
	s_stride.lastNoise = newest;
	if (s.bMoving && s.air < 0.5f)
	{
		s_stride.walked += s.speed * dt;
		// A stride's worth covered and no step heard for it: a silent one.
		if (s_stride.walked >= STRIDE_METRES && now - s_stride.lastStep > 0.25f)
			Land(now, 0.0f, MARK_SILENT, false);
	}
	else
	{
		s_stride.walked = 0.0f;
	}
}

void PaintStrideStrip(const Frame &f, const Local &L, float left, float right, float y, float alpha)
{
	const Senses &s = *f.pSenses;
	Listen(s, f.now);
	// The baseline, faint.
	for (float x = left; x < right; x += 6.0f)
		Line(f, L.At(x, y), L.At(Min(x + 3.0f, right), y), NEO_GHOST_LIGHT, f.color, 0.2f * alpha);
	if (s_stride.count == 0)
		return;
	const float newest = s_stride.marks[s_stride.count - 1].time;
	for (int i = 0; i < s_stride.count; ++i)
	{
		const Mark &mark = s_stride.marks[i];
		const float age = f.now - mark.time;
		const float fade = 1.0f - clamp((age - STRIDE_FADE_FROM) / STRIDE_FADE_FOR, 0.0f, 1.0f);
		// Its place: newest at the right, the rest at their real gaps (it moves only when a mark lands).
		const float x = right - 4.0f - (newest - mark.time) * STRIDE_PX_PER_SEC;
		if (fade <= 0.0f || x < left)
			continue;
		const Color c = mark.bLoud ? WARN : f.color;
		// Falling in: dropped from a little above, a small damped bounce on landing.
		const float drop = age < 0.4f ? -7.0f * expf(-age / 0.07f) * cosf(age * 26.0f) : 0.0f;
		const float a = fade * alpha;
		if (mark.kind == MARK_SILENT)
		{
			Line(f, L.At(x - 3.0f, y + drop), L.At(x + 3.0f, y + drop), NEO_GHOST_MEDIUM, c, 0.7f * a);
			continue;
		}
		const int strokes = mark.kind == MARK_OTHER ? 5 : 3;
		const float pitch = 2.5f, mid = (strokes - 1) * 0.5f;
		const float scale = mark.kind == MARK_OTHER ? 1.5f : 1.0f;
		for (int k = 0; k < strokes; ++k)
		{
			const float window = 1.0f - fabsf(k - mid) / (mid + 1.0f);
			const float h = Max(1.5f, STRIDE_HEIGHT * scale * mark.level * window * (0.55f + 0.45f * StrideUnit(mark.seed + k)));
			const float sx = x + (k - mid) * pitch;
			Line(f, L.At(sx, y - h + drop), L.At(sx, y + h + drop), NEO_GHOST_MEDIUM, c, 0.9f * a);
		}
	}
}
} // namespace NeoCyberbrain
