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
		// Two level cells, stacked, the fill aux over what two jumps cost (phase 5): the recharge shows as the fill, in strips
		// while sprinting (half rate), the banked ones as outlines when too long in the air to use them.
		const float x = 22.0f;
		CellStyle style;
		style.count = 2;
		style.chamfer = chamfer;
		style.bCharging = s.aux < 2.0f * JUMP_COST - 0.5f && fmodf(s.aux, JUMP_COST) > 0.01f;
		style.bStrips = s.bSprinting && style.bCharging;
		style.bLocked = s.bJumpsLocked;
		style.fill = f.color;
		const Vector2D p0 = L.At(d * x, -10.0f), p1 = L.At(d * (x + 13.0f), 40.0f);
		const Vector2D lo(Min(p0.x, p1.x), p0.y), hi(Max(p0.x, p1.x), p1.y);
		Cells(f, lo, hi, clamp(s.aux / (2.0f * JUMP_COST), 0.0f, 1.0f), style, a);

		// The ready flare: a cell becoming usable gives one brief outline round the stack, a rise and fall over 0.15 s at
		// Full, 0.25 s at Calm, 0.4 s at Still (slower, never shorter, and never a burst).
		static int s_ready = 0;
		static float s_readyAt = -100.0f;
		const int ready = static_cast<int>(s.aux / JUMP_COST);
		if (ready > s_ready && f.now - s_motion.lastSeen < 0.5f)
			s_readyAt = f.now;
		s_ready = ready;
		static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
		const int motion = cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1;
		const float flareFor = motion >= 2 ? 0.15f : motion == 1 ? 0.25f : 0.4f, age = f.now - s_readyAt;
		if (age >= 0.0f && age < flareFor)
		{
			const float pad = 3.0f * f.s, pulse = sinf(age / flareFor * M_PI_F);
			RectOutline(f, lo - Vector2D(pad, pad), hi + Vector2D(pad, pad), NEO_GHOST_MEDIUM, f.color, 0.9f * pulse * a);
		}

		// JMP: a budget word at the stack's foot (R7), crystallising with the group's plate.
		if (look.labels > 0.02f)
		{
			wchar_t word[8];
			Text(f, Crystallise(f, GROUP_MOTION, Word("neo_hud_cb_jmp", L"JMP"), word, ARRAYSIZE(word)), (lo.x + hi.x) * 0.5f,
				hi.y + (ROW_GAP + 5.0f) * L.k, 0, FONT_LABEL, s.bJumpsLocked ? WARN : f.color, look.labels * a);
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
	const float strideY = TRACE_TOP - 16.0f;
	PaintStrideStrip(f, L, left, right, strideY, Max(a, 0.6f));
	// The top rail: the plate a gap above the waveform at its loudest (R4; it sat inside a shot's reach).
	if (look.labels > 0.02f)
	{
		static const wchar_t *const s_kanji = L"\u6a5f\u52d5";
		const float gap = ROW_GAP * L.k;
		Stack rail = { L.At(0.0f, strideY - StrideReach()).y - gap, gap, -1 };
		const float y = rail.Row(PlateTall(f, Word("neo_hud_cb_motion", L"MOTION"), s_kanji));
		wchar_t word[16];
		Plate(f, Crystallise(f, GROUP_MOTION, Word("neo_hud_cb_motion", L"MOTION"), word, ARRAYSIZE(word)), L.At(left, 0.0f).x, y, 1, look.labels, s_kanji);
	}
}
} // namespace NeoCyberbrain
