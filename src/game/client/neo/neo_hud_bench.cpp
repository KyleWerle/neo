#include "cbase.h"
#include "neo_hud_profile.h"
#include "c_neo_player.h"
#include "igamesystem.h"
#include "filesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include <time.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// neo_hud_bench: a hands-off benchmark of the HUD styles (HUD-SYSTEM.md). Stand somewhere representative (a busy
// view; bots on your team give the squad list rows), turn vsync off, type the command and don't touch anything. Each
// scenario below is set with convars, the view held still, let settle, then measured: real frame time (CPU and GPU
// together), the HUD's CPU time by section (NEO_HUD_PROFILE) and its counts per frame. Everything runs twice, the
// second pass in reverse order, so drift over the run shows as a gap between the passes. It prints a table, writes a
// CSV under hud_bench/, and puts the convars back. No cheats needed. neo_hud_bench_stop aborts it.
//-----------------------------------------------------------------------------

struct HudBenchScenario
{
	const char *label;
	const char *commands;	// on top of the player's own style and backing, the text edge as copies
};

static const HudBenchScenario s_hudBenchScenarios[] = {
	{ "no hud", "cl_drawhud 0" },
	{ "0 original", "cl_neo_hud_style 0" },
	{ "1 compact", "cl_neo_hud_style 1" },
	{ "2 on the body", "cl_neo_hud_style 2" },
	{ "2 body, no backing", "cl_neo_hud_style 2; cl_neo_hud_backing 0" },
	{ "2 body, baked text", "cl_neo_hud_style 2; cl_neo_hud_text_baked 1" },
	{ "3 racer", "cl_neo_hud_style 3" },
	{ "4 competitive", "cl_neo_hud_style 4" },
};
static constexpr int HUD_BENCH_SCENARIOS = ARRAYSIZE(s_hudBenchScenarios);
static constexpr int HUD_BENCH_PASSES = 2;

struct HudBenchResult
{
	bool measured = false;
	int frames = 0;
	double frameMs = 0.0;		// mean frame time
	double worstMs = 0.0;		// mean of the slowest 1% of frames
	double sectionMs[NEO_HUD_PROFILE__COUNT] = {};	// mean CPU time per frame
	double counts[NEO_HUD_COUNT__COUNT] = {};		// mean per frame
};

class CNeoHudBench : public CAutoGameSystemPerFrame
{
public:
	CNeoHudBench() : CAutoGameSystemPerFrame("CNeoHudBench") {}

	void Start(float measureSeconds, float settleSeconds);
	void Stop(const char *pszWhy);
	void Update(float frametime) override;

private:
	enum Phase { IDLE, SETTLE, MEASURE };

	void Command(const char *pszFormat, ...);
	int ScenarioOf(int run) const;
	void BeginRun();
	void FinishRun();
	void DropSamples();
	void Report();
	void Restore();

	Phase m_phase = IDLE;
	int m_run = 0;
	float m_measureSeconds = 3.0f;
	float m_settleSeconds = 2.0f;
	double m_phaseStart = 0.0;
	QAngle m_angles;
	CUtlVector<float> m_frameMs;
	double m_sectionMs[NEO_HUD_PROFILE__COUNT] = {};
	double m_counts[NEO_HUD_COUNT__COUNT] = {};
	HudBenchResult m_results[HUD_BENCH_PASSES][HUD_BENCH_SCENARIOS];

	// What to put back afterwards.
	char m_savedStyle[16] = "";
	char m_savedBacking[16] = "";
	char m_savedDrawHud[16] = "";
	char m_savedFpsMax[16] = "";
	char m_savedTextBaked[16] = "";
};
static CNeoHudBench s_hudBench;

void CNeoHudBench::Command(const char *pszFormat, ...)
{
	char command[256];
	va_list args;
	va_start(args, pszFormat);
	V_vsnprintf(command, sizeof(command), pszFormat, args);
	va_end(args);
	engine->ClientCmd_Unrestricted(command);
}

// The first pass in order, the second in reverse.
int CNeoHudBench::ScenarioOf(int run) const
{
	const int i = run % HUD_BENCH_SCENARIOS;
	return (run / HUD_BENCH_SCENARIOS) % 2 ? HUD_BENCH_SCENARIOS - 1 - i : i;
}

