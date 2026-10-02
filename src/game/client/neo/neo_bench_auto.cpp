#include "cbase.h"
#include "neo_hud_bench.h"
#include "neo_ironsight_bench.h"
#include "c_neo_player.h"
#include "igamesystem.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// neo_bench_auto: the benches with nobody at the keyboard (SourceDev\bench.py launches the game with it; PROFILING.md,
// "Running it unattended"). Armed from the command line before the map loads, it waits for the game, joins Jinrai as
// an assault with the first loadout (again every few seconds until alive), goes to neo_hud_bench_auto_view if one is
// set, then runs each part it was given, in order, in the one session:
//   hud      neo_hud_bench (no HUD, each style, the backings, baked text)
//   optics   neo_ironsight_bench optics (the sights and scopes, held still)
//   fire     neo_ironsight_bench fire (each gun family shooting, gunplay off and on)
// With "quit" it closes the game afterwards. A stopped part (a death, a respawn) is tried again, twice, then skipped;
// three minutes without getting into the game gives up. It leaves bench_auto/status.txt: "done" or "failed" on the
// first line, then a line per part, so the script knows which parts to file and which to run again.
//-----------------------------------------------------------------------------

ConVar neo_hud_bench_auto_view("neo_hud_bench_auto_view", "", FCVAR_ARCHIVE,
	"Commands that put you at the automatic bench's view once alive, e.g. \"setpos x y z; setang p y 0\" (getpos on"
	" the spot prints them; needs sv_cheats). Empty: wherever you spawn.");

constexpr double AUTO_GIVE_UP = 180.0;	// seconds to get into the game
constexpr double AUTO_RETRY = 3.0;		// seconds between tries to join
constexpr double AUTO_SETTLE = 1.0;		// seconds alive (and at the view) before each part starts
constexpr int AUTO_ATTEMPTS = 3;
constexpr int AUTO_MAX_PARTS = 8;
constexpr double AUTO_PART_BUDGET = 240.0;	// seconds one part may run before it's stopped (each takes 70 to 120)

enum BenchPart { PART_HUD, PART_OPTICS, PART_FIRE, PART__COUNT };
static const char *const s_partNames[PART__COUNT] = { "hud", "optics", "fire" };

// bench.py --quick sets these for shorter runs; 0 keeps each bench's own default.
static ConVar neo_bench_measure("neo_bench_measure", "0", FCVAR_NONE, "Automatic bench: seconds measured per run (0: each bench's default).");
static ConVar neo_bench_settle("neo_bench_settle", "0", FCVAR_NONE, "Automatic bench: seconds to settle before each run (0: each bench's default).");

static float Seconds(const ConVar &var, float fallback)
{
	return var.GetFloat() > 0.0f ? var.GetFloat() : fallback;
}

static bool PartRunning(BenchPart part)
{
	return part == PART_HUD ? NeoHudBenchRunning() : NeoIronsightBenchRunning();
}

static int PartReports(BenchPart part)
{
	return part == PART_HUD ? NeoHudBenchReports() : NeoIronsightBenchReports();
}

static void PartStop(BenchPart part)
{
	engine->ClientCmd_Unrestricted(part == PART_HUD ? "neo_hud_bench_stop" : "neo_ironsight_bench_stop");
}

static bool PartStart(BenchPart part)
{
	switch (part)
	{
	case PART_HUD:
		NeoHudBenchStart(Seconds(neo_bench_measure, 3.0f), Seconds(neo_bench_settle, 2.0f));
		return NeoHudBenchRunning();
	case PART_OPTICS:
		return NeoIronsightBenchStart("optics", Seconds(neo_bench_measure, 3.0f), Seconds(neo_bench_settle, 1.5f));
	case PART_FIRE:
		return NeoIronsightBenchStart("fire", Seconds(neo_bench_measure, 3.0f), Seconds(neo_bench_settle, 1.5f));
	default:
		return false;
	}
}

class CNeoBenchAuto : public CAutoGameSystemPerFrame
{
public:
	CNeoBenchAuto() : CAutoGameSystemPerFrame("CNeoBenchAuto") {}

	void Arm(const BenchPart *pParts, int count, bool bQuit);
	void Update(float frametime) override;

private:
	enum Stage { OFF, JOIN, SETTLE, RUNNING };

	void EndPart(const char *pszHow, bool bDone);
	void Finish(const char *pszHow);

	Stage m_stage = OFF;
	bool m_bQuit = false;
	BenchPart m_parts[AUTO_MAX_PARTS];
	int m_partCount = 0;
	int m_part = 0;
	int m_attempts = 0;
	int m_reportsBefore = 0;
	bool m_bAllDone = true;
	char m_log[512] = "";
	double m_armed = 0.0, m_lastTry = 0.0, m_stageStart = 0.0;
};
static CNeoBenchAuto s_benchAuto;

void CNeoBenchAuto::Arm(const BenchPart *pParts, int count, bool bQuit)
{
	m_partCount = Min(count, AUTO_MAX_PARTS);
	for (int i = 0; i < m_partCount; ++i)
	{
		m_parts[i] = pParts[i];
	}
	m_stage = JOIN;
	m_bQuit = bQuit;
	m_part = 0;
	m_attempts = 0;
	m_bAllDone = true;
	m_log[0] = '\0';
	m_armed = Plat_FloatTime();
	m_lastTry = -100.0;
	g_pFullFileSystem->CreateDirHierarchy("bench_auto", "MOD");
	g_pFullFileSystem->RemoveFile("bench_auto/status.txt", "MOD");
	char names[64] = "";
	for (int i = 0; i < m_partCount; ++i)
	{
		V_strncat(names, i ? " " : "", sizeof(names));
		V_strncat(names, s_partNames[m_parts[i]], sizeof(names));
	}
	Msg("neo_bench_auto: armed for %s; joining when the game is up%s.\n", names, bQuit ? ", quitting after" : "");
}

