#include "cbase.h"
#include "neo_ironsight_profile.h"
#include "neo_ironsight_bench.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "igamesystem.h"
#include "filesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include <time.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// neo_ironsight_bench: a hands-off benchmark of the ironsight features. Stand somewhere representative on a
// server you host (a busy view, no other players), type the command, and don't touch anything. It runs each
// scenario below twice, ironsights off and on, driving the weapon, class, aim, cloak and vision itself with
// ordinary commands (and two cheat helpers, neo_ironsight_bench_server.cpp), holds the view still, lets it
// settle, then measures real frame time (CPU and GPU together) and the per-feature CPU time from the
// NEO_IRONSIGHT_PROFILE scopes. It prints a table, writes a CSV under ironsight_bench/, and puts everything
// back. neo_ironsight_bench_stop aborts it.
//-----------------------------------------------------------------------------

struct BenchScenario
{
	const char *label;
	const char *weapon;
	const char *playerClass;
	bool aim;
	bool cloak;
	bool vision;
	bool fire = false;	// the trigger tapped at FIRE_TAP through settle and measure, the ammo topped up
};

// The cloak gets the fuller set; thermals, a lighter one.
static const BenchScenario s_scenarios[] = {
	{ "MX hip", "weapon_mx", "assault", false, false, false },
	{ "MX aimed", "weapon_mx", "assault", true, false, false },
	{ "MX aimed cloaked", "weapon_mx", "assault", true, true, false },
	{ "Jitte aimed", "weapon_jitte", "recon", true, false, false },
	{ "ZR68C aimed cloaked", "weapon_zr68c", "assault", true, true, false },
	{ "M41S aimed cloaked", "weapon_m41s", "assault", true, true, false },
	{ "MPN45 aimed", "weapon_mpn", "assault", true, false, false },
	{ "PZ aimed thermals", "weapon_pz", "support", true, false, true },
	{ "MX aimed thermals", "weapon_mx", "support", true, false, true },
};
// Shooting: what gunplay does per shot (knock, pivot, impact marks, the crosshair layers' pops) only runs while
// firing. One gun per crosshair family at the hip, and the sights and scopes aimed.
static const BenchScenario s_fireScenarios[] = {
	{ "ZR68C hip fire", "weapon_zr68c", "assault", false, false, false, true },
	{ "ZR68C aimed fire", "weapon_zr68c", "assault", true, false, false, true },
	{ "SRM hip fire", "weapon_srm", "assault", false, false, false, true },
	{ "PZ hip fire", "weapon_pz", "support", false, false, false, true },
	{ "Tachi hip fire", "weapon_tachi", "assault", false, false, false, true },
	{ "Supa7 hip fire", "weapon_supa7", "assault", false, false, false, true },
	{ "AA13 hip fire", "weapon_aa13", "assault", false, false, false, true },
	{ "MX aimed fire", "weapon_mx", "assault", true, false, false, true },
	{ "SRS aimed fire", "weapon_srs", "recon", true, false, false, true },
};
struct BenchSet
{
	const char *name;
	const BenchScenario *scenarios;
	int count;
};
static const BenchSet s_sets[] = {
	{ "optics", s_scenarios, ARRAYSIZE(s_scenarios) },
	{ "fire", s_fireScenarios, ARRAYSIZE(s_fireScenarios) },
};
// Cloak and vision off, to finish on.
static const BenchScenario s_restState = { "", "", "", false, false, false };
// Each scenario with ironsights off, then on.
static constexpr int BENCH_MAX_RUNS = 2 * (ARRAYSIZE(s_scenarios) > ARRAYSIZE(s_fireScenarios) ? ARRAYSIZE(s_scenarios) : ARRAYSIZE(s_fireScenarios));
static constexpr float FIRE_TAP = 0.06f;		// seconds the trigger is held, then let go: semi-autos fire every other tap
static constexpr float FIRE_REFILL = 0.25f;	// seconds between ammo top-ups (the server only fills a nearly empty clip)
static constexpr float BENCH_STATE_TIMEOUT = 4.0f;	// seconds to reach a scenario's state before skipping it
static constexpr float BENCH_TOGGLE_RETRY = 0.35f;	// seconds between presses of a toggle that didn't take