void CNeoHudBench::Start(float measureSeconds, float settleSeconds)
{
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (m_phase != IDLE || !pPlayer || !pPlayer->IsAlive() || pPlayer->IsObserver())
	{
		Msg("neo_hud_bench: needs you alive in a game, and no benchmark running.\n");
		return;
	}
	m_measureSeconds = clamp(measureSeconds, 0.5f, 30.0f);
	m_settleSeconds = clamp(settleSeconds, 0.5f, 10.0f);
	const auto save = [](const char *pszName, char *pszOut, int size) {
		const ConVarRef var(pszName);
		V_strncpy(pszOut, var.IsValid() ? var.GetString() : "", size);
	};
	save("cl_neo_hud_style", m_savedStyle, sizeof(m_savedStyle));
	save("cl_neo_hud_backing", m_savedBacking, sizeof(m_savedBacking));
	save("cl_drawhud", m_savedDrawHud, sizeof(m_savedDrawHud));
	save("fps_max", m_savedFpsMax, sizeof(m_savedFpsMax));
	save("cl_neo_hud_text_baked", m_savedTextBaked, sizeof(m_savedTextBaked));
	m_angles = pPlayer->EyeAngles();

	const ConVarRef vsync("mat_vsync");
	if (vsync.IsValid() && vsync.GetBool())
	{
		Msg("neo_hud_bench: vsync is on, so frame times will sit at the refresh rate. Turn it off for real numbers.\n");
	}
	Command("fps_max 0");
	for (int p = 0; p < HUD_BENCH_PASSES; ++p)
	{
		for (int s = 0; s < HUD_BENCH_SCENARIOS; ++s)
		{
			m_results[p][s] = HudBenchResult();
		}
	}
	Msg("neo_hud_bench: %d runs of %.1f s (after %.1f s to settle). Hands off.\n", HUD_BENCH_PASSES * HUD_BENCH_SCENARIOS, m_measureSeconds,
		m_settleSeconds);
	m_run = 0;
	BeginRun();
}

void CNeoHudBench::BeginRun()
{
	// Back to the player's own settings, then the scenario's on top.
	Command("cl_drawhud 1; cl_neo_hud_style %s; cl_neo_hud_backing %s; cl_neo_hud_text_baked 0; %s", m_savedStyle,
		m_savedBacking, s_hudBenchScenarios[ScenarioOf(m_run)].commands);
	m_phase = SETTLE;
	m_phaseStart = Plat_FloatTime();
}

void CNeoHudBench::DropSamples()
{
	for (int i = 0; i < NEO_HUD_PROFILE__COUNT; ++i)
	{
		NeoHudProfileTakeMs(static_cast<NeoHudProfileSection>(i));
	}
	for (int i = 0; i < NEO_HUD_COUNT__COUNT; ++i)
	{
		NeoHudProfileTakeCount(static_cast<NeoHudCounter>(i));
	}
}

void CNeoHudBench::Update(float frametime)
{
	if (m_phase == IDLE)
	{
		DropSamples();	// so the counts never pile up between runs
		return;
	}
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (!pPlayer || !pPlayer->IsAlive())
	{
		Stop("the player died or left");
		return;
	}
	// The view held still, whatever the mouse does.
	engine->SetViewAngles(m_angles);

	const double now = Plat_FloatTime();
	if (m_phase == SETTLE)
	{
		DropSamples();
		if (now - m_phaseStart >= m_settleSeconds)
		{
			m_phase = MEASURE;
			m_phaseStart = now;
			m_frameMs.RemoveAll();
			for (double &ms : m_sectionMs)
				ms = 0.0;
			for (double &count : m_counts)
				count = 0.0;
		}
		return;
	}
	m_frameMs.AddToTail(gpGlobals->absoluteframetime * 1000.0f);
	for (int i = 0; i < NEO_HUD_PROFILE__COUNT; ++i)
	{
		m_sectionMs[i] += NeoHudProfileTakeMs(static_cast<NeoHudProfileSection>(i));
	}
	for (int i = 0; i < NEO_HUD_COUNT__COUNT; ++i)
	{
		m_counts[i] += NeoHudProfileTakeCount(static_cast<NeoHudCounter>(i));
	}
	if (now - m_phaseStart >= m_measureSeconds)
	{
		FinishRun();
	}
}

