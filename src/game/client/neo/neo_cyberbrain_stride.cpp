#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The stride waveform (Kyle's pick, merging the gait with your noise): a strip over the speed trace, your noise as one
// smooth mirrored line (Kyle, 2026-09-30: separate marks overlapped; "ideally a smooth graph"). Each sound you make
// lifts the level to its height at once, how far it carried (a shot, a landing, a reload half again as tall as a step),
// and it eases back down; sounds close together blend into one rise. Silent strides (no sound, counted from the
// distance you cover on the ground) lift it only a little, so quiet movement still shows as a faint ripple. Sampled
// with the speed trace, so the two graphs share one clock (neo_cyberbrain_motion.cpp). Amber where a sound carried
// 16 m or more; the oldest tenth fades out.

namespace NeoCyberbrain
{
constexpr float STRIDE_METRES = 0.85f;			// a silent stride
constexpr float STRIDE_SILENT = 0.1f;			// a silent stride's lift
constexpr float STRIDE_DECAY = 0.18f;			// seconds for the level to fall to a third
constexpr float STRIDE_LOUD_FOR = 0.3f;			// seconds a loud sound keeps its stretch amber
constexpr float STRIDE_FADE_EDGE = 0.1f;		// the share of the trace, from its oldest end, the line fades over
constexpr float STRIDE_HEIGHT = 11.0f;			// the line's half height at a step's full loudness, pixels at 1080p

static struct
{
	float level = 0.0f;
	float lastNoise = -100.0f;		// the newest of your noises already taken
	float lastStep = -100.0f;		// the last step of any kind
	float walked = 0.0f;			// metres covered since the last step
	float lastTime = -100.0f;
	float loudUntil = -100.0f;
} s_stride;

static void Lift(float level)
{
	s_stride.level = Max(s_stride.level, level);
}

float StrideListen(const Senses &s, float now, bool &bLoud)
{
	const float dt = (now < s_stride.lastTime || now - s_stride.lastTime > 0.5f) ? 0.0f : now - s_stride.lastTime;
	s_stride.lastTime = now;
	s_stride.level *= expf(-dt / STRIDE_DECAY);
	float newest = s_stride.lastNoise;
	for (int i = 0; i < s.noiseCount; ++i)
	{
		const Noise &n = s.noise[i];
		if (n.time <= s_stride.lastNoise)
			continue;
		newest = Max(newest, n.time);
		const float level = clamp(log10f(1.0f + n.metres) / 2.0f, 0.2f, 1.0f);
		Lift(level * (n.kind == SOUND_STEP ? 1.0f : 1.5f));
		if (n.metres >= 16.0f)
			s_stride.loudUntil = n.time + STRIDE_LOUD_FOR;
		if (n.kind == SOUND_STEP)
		{
			s_stride.lastStep = n.time;
			s_stride.walked = 0.0f;
		}
	}
	s_stride.lastNoise = newest;
	if (s.bMoving && s.air < 0.5f)
	{
		s_stride.walked += s.speed * dt;
		// A stride's worth covered and no step heard for it: a silent one.
		if (s_stride.walked >= STRIDE_METRES && now - s_stride.lastStep > 0.25f)
		{
			Lift(STRIDE_SILENT);
			s_stride.lastStep = now;
			s_stride.walked = 0.0f;
		}
	}
	else
	{
		s_stride.walked = 0.0f;
	}
	bLoud = now < s_stride.loudUntil;
	return s_stride.level;
}

void PaintStrideStrip(const Frame &f, const Local &L, float left, float right, float y, float alpha)
{
	// The baseline, faint.
	for (float x = left; x < right; x += 6.0f)
		Line(f, L.At(x, y), L.At(Min(x + 3.0f, right), y), NEO_GHOST_LIGHT, f.color, 0.2f * alpha);
	const int count = MotionSamples();
	// A light smoothing across neighbours (1 2 1), so the line runs smooth between the tenths.
	const auto height = [&](int i, bool &bLoud) {
		bool bPrev, bNext;
		const float h = 0.25f * MotionNoiseAt(Max(i - 1, 0), bPrev) + 0.5f * MotionNoiseAt(i, bLoud)
			+ 0.25f * MotionNoiseAt(Min(i + 1, count - 1), bNext);
		return STRIDE_HEIGHT * h;
	};
	bool bLastLoud;
	float lastH = height(0, bLastLoud);
	for (int i = 1; i < count; ++i)
	{
		bool bLoud;
		const float h = height(i, bLoud);
		if (lastH > 0.3f || h > 0.3f)
		{
			const float x0 = left + (right - left) * (i - 1) / (count - 1), x1 = left + (right - left) * i / (count - 1);
			const float fade = clamp((x0 - left) / (STRIDE_FADE_EDGE * (right - left)), 0.0f, 1.0f);
			const Color &c = (bLoud || bLastLoud) ? WARN : f.color;
			Line(f, L.At(x0, y - lastH), L.At(x1, y - h), NEO_GHOST_MEDIUM, c, 0.9f * fade * alpha);
			Line(f, L.At(x0, y + lastH), L.At(x1, y + h), NEO_GHOST_MEDIUM, c, 0.9f * fade * alpha);
		}
		lastH = h;
		bLastLoud = bLoud;
	}
}
} // namespace NeoCyberbrain