struct BenchResult
{
	bool measured = false;
	int frames = 0;
	double frameMs = 0.0;		// mean frame time
	double worstMs = 0.0;		// mean of the slowest 1% of frames
	double sectionMs[NEO_PROFILE__COUNT] = {};	// mean CPU time per frame, by feature
	double sectionCalls[NEO_PROFILE__COUNT] = {};	// mean times it ran per frame
};

static const char *ClassName(int neoClass)
{
	switch (neoClass)
	{
	case NEO_CLASS_RECON: return "recon";
	case NEO_CLASS_SUPPORT: return "support";
	default: return "assault";
	}
}

class CNeoIronsightBench : public CAutoGameSystemPerFrame
{
public:
	CNeoIronsightBench() : CAutoGameSystemPerFrame("CNeoIronsightBench") {}

	bool Start(const char *pszSet, float measureSeconds, float settleSeconds);
	void Stop(const char *pszWhy);
	void Update(float frametime) override;
	bool Running() const { return m_phase != IDLE; }
	int Reports() const { return m_reports; }

private:
	enum Phase { IDLE, SETUP, SETTLE, MEASURE, RESTORE };

	void Command(const char *pszFormat, ...);
	void BeginRun();
	bool StateReached(C_NEO_Player *pPlayer, const BenchScenario &scenario, bool bHolding) const;
	void PressToggles(C_NEO_Player *pPlayer, const BenchScenario &scenario);
	void FinishRun();
	void Report();
	void BeginRestore();
	void FinishRestore();
	void Fire(const BenchScenario &scenario);
	const BenchScenario &Scenario() const { return m_pSet->scenarios[m_run / 2]; }

	Phase m_phase = IDLE;
	const BenchSet *m_pSet = &s_sets[0];
	int m_runs = 0;
	int m_run = 0;
	int m_reports = 0;
	bool m_bTrigger = false;
	double m_lastTap = 0.0, m_lastRefill = 0.0;
	double m_runStart = 0.0;
	float m_measureSeconds = 3.0f;
	float m_settleSeconds = 1.5f;
	double m_phaseStart = 0.0;
	double m_lastToggle = 0.0;
	const char *m_pszRelease = nullptr;	// a toggle held down this frame, released next frame
	QAngle m_angles;
	CUtlVector<float> m_frameMs;
	double m_sectionMs[NEO_PROFILE__COUNT] = {};
	int m_sectionCalls[NEO_PROFILE__COUNT] = {};
	BenchResult m_results[BENCH_MAX_RUNS];

	// What to put back afterwards.
	char m_savedIronsights[16] = "";
	char m_savedGunplay[16] = "";
	char m_savedFpsMax[16] = "";
	char m_savedCheats[16] = "";
	char m_savedInfiniteCloak[16] = "";
	char m_savedClass[16] = "";
	char m_savedWeapons[MAX_WEAPONS][MAX_WEAPON_STRING] = {};
	char m_savedActive[MAX_WEAPON_STRING] = "";
};
static CNeoIronsightBench s_bench;

void CNeoIronsightBench::Command(const char *pszFormat, ...)
{
	char command[256];
	va_list args;
	va_start(args, pszFormat);
	V_vsnprintf(command, sizeof(command), pszFormat, args);
	va_end(args);
	engine->ClientCmd_Unrestricted(command);
}

