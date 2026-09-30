#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The motion group: proprioception of movement, its own group, kept beside the body (the line that tied them was
// dropped: Kyle found it weird; being side by side says it). Stamina (assault, juggernaut) as four loose cells, or recon's two jump cells filling as each recharges;
// speed as a short trace of the last five seconds (Agiel's speed graph, as a shape: no numbers), a line at your run
// speed, the scale growing for bunny hops; over it the stride waveform (neo_cyberbrain_stride.cpp: your noise as one
// smooth line on the same samples, gait and noise in one strip). Its attention: sprinting, recovering, recharging, going faster
// than a run, landing.

namespace NeoCyberbrain
{
constexpr float JUMP_COST = SUPER_JMP_COST;			// a recon's jump cell
constexpr int SAMPLES = 50;							// five seconds at ten a second (Kyle: both graphs on five)
constexpr float SAMPLE_EVERY = 0.1f;
constexpr float TRACE_W = 90.0f, TRACE_TOP = -8.0f, TRACE_BOTTOM = 28.0f;
constexpr float SCALE_GROW = 0.12f, SCALE_SHRINK = 0.8f;

static struct
{
	float samples[SAMPLES] = {};
	float noise[SAMPLES] = {};		// the stride waveform's level, sampled with the speed (the loudest in each tenth)
	bool loud[SAMPLES] = {};		// a sound in it carried 16 m or more
	int head = 0;
	float noisePeak = 0.0f;
	bool bLoudPeak = false;
	float peak = 0.0f, lastSample = -100.0f, lastSeen = -100.0f, top = 8.0f, lastTop = -100.0f;
} s_motion;

// Every tenth of a second, the fastest speed since the last sample (so a hop's peak stays), and the loudest noise.
static void Record(const Senses &s, float now, float noise, bool bLoud)
{
	if (now - s_motion.lastSeen > 0.5f || now < s_motion.lastSeen)
	{
		V_memset(s_motion.samples, 0, sizeof(s_motion.samples));
		V_memset(s_motion.noise, 0, sizeof(s_motion.noise));
		V_memset(s_motion.loud, 0, sizeof(s_motion.loud));
		s_motion.lastSample = now;
		s_motion.peak = 0.0f;
		s_motion.noisePeak = 0.0f;
		s_motion.bLoudPeak = false;
	}
	s_motion.lastSeen = now;
	s_motion.peak = Max(s_motion.peak, s.speed);
	s_motion.noisePeak = Max(s_motion.noisePeak, noise);
	s_motion.bLoudPeak = s_motion.bLoudPeak || bLoud;
	while (now - s_motion.lastSample >= SAMPLE_EVERY)
	{
		s_motion.samples[s_motion.head] = s_motion.peak;
		s_motion.noise[s_motion.head] = s_motion.noisePeak;
		s_motion.loud[s_motion.head] = s_motion.bLoudPeak;
		s_motion.head = (s_motion.head + 1) % SAMPLES;
		s_motion.lastSample += SAMPLE_EVERY;
		s_motion.peak = s.speed;
		s_motion.noisePeak = noise;
		s_motion.bLoudPeak = bLoud;
	}
}

int MotionSamples()
{
	return SAMPLES;
}

float MotionNoiseAt(int i, bool &bLoud)
{
	const int at = (s_motion.head + i) % SAMPLES;
	bLoud = s_motion.loud[at];
	return s_motion.noise[at];
}

void PaintMotion(const Frame &f)
{
	const Senses &s = *f.pSenses;
	bool bLoud;
	const float noise = StrideListen(s, f.now, bLoud);
	Record(s, f.now, noise, bLoud);
	const Look look = LookOf(f, GROUP_MOTION);
	const Local L = { f.pPlaces[GROUP_MOTION].pos, f.s * look.scale, f.hand };
	const float a = look.alpha;
	const Place &body = f.pPlaces[GROUP_BODY];
	const float d = body.pos.x >= L.origin.x ? 1.0f : -1.0f;	// toward the body


	// Cells on the body's side: stamina in four, recon's two jumps one each (each fills as it recharges).
	const int chamfer = d > 0.0f ? -1 : 1;	// the cut corner away from the body
	if (s.bHasSprint)
	{
		const float fill = s.aux / 100.0f;
		CellStyle style;
		style.count = 4;
		style.chamfer = chamfer;
		style.bCharging = !s.bSprinting && fill < 0.995f;
		style.fill = fill < 0.25f ? WARN : f.color;
		const Vector2D p0 = L.At(d * 22.0f, -58.0f), p1 = L.At(d * 36.0f, 40.0f);
		Cells(f, Vector2D(Min(p0.x, p1.x), p0.y), Vector2D(Max(p0.x, p1.x), p1.y), fill, style, a);
	}
	if (s.bHasJumps)
	{
		for (int i = 0; i < 2; ++i)
		{
			const float fill = clamp((s.aux - i * JUMP_COST) / JUMP_COST, 0.0f, 1.0f), x = 22.0f + i * 17.0f;
			CellStyle style;
			style.chamfer = chamfer;
			style.bCharging = fill > 0.0f && fill < 1.0f;
			style.fill = f.color;
			const Vector2D p0 = L.At(d * x, -10.0f), p1 = L.At(d * (x + 13.0f), 40.0f);
			Cells(f, Vector2D(Min(p0.x, p1.x), p0.y), Vector2D(Max(p0.x, p1.x), p1.y), fill, style, a);
		}
	}

	// The speed trace, away from the body; time runs left to right whichever side it sits.
	const float left = d > 0.0f ? -TRACE_W - 6.0f : 6.0f, right = left + TRACE_W;
	float most = 0.0f;
	for (const float sample : s_motion.samples)
	{
		most = Max(most, sample);
	}
	const float want = Max(s.runSpeed * 1.7f, most * 1.15f);
	const float dt = clamp(f.now - s_motion.lastTop, 0.0f, 0.1f);
	s_motion.lastTop = f.now;
	s_motion.top += (want - s_motion.top) * Min(1.0f, dt / (want > s_motion.top ? SCALE_GROW : SCALE_SHRINK));
	const float top = Max(s_motion.top, 1.0f);
	const auto yOf = [&](float speed) { return TRACE_BOTTOM - (TRACE_BOTTOM - TRACE_TOP) * clamp(speed / top, 0.0f, 1.0f); };
	Line(f, L.At(left, TRACE_BOTTOM), L.At(right, TRACE_BOTTOM), NEO_GHOST_LIGHT, f.color, 0.35f * a);
	for (int k = 0; k <= 5; ++k)	// a tick a second
	{
		const float x = left + TRACE_W * k / 5.0f;
		Line(f, L.At(x, TRACE_BOTTOM), L.At(x, TRACE_BOTTOM + 3.0f), NEO_GHOST_LIGHT, f.color, 0.3f * a);
	}
	// Your run speed as a dashed reference.
	const float runY = yOf(s.runSpeed);
	for (float x = left; x < right; x += 8.0f)
	{
		Line(f, L.At(x, runY), L.At(Min(x + 4.0f, right), runY), NEO_GHOST_LIGHT, f.color, 0.25f * a);
	}
	Vector2D last;
	for (int i = 0; i < SAMPLES; ++i)
	{
		const float speed = s_motion.samples[(s_motion.head + i) % SAMPLES];
		const Vector2D p = L.At(left + TRACE_W * i / (SAMPLES - 1), yOf(speed));
		if (i > 0)
		{
			Line(f, last, p, NEO_GHOST_MEDIUM, speed > s.runSpeed * 1.2f ? WARN : f.color, 0.85f * a);
		}
		last = p;
	}

	// Over the trace: the stride waveform, your steps and sounds landing live (it took the chevrons' and the noise
	// waveform's place: one strip for gait and noise), on the trace's own samples: Kyle, the two graphs on one
	// timescale, so a step sits over the speed it was taken at.
	PaintStrideStrip(f, L, left, right, TRACE_TOP - 16.0f, Max(a, 0.6f));
	if (look.labels > 0.02f)
	{
		const Vector2D pa = L.At(left, TRACE_TOP - 40.0f);
		Plate(f, L"MOTION", pa.x, pa.y, 1, look.labels, L"\u6a5f\u52d5");
	}
}
} // namespace NeoCyberbrain