void CNeoHudBench::FinishRun()
{
	HudBenchResult &result = m_results[m_run / HUD_BENCH_SCENARIOS][ScenarioOf(m_run)];
	const int frames = m_frameMs.Count();
	if (frames > 0)
	{
		result.measured = true;
		result.frames = frames;
		double total = 0.0;
		for (int i = 0; i < frames; ++i)
		{
			total += m_frameMs[i];
		}
		result.frameMs = total / frames;
		m_frameMs.Sort([](const float *a, const float *b) { return (*a < *b) ? 1 : (*a > *b) ? -1 : 0; });
		const int worst = Max(1, frames / 100);
		double worstTotal = 0.0;
		for (int i = 0; i < worst; ++i)
		{
			worstTotal += m_frameMs[i];
		}
		result.worstMs = worstTotal / worst;
		for (int i = 0; i < NEO_HUD_PROFILE__COUNT; ++i)
		{
			result.sectionMs[i] = m_sectionMs[i] / frames;
		}
		for (int i = 0; i < NEO_HUD_COUNT__COUNT; ++i)
		{
			result.counts[i] = m_counts[i] / frames;
		}
	}
	if (++m_run >= HUD_BENCH_PASSES * HUD_BENCH_SCENARIOS)
	{
		Report();
		Restore();
		return;
	}
	BeginRun();
}

void CNeoHudBench::Stop(const char *pszWhy)
{
	if (m_phase == IDLE)
	{
		return;
	}
	Msg("neo_hud_bench: stopped (%s); settings put back.\n", pszWhy);
	Restore();
}

void CNeoHudBench::Restore()
{
	m_phase = IDLE;
	Command("cl_drawhud %s; cl_neo_hud_style %s; cl_neo_hud_backing %s; fps_max %s; cl_neo_hud_text_baked %s",
		m_savedDrawHud, m_savedStyle, m_savedBacking, m_savedFpsMax, m_savedTextBaked);
}