bool CNeoIronsightBench::Start(const char *pszSet, float measureSeconds, float settleSeconds)
{
	const BenchSet *pSet = nullptr;
	for (const BenchSet &set : s_sets)
	{
		if (!V_stricmp(set.name, pszSet))
		{
			pSet = &set;
		}
	}
	if (!pSet)
	{
		Msg("neo_ironsight_bench: no set called \"%s\" (optics, fire).\n", pszSet);
		return false;
	}
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (m_phase != IDLE || !pPlayer || !pPlayer->IsAlive() || pPlayer->IsObserver())
	{
		Msg("neo_ironsight_bench: needs you alive in a game you host, and no benchmark running.\n");
		return false;
	}
	m_pSet = pSet;
	m_runs = pSet->count * 2;
	m_measureSeconds = clamp(measureSeconds, 0.5f, 30.0f);
	m_settleSeconds = clamp(settleSeconds, 0.5f, 10.0f);

	// Save what the run changes.
	const auto save = [](const char *pszName, char *pszOut, int size) {
		const ConVarRef var(pszName);
		V_strncpy(pszOut, var.IsValid() ? var.GetString() : "", size);
	};
	save("cl_neo_ironsights", m_savedIronsights, sizeof(m_savedIronsights));
	save("cl_neo_gunplay", m_savedGunplay, sizeof(m_savedGunplay));
	save("fps_max", m_savedFpsMax, sizeof(m_savedFpsMax));
	save("sv_cheats", m_savedCheats, sizeof(m_savedCheats));
	save("sv_neo_infinite_cloak", m_savedInfiniteCloak, sizeof(m_savedInfiniteCloak));
	V_strncpy(m_savedClass, ClassName(pPlayer->GetClass()), sizeof(m_savedClass));
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		C_BaseCombatWeapon *pWeapon = pPlayer->GetWeapon(i);
		V_strncpy(m_savedWeapons[i], pWeapon ? pWeapon->GetClassname() : "", sizeof(m_savedWeapons[i]));
	}
	C_BaseCombatWeapon *pActive = pPlayer->GetActiveWeapon();
	V_strncpy(m_savedActive, pActive ? pActive->GetClassname() : "", sizeof(m_savedActive));
	m_angles = pPlayer->EyeAngles();

	const ConVarRef vsync("mat_vsync");
	if (vsync.IsValid() && vsync.GetBool())
	{
		Msg("neo_ironsight_bench: vsync is on, so frame times will sit at the refresh rate. Turn it off for real numbers.\n");
	}
	Command("sv_cheats 1; sv_neo_infinite_cloak 1; fps_max 0");
	for (int i = 0; i < BENCH_MAX_RUNS; ++i)
	{
		m_results[i] = BenchResult();
	}
	Msg("neo_ironsight_bench: %s, %d runs of %.1f s (after %.1f s to settle). Hands off.\n", m_pSet->name, m_runs,
		m_measureSeconds, m_settleSeconds);
	m_run = 0;
	BeginRun();
	return true;
}

void CNeoIronsightBench::BeginRun()
{
	const BenchScenario &scenario = Scenario();
	const bool bIronsights = (m_run % 2) == 1;
	Msg("neo_ironsight_bench: run %d/%d, %s, gunplay %s\n", m_run + 1, m_runs, scenario.label, bIronsights ? "on" : "off");
	Command("-attack; -aim; cl_neo_gunplay %d; cl_neo_ironsights 1; neo_ironsight_bench_class %s; neo_ironsight_bench_equip %s",
		bIronsights ? 1 : 0, scenario.playerClass, scenario.weapon);
	if (scenario.aim)
	{
		Command("+aim");
	}
	m_bTrigger = false;
	m_phase = SETUP;
	m_phaseStart = m_runStart = Plat_FloatTime();
	m_lastToggle = 0.0;
}

// The trigger tapped on a fixed beat (automatics fire a few rounds a tap, semi-autos one), the clip kept from running dry.
void CNeoIronsightBench::Fire(const BenchScenario &scenario)
{
	if (!scenario.fire || (m_phase != SETTLE && m_phase != MEASURE))
	{
		if (m_bTrigger)
		{
			Command("-attack");
			m_bTrigger = false;
		}
		return;
	}
	const double now = Plat_FloatTime();
	if (now - m_lastTap >= FIRE_TAP)
	{
		m_bTrigger = !m_bTrigger;
		Command(m_bTrigger ? "+attack" : "-attack");
		m_lastTap = now;
	}
	if (now - m_lastRefill >= FIRE_REFILL)
	{
		Command("neo_ironsight_bench_refill");
		m_lastRefill = now;
	}
}