// One part over, finished or given up: note it, then on to the next.
void CNeoBenchAuto::EndPart(const char *pszHow, bool bDone)
{
	char line[128];
	V_snprintf(line, sizeof(line), "%s %s\n", s_partNames[m_parts[m_part]], pszHow);
	V_strncat(m_log, line, sizeof(m_log));
	Msg("neo_bench_auto: %s", line);
	m_bAllDone &= bDone;
	m_attempts = 0;
	if (++m_part >= m_partCount)
	{
		Finish(m_bAllDone ? "done" : "failed");
		return;
	}
	m_stage = SETTLE;
	m_stageStart = Plat_FloatTime();
}

void CNeoBenchAuto::Finish(const char *pszHow)
{
	Msg("neo_bench_auto: %s\n", pszHow);
	FileHandle_t file = g_pFullFileSystem->Open("bench_auto/status.txt", "w", "MOD");
	if (file)
	{
		g_pFullFileSystem->FPrintf(file, "%s\n%s", pszHow, m_log);
		g_pFullFileSystem->Close(file);
	}
	m_stage = OFF;
	if (m_bQuit)
	{
		Msg("neo_bench_auto: quitting.\n");
		engine->ClientCmd_Unrestricted("quit");
	}
}

void CNeoBenchAuto::Update(float frametime)
{
	if (m_stage == OFF)
		return;
	const double now = Plat_FloatTime();
	C_NEO_Player *pPlayer = engine->IsInGame() ? C_NEO_Player::GetLocalNEOPlayer() : nullptr;
	const bool bAlive = pPlayer && pPlayer->IsAlive() && !pPlayer->IsObserver();
	const BenchPart part = m_parts[m_part];
	switch (m_stage)
	{
	case JOIN:
		if (bAlive)
		{
			m_stage = SETTLE;
			m_stageStart = now;
			if (neo_hud_bench_auto_view.GetString()[0])
				engine->ClientCmd_Unrestricted(neo_hud_bench_auto_view.GetString());
		}
		else if (now - m_armed > AUTO_GIVE_UP)
		{
			V_strncat(m_log, "never got into the game (no spawn in three minutes)\n", sizeof(m_log));
			Finish("failed");
		}
		else if (pPlayer && now - m_lastTry > AUTO_RETRY)
		{
			m_lastTry = now;
			Msg("neo_bench_auto: waiting to spawn (%.0f s)\n", now - m_armed);
			// A team first, then a class and loadout for the next spawn.
			engine->ClientCmd_Unrestricted(pPlayer->GetTeamNumber() < TEAM_JINRAI ? "jointeam 2" : "setclass 2; loadout 0");
		}
		break;
	case SETTLE:
		if (!bAlive)
		{
			m_stage = JOIN;
		}
		else if (now - m_stageStart > AUTO_SETTLE)
		{
			m_reportsBefore = PartReports(part);
			++m_attempts;
			if (PartStart(part))
			{
				m_stage = RUNNING;
				m_stageStart = now;
			}
			else if (m_attempts >= AUTO_ATTEMPTS)
				EndPart("failed: it wouldn't start (see the console)", false);
			else
				m_stageStart = now;	// try again after another settle
		}
		break;
	case RUNNING:
		if (PartRunning(part) && now - m_stageStart > AUTO_PART_BUDGET)
		{
			PartStop(part);
			EndPart("failed: over its time budget, stopped (see the console for the last run)", false);
			break;
		}
		if (PartRunning(part))
			break;
		if (PartReports(part) > m_reportsBefore)
			EndPart("done", true);
		else if (m_attempts < AUTO_ATTEMPTS)
			m_stage = JOIN;	// stopped (a death, a round restart): again once alive
		else
			EndPart("failed: it stopped every time it ran (see the console)", false);
		break;
	default:
		break;
	}
}

CON_COMMAND(neo_bench_auto, "Runs benches with nobody at the keyboard: joins, spawns, runs each part in order. Usage:"
	" neo_bench_auto <hud|optics|fire>... [quit], or as one token: hud,optics,quit (the engine's command line only"
	" passes the first argument after +neo_bench_auto). quit: close the game when it's done. See neo_bench_auto.cpp.")
{
	BenchPart parts[AUTO_MAX_PARTS];
	int count = 0;
	bool bQuit = false;
	for (int i = 1; i < args.ArgC(); ++i)
	{
		CUtlStringList words;
		V_SplitString(args[i], ",", words);
		for (int w = 0; w < words.Count(); ++w)
		{
			const char *pszWord = words[w];
			if (!pszWord[0])
				continue;
			if (!V_stricmp(pszWord, "quit"))
			{
				bQuit = true;
				continue;
			}
			int found = -1;
			for (int p = 0; p < PART__COUNT; ++p)
			{
				if (!V_stricmp(pszWord, s_partNames[p]))
					found = p;
			}
			if (found < 0)
			{
				Msg("neo_bench_auto: no part called \"%s\" (hud, optics, fire).\n", pszWord);
				return;
			}
			if (count < AUTO_MAX_PARTS)
				parts[count++] = static_cast<BenchPart>(found);
		}
	}
	if (count == 0)
	{
		Msg("neo_bench_auto: name at least one part (hud, optics, fire).\n");
		return;
	}
	s_benchAuto.Arm(parts, count, bQuit);
}

CON_COMMAND(neo_hud_bench_auto, "Same as neo_bench_auto hud [quit]. Usage: neo_hud_bench_auto [quit]")
{
	const BenchPart part = PART_HUD;
	s_benchAuto.Arm(&part, 1, args.ArgC() > 1 && !V_stricmp(args[1], "quit"));
}