void CNeoHudBench::Report()
{
	MaterialAdapterInfo_t adapter;
	materials->GetDisplayAdapterInfo(materials->GetCurrentAdapter(), adapter);
	char map[MAX_PATH];
	V_FileBase(engine->GetLevelName(), map, sizeof(map));
	char system[256];
	V_snprintf(system, sizeof(system), "%s, %dx%d, dxlevel %d, map %s", adapter.m_pDriverName, ScreenWidth(), ScreenHeight(),
		g_pMaterialSystemHardwareConfig->GetDXSupportLevel(), map);

	// The two passes averaged; "vs none" against the no-HUD scenario; "gap" is how far the passes disagree (noise).
	HudBenchResult mean[HUD_BENCH_SCENARIOS];
	double gap[HUD_BENCH_SCENARIOS] = {};
	for (int s = 0; s < HUD_BENCH_SCENARIOS; ++s)
	{
		const HudBenchResult &a = m_results[0][s], &b = m_results[1][s];
		if (!a.measured || !b.measured)
		{
			continue;
		}
		HudBenchResult &m = mean[s];
		m.measured = true;
		m.frames = a.frames + b.frames;
		m.frameMs = 0.5 * (a.frameMs + b.frameMs);
		m.worstMs = 0.5 * (a.worstMs + b.worstMs);
		for (int i = 0; i < NEO_HUD_PROFILE__COUNT; ++i)
			m.sectionMs[i] = 0.5 * (a.sectionMs[i] + b.sectionMs[i]);
		for (int i = 0; i < NEO_HUD_COUNT__COUNT; ++i)
			m.counts[i] = 0.5 * (a.counts[i] + b.counts[i]);
		gap[s] = fabs(a.frameMs - b.frameMs);
	}
	const double none = mean[0].measured ? mean[0].frameMs : 0.0;

	Msg("\nneo_hud_bench results (%s).\n", system);
	Msg("Frame times in ms (two passes averaged); HUD CPU ms per frame by section; counts per frame.\n");
	char line[512];
	int used = V_snprintf(line, sizeof(line), "%-20s %7s %8s %7s %6s", "scenario", "frame", "vs none", "1%", "gap");
	for (int i = 0; i < NEO_HUD_PROFILE__COUNT && used < static_cast<int>(sizeof(line)); ++i)
		used += V_snprintf(line + used, sizeof(line) - used, " %9s", NeoHudProfileSectionName(static_cast<NeoHudProfileSection>(i)));
	for (int i = 0; i < NEO_HUD_COUNT__COUNT && used < static_cast<int>(sizeof(line)); ++i)
		used += V_snprintf(line + used, sizeof(line) - used, " %7s", NeoHudProfileCounterName(static_cast<NeoHudCounter>(i)));
	Msg("%s\n", line);

	time_t now = time(nullptr);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
	char path[MAX_PATH];
	V_snprintf(path, sizeof(path), "hud_bench/%s_%s.csv", map, stamp);
	g_pFullFileSystem->CreateDirHierarchy("hud_bench", "MOD");
	FileHandle_t file = g_pFullFileSystem->Open(path, "w", "MOD");
	if (file)
	{
		g_pFullFileSystem->FPrintf(file, "# %s\nscenario,frame_ms,vs_none_ms,worst_1pct_ms,pass_gap_ms", system);
		for (int i = 0; i < NEO_HUD_PROFILE__COUNT; ++i)
			g_pFullFileSystem->FPrintf(file, ",%s_cpu_ms", NeoHudProfileSectionName(static_cast<NeoHudProfileSection>(i)));
		for (int i = 0; i < NEO_HUD_COUNT__COUNT; ++i)
			g_pFullFileSystem->FPrintf(file, ",%s", NeoHudProfileCounterName(static_cast<NeoHudCounter>(i)));
		g_pFullFileSystem->FPrintf(file, "\n");
	}

	for (int s = 0; s < HUD_BENCH_SCENARIOS; ++s)
	{
		const HudBenchResult &m = mean[s];
		if (!m.measured)
		{
			Msg("%-20s (not measured)\n", s_hudBenchScenarios[s].label);
			continue;
		}
		used = V_snprintf(line, sizeof(line), "%-20s %7.2f %+8.2f %7.2f %6.2f", s_hudBenchScenarios[s].label, m.frameMs,
			m.frameMs - none, m.worstMs, gap[s]);
		for (int i = 0; i < NEO_HUD_PROFILE__COUNT && used < static_cast<int>(sizeof(line)); ++i)
			used += V_snprintf(line + used, sizeof(line) - used, " %9.3f", m.sectionMs[i]);
		for (int i = 0; i < NEO_HUD_COUNT__COUNT && used < static_cast<int>(sizeof(line)); ++i)
			used += V_snprintf(line + used, sizeof(line) - used, " %7.1f", m.counts[i]);
		Msg("%s\n", line);
		if (file)
		{
			g_pFullFileSystem->FPrintf(file, "%s,%.3f,%.3f,%.3f,%.3f", s_hudBenchScenarios[s].label, m.frameMs, m.frameMs - none,
				m.worstMs, gap[s]);
			for (int i = 0; i < NEO_HUD_PROFILE__COUNT; ++i)
				g_pFullFileSystem->FPrintf(file, ",%.4f", m.sectionMs[i]);
			for (int i = 0; i < NEO_HUD_COUNT__COUNT; ++i)
				g_pFullFileSystem->FPrintf(file, ",%.1f", m.counts[i]);
			g_pFullFileSystem->FPrintf(file, "\n");
		}
	}
	if (file)
	{
		g_pFullFileSystem->Close(file);
		Msg("Saved to %s (in the mod folder).\n", path);
	}
}

CON_COMMAND(neo_hud_bench, "Hands-off benchmark of the HUD styles (see neo_hud_bench.cpp). Usage:"
	" neo_hud_bench [measure seconds = 3] [settle seconds = 2]")
{
	s_hudBench.Start(args.ArgC() > 1 ? V_atof(args[1]) : 3.0f, args.ArgC() > 2 ? V_atof(args[2]) : 2.0f);
}

CON_COMMAND(neo_hud_bench_stop, "Stops a running HUD benchmark and puts the settings back.")
{
	s_hudBench.Stop("stopped by command");
}