// bHolding: already settling or measuring. Firing can drop the aim for a moment (a bolt action cycling out of the
// scope), so a fire scenario only needs the aim to get going, not to keep it.
bool CNeoIronsightBench::StateReached(C_NEO_Player *pPlayer, const BenchScenario &scenario, bool bHolding) const
{
	C_BaseCombatWeapon *pActive = pPlayer->GetActiveWeapon();
	const bool bAimOk = (bHolding && scenario.fire) || pPlayer->IsInAim() == scenario.aim;
	return pActive && V_stricmp(pActive->GetClassname(), scenario.weapon) == 0
		&& V_stricmp(ClassName(pPlayer->GetClass()), scenario.playerClass) == 0
		&& bAimOk && pPlayer->IsCloaked() == scenario.cloak
		&& pPlayer->IsInVision() == scenario.vision;
}

// Cloak and vision are toggles: press one that's wrong, and again later if it didn't take.
void CNeoIronsightBench::PressToggles(C_NEO_Player *pPlayer, const BenchScenario &scenario)
{
	const double now = Plat_FloatTime();
	if (m_pszRelease || now - m_lastToggle < BENCH_TOGGLE_RETRY)
	{
		return;
	}
	if (pPlayer->IsCloaked() != scenario.cloak)
	{
		Command("+thermoptic");
		m_pszRelease = "-thermoptic";
		m_lastToggle = now;
	}
	else if (pPlayer->IsInVision() != scenario.vision)
	{
		Command("+vision");
		m_pszRelease = "-vision";
		m_lastToggle = now;
	}
}

void CNeoIronsightBench::Update(float frametime)
{
	// A toggle pressed last frame is let go now, so the game sees a whole press.
	if (m_pszRelease)
	{
		Command(m_pszRelease);
		m_pszRelease = nullptr;
	}
	if (m_phase == IDLE)
	{
		return;
	}
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (!pPlayer || !pPlayer->IsAlive())
	{
		m_phase = IDLE;
		Msg("neo_ironsight_bench: stopped (the player died or left); settings put back.\n");
		FinishRestore();
		return;
	}
	if (m_phase == RESTORE)
	{
		// Cloak and vision off first (a support can't uncloak), then everything else.
		if ((pPlayer->IsCloaked() || pPlayer->IsInVision()) && Plat_FloatTime() - m_phaseStart < BENCH_STATE_TIMEOUT)
		{
			PressToggles(pPlayer, s_restState);
			return;
		}
		FinishRestore();
		m_phase = IDLE;
		return;
	}
	// The view held still, whatever the mouse does.
	engine->SetViewAngles(m_angles);

	const BenchScenario &scenario = Scenario();
	const double now = Plat_FloatTime();
	const double elapsed = now - m_phaseStart;
	// Never stuck on one run: a state that keeps slipping (back to setup, again and again) is skipped.
	if (m_phase != MEASURE && now - m_runStart > m_settleSeconds + m_measureSeconds + 3 * BENCH_STATE_TIMEOUT)
	{
		Msg("neo_ironsight_bench: \"%s\" kept slipping out of its state, skipped.\n", scenario.label);
		FinishRun();
		return;
	}
	switch (m_phase)
	{
	case SETUP:
		PressToggles(pPlayer, scenario);
		if (StateReached(pPlayer, scenario, false))
		{
			m_phase = SETTLE;
			m_phaseStart = now;
		}
		else if (elapsed > BENCH_STATE_TIMEOUT)
		{
			Msg("neo_ironsight_bench: couldn't reach \"%s\", skipped.\n", scenario.label);
			FinishRun();
		}
		break;
	case SETTLE:
		if (!StateReached(pPlayer, scenario, true))
		{
			m_phase = SETUP;
		}
		else if (elapsed >= m_settleSeconds * (m_run == 0 ? 4.0f : 1.0f))	// the first run also warms up (materials, sounds)
		{
			m_phase = MEASURE;
			m_phaseStart = now;
			m_frameMs.RemoveAll();
			for (int i = 0; i < NEO_PROFILE__COUNT; ++i)
			{
				m_sectionMs[i] = 0.0;
				m_sectionCalls[i] = 0;
			}
		}
		break;
	case MEASURE:
		m_frameMs.AddToTail(gpGlobals->absoluteframetime * 1000.0f);
		for (int i = 0; i < NEO_PROFILE__COUNT; ++i)
		{
			m_sectionMs[i] += NeoIronsightProfileTakeMs(static_cast<NeoIronsightProfileSection>(i));
			m_sectionCalls[i] += NeoIronsightProfileTakeCalls(static_cast<NeoIronsightProfileSection>(i));
		}
		if (elapsed >= m_measureSeconds)
		{
			FinishRun();
		}
		break;
	default:
		break;
	}
	if (m_phase != IDLE && m_phase != RESTORE)
	{
		Fire(Scenario());
	}
	// Outside measurement the per-feature times are dropped.
	if (m_phase != MEASURE)
	{
		for (int i = 0; i < NEO_PROFILE__COUNT; ++i)
		{
			NeoIronsightProfileTakeMs(static_cast<NeoIronsightProfileSection>(i));
			NeoIronsightProfileTakeCalls(static_cast<NeoIronsightProfileSection>(i));
		}
	}
}

