#include "cbase.h"
#include "neo_hud_bench.h"
#include "c_neo_player.h"
#include "igamesystem.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// neo_hud_bench_auto: the HUD bench with nobody at the keyboard (bench-hud.bat launches the game with it; PROFILING.md,
// "Running it unattended"). Armed from the command line before the map loads, it waits for the game, joins Jinrai as
// an assault with the first loadout (again every few seconds until alive), goes to neo_hud_bench_auto_view if one is
// set, runs neo_hud_bench, and with "quit" closes the game afterwards. A stopped run (a death, a respawn) is tried
// again, twice; three minutes without getting into the game gives up. Either way it leaves
// hud_bench/auto_status.txt saying how it ended, so the script can tell a finished run from a failed one.
//-----------------------------------------------------------------------------

ConVar neo_hud_bench_auto_view("neo_hud_bench_auto_view", "", FCVAR_ARCHIVE,
	"Commands that put you at the automatic HUD bench's view once alive, e.g. \"setpos x y z; setang p y 0\" (getpos on"
	" the spot prints them; needs sv_cheats). Empty: wherever you spawn.");

constexpr double AUTO_GIVE_UP = 180.0;	// seconds to get into the game
constexpr double AUTO_RETRY = 3.0;		// seconds between tries to join
constexpr double AUTO_SETTLE = 2.0;		// seconds alive (and at the view) before the bench starts
constexpr int AUTO_ATTEMPTS = 3;

class CNeoHudBenchAuto : public CAutoGameSystemPerFrame
{
public:
	CNeoHudBenchAuto() : CAutoGameSystemPerFrame("CNeoHudBenchAuto") {}

	void Arm(bool bQuit);
	void Update(float frametime) override;

private:
	enum Stage { OFF, JOIN, SETTLE, RUNNING };

	void Finish(const char *pszHow, bool bDone);

	Stage m_stage = OFF;
	bool m_bQuit = false;
	int m_attempts = 0;
	int m_reportsBefore = 0;
	double m_armed = 0.0, m_lastTry = 0.0, m_stageStart = 0.0;
};
static CNeoHudBenchAuto s_hudBenchAuto;

void CNeoHudBenchAuto::Arm(bool bQuit)
{
	m_stage = JOIN;
	m_bQuit = bQuit;
	m_attempts = 0;
	m_armed = Plat_FloatTime();
	m_lastTry = -100.0;
	g_pFullFileSystem->CreateDirHierarchy("hud_bench", "MOD");
	g_pFullFileSystem->RemoveFile("hud_bench/auto_status.txt", "MOD");
	Msg("neo_hud_bench_auto: armed; joining when the game is up%s.\n", bQuit ? ", quitting after" : "");
}

void CNeoHudBenchAuto::Finish(const char *pszHow, bool bDone)
{
	Msg("neo_hud_bench_auto: %s\n", pszHow);
	FileHandle_t file = g_pFullFileSystem->Open("hud_bench/auto_status.txt", "w", "MOD");
	if (file)
	{
		g_pFullFileSystem->FPrintf(file, "%s\n%s\n", bDone ? "done" : "failed", pszHow);
		g_pFullFileSystem->Close(file);
	}
	m_stage = OFF;
	if (m_bQuit)
		engine->ClientCmd_Unrestricted("quit");
}

void CNeoHudBenchAuto::Update(float frametime)
{
	if (m_stage == OFF)
		return;
	const double now = Plat_FloatTime();
	C_NEO_Player *pPlayer = engine->IsInGame() ? C_NEO_Player::GetLocalNEOPlayer() : nullptr;
	const bool bAlive = pPlayer && pPlayer->IsAlive() && !pPlayer->IsObserver();
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
			Finish("never got into the game (no spawn in three minutes)", false);
		}
		else if (pPlayer && now - m_lastTry > AUTO_RETRY)
		{
			m_lastTry = now;
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
			m_reportsBefore = NeoHudBenchReports();
			NeoHudBenchStart(3.0f, 2.0f);
			++m_attempts;
			m_stage = RUNNING;
		}
		break;
	case RUNNING:
		if (NeoHudBenchRunning())
			break;
		if (NeoHudBenchReports() > m_reportsBefore)
			Finish("done", true);
		else if (m_attempts < AUTO_ATTEMPTS)
			m_stage = JOIN;	// stopped (a death, a round restart): again once alive
		else
			Finish("the bench stopped every time it ran (see the console)", false);
		break;
	default:
		break;
	}
}

CON_COMMAND(neo_hud_bench_auto, "Runs neo_hud_bench with nobody at the keyboard: joins, spawns, benches. Usage:"
	" neo_hud_bench_auto [quit] (quit: close the game when it's done). See neo_hud_bench_auto.cpp.")
{
	s_hudBenchAuto.Arm(args.ArgC() > 1 && !V_stricmp(args[1], "quit"));
}