void CNeoIronsightBench::FinishRun()
{
	BenchResult &result = m_results[m_run];
	const int frames = m_frameMs.Count();
	if (m_phase == MEASURE && frames > 0)
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
		for (int i = 0; i < NEO_PROFILE__COUNT; ++i)
		{
			result.sectionMs[i] = m_sectionMs[i] / frames;
			result.sectionCalls[i] = static_cast<double>(m_sectionCalls[i]) / frames;
		}
	}
	if (++m_run >= m_runs)
	{
		Report();
		BeginRestore();
		return;
	}
	BeginRun();
}

void CNeoIronsightBench::Stop(const char *pszWhy)
{
	if (m_phase == IDLE || m_phase == RESTORE)
	{
		return;
	}
	Msg("neo_ironsight_bench: stopped (%s).\n", pszWhy);
	BeginRestore();
}

void CNeoIronsightBench::BeginRestore()
{
	Command("-attack; -aim");
	m_bTrigger = false;
	m_phase = RESTORE;
	m_phaseStart = Plat_FloatTime();
	m_lastToggle = 0.0;
}

void CNeoIronsightBench::FinishRestore()
{
	Command("neo_ironsight_bench_class %s", m_savedClass);
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		if (m_savedWeapons[i][0])
		{
			Command("neo_ironsight_bench_equip %s", m_savedWeapons[i]);
		}
	}
	if (m_savedActive[0])
	{
		Command("neo_ironsight_bench_equip %s", m_savedActive);
	}
	Command("cl_neo_gunplay %s; cl_neo_ironsights %s; fps_max %s; sv_neo_infinite_cloak %s; sv_cheats %s", m_savedGunplay,
		m_savedIronsights, m_savedFpsMax, m_savedInfiniteCloak, m_savedCheats);
}

void CNeoIronsightBench::Report()
{
	MaterialAdapterInfo_t adapter;
	materials->GetDisplayAdapterInfo(materials->GetCurrentAdapter(), adapter);
	char map[MAX_PATH];
	V_FileBase(engine->GetLevelName(), map, sizeof(map));
	char system[256];
	V_snprintf(system, sizeof(system), "%s, %dx%d, dxlevel %d, map %s", adapter.m_pDriverName, ScreenWidth(), ScreenHeight(),
		g_pMaterialSystemHardwareConfig->GetDXSupportLevel(), map);

	Msg("\nneo_ironsight_bench %s results (%s).\n", m_pSet->name, system);
	Msg("Frame times in ms; cost = ironsights on minus off. Per feature: CPU ms per frame with ironsights on, x calls per frame.\n");
	char line[512];
	int used = V_snprintf(line, sizeof(line), "%-22s %8s %8s %8s %9s %9s", "scenario", "off", "on", "cost", "1% off", "1% on");
	for (int s = 0; s < NEO_PROFILE__COUNT && used < static_cast<int>(sizeof(line)); ++s)
	{
		used += V_snprintf(line + used, sizeof(line) - used, " %15s", NeoIronsightProfileSectionName(static_cast<NeoIronsightProfileSection>(s)));
	}
	Msg("%s\n", line);

	time_t now = time(nullptr);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
	char path[MAX_PATH];
	V_snprintf(path, sizeof(path), "ironsight_bench/%s_%s_%s.csv", m_pSet->name, map, stamp);
	g_pFullFileSystem->CreateDirHierarchy("ironsight_bench", "MOD");
	FileHandle_t file = g_pFullFileSystem->Open(path, "w", "MOD");
	if (file)
	{
		g_pFullFileSystem->FPrintf(file, "# %s, set %s\nscenario,off_ms,on_ms,cost_ms,off_1pct_ms,on_1pct_ms", system, m_pSet->name);
		for (int s = 0; s < NEO_PROFILE__COUNT; ++s)
		{
			const char *pszName = NeoIronsightProfileSectionName(static_cast<NeoIronsightProfileSection>(s));
			g_pFullFileSystem->FPrintf(file, ",%s_ms,%s_calls", pszName, pszName);
		}
		g_pFullFileSystem->FPrintf(file, "\n");
	}

	for (int i = 0; i < m_pSet->count; ++i)
	{
		const BenchScenario &scenario = m_pSet->scenarios[i];
		const BenchResult &off = m_results[i * 2], &on = m_results[i * 2 + 1];
		if (!off.measured || !on.measured)
		{
			Msg("%-22s (not measured)\n", scenario.label);
			continue;
		}
		used = V_snprintf(line, sizeof(line), "%-22s %8.2f %8.2f %+8.2f %9.2f %9.2f", scenario.label, off.frameMs, on.frameMs,
			on.frameMs - off.frameMs, off.worstMs, on.worstMs);
		for (int s = 0; s < NEO_PROFILE__COUNT && used < static_cast<int>(sizeof(line)); ++s)
		{
			used += V_snprintf(line + used, sizeof(line) - used, " %8.3f x%-5.1f", on.sectionMs[s], on.sectionCalls[s]);
		}
		Msg("%s\n", line);
		if (file)
		{
			g_pFullFileSystem->FPrintf(file, "%s,%.3f,%.3f,%.3f,%.3f,%.3f", scenario.label, off.frameMs, on.frameMs,
				on.frameMs - off.frameMs, off.worstMs, on.worstMs);
			for (int s = 0; s < NEO_PROFILE__COUNT; ++s)
			{
				g_pFullFileSystem->FPrintf(file, ",%.4f,%.2f", on.sectionMs[s], on.sectionCalls[s]);
			}
			g_pFullFileSystem->FPrintf(file, "\n");
		}
	}
	if (file)
	{
		g_pFullFileSystem->Close(file);
		Msg("Saved to %s (in the mod folder).\n", path);
	}
	++m_reports;
}

bool NeoIronsightBenchRunning() { return s_bench.Running(); }
bool NeoIronsightBenchStart(const char *pszSet, float measureSeconds, float settleSeconds)
{
	return s_bench.Start(pszSet, measureSeconds, settleSeconds);
}
int NeoIronsightBenchReports() { return s_bench.Reports(); }

CON_COMMAND(neo_ironsight_bench, "Hands-off benchmark of the ironsight features (see neo_ironsight_bench.cpp). Usage:"
	" neo_ironsight_bench [optics|fire = optics] [measure seconds = 3] [settle seconds = 1.5]")
{
	int arg = 1;
	const char *pszSet = "optics";
	if (args.ArgC() > 1 && !V_isdigit(args[1][0]) && args[1][0] != '.')
	{
		pszSet = args[1];
		arg = 2;
	}
	s_bench.Start(pszSet, args.ArgC() > arg ? V_atof(args[arg]) : 3.0f, args.ArgC() > arg + 1 ? V_atof(args[arg + 1]) : 1.5f);
}

CON_COMMAND(neo_ironsight_bench_stop, "Stops a running ironsight benchmark and puts everything back.")
{
	s_bench.Stop("stopped by command");
}
