#include "cbase.h"
#include "neo_hud_model_team.h"
#include "c_neo_player.h"
#include "c_team.h"
#include "c_playerresource.h"
#include "neo_gamerules.h"
#include "ui/neo_scoreboard.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace NeoHud
{
void TeamSides(int &left, int &right)
{
	static ConVarRef cl_neo_hud_team_swap_sides("cl_neo_hud_team_swap_sides");
	const int local = GetLocalPlayerTeam();
	const bool bOnTeam = local == TEAM_JINRAI || local == TEAM_NSF;
	left = (cl_neo_hud_team_swap_sides.GetBool() && bOnTeam) ? local : TEAM_JINRAI;
	right = left == TEAM_JINRAI ? TEAM_NSF : TEAM_JINRAI;
}

Color TeamColour(int team)
{
	return team == TEAM_JINRAI ? COLOR_NEO_GREEN : team == TEAM_NSF ? COLOR_NEO_BLUE : team == TEAM_SPECTATOR ? COLOR_NEO_ORANGE
		: COLOR_NEO_WHITE;
}

// The round's status: what the stock element puts under its box (its strings, as they are).
static const wchar_t *Status()
{
	const NeoRoundStatus status = NEORules()->GetRoundStatus();
	const bool bDoOrDie = NEORules()->RoundIsDoOrDie();
	if (status == NeoRoundStatus::Idle)
		return NEORules()->IsReadyUpEnabled() ? L"Waiting for players to ready up" : L"Waiting for players";
	if (status == NeoRoundStatus::Warmup)
		return L"Warmup";
	if (status == NeoRoundStatus::Countdown)
		return L"Match is starting...";
	if (NEORules()->RoundIsInSuddenDeath())
		return L"Sudden death";
	if (bDoOrDie)
		return L"Do or Die";
	if (NEORules()->RoundIsMatchPoint())
		return L"Match point";
	if (status == NeoRoundStatus::Pause || !(NEORules()->IsRoundPreRoundFreeze() || (g_pNeoScoreBoard && g_pNeoScoreBoard->IsVisible())))
		return L"";
	switch (NEORules()->GetGameType())
	{
	case NEO_GAME_TYPE_DM:	return L"";
	case NEO_GAME_TYPE_TDM:	return L"Score the most Points";
	case NEO_GAME_TYPE_CTG:	return L"Capture the Ghost";
	case NEO_GAME_TYPE_VIP:
		if (GetLocalPlayerTeam() == NEORules()->m_iEscortingTeam.Get())
			return NEORules()->GhostExists() ? L"VIP down, prevent Ghost capture" : L"Escort the VIP";
		return NEORules()->GhostExists() ? L"HVT down, secure the Ghost" : L"Eliminate the HVT";
	case NEO_GAME_TYPE_JGR:	return L"Control the Juggernaut";
	default:				return L"Await further orders";
	}
}

void ReadRound(RoundReadout &out)
{
	out = RoundReadout();
	const NeoRoundStatus status = NEORules()->GetRoundStatus();
	const int gameType = NEORules()->GetGameType();
	out.pStatus = Status();
	float left = NEORules()->GetRoundRemainingTime();
	// Exactly no time left means no time limit: nothing else, as the stock element.
	if (left == 0.0f)
		return;
	out.bTimed = true;
	left = Max(left, 0.0f);
	out.bPaused = status == NeoRoundStatus::Pause;
	if (out.bPaused)
	{
		V_wcsncpy(out.round, L"PAUSED", sizeof(out.round));
		left = NEORules()->m_flPauseEnd.Get() - gpGlobals->curtime;
	}
	else if (status == NeoRoundStatus::Countdown)
		V_wcsncpy(out.round, L"STARTING", sizeof(out.round));
	else if (status == NeoRoundStatus::Overtime)
		V_wcsncpy(out.round, L"OVERTIME", sizeof(out.round));
	else if (gameType == NEO_GAME_TYPE_DM)
		V_wcsncpy(out.round, L"DEATHMATCH", sizeof(out.round));
	else
		V_snwprintf(out.round, ARRAYSIZE(out.round), L"ROUND %02d", NEORules()->roundNumber());

	// The clock (the freeze's own, CTG's overtime), red as the stock one goes.
	if (status == NeoRoundStatus::PreRoundFreeze)
		left = NEORules()->GetRemainingPreRoundFreezeTime(true);
	const int secs = (status == NeoRoundStatus::Overtime && gameType == NEO_GAME_TYPE_CTG) ? RoundFloatToInt(NEORules()->GetCTGOverTime())
		: RoundFloatToInt(Max(left, 0.0f));
	V_snwprintf(out.clock, ARRAYSIZE(out.clock), L"%02d:%02d", secs / 60, secs % 60);
	static ConVarRef sv_neo_ctg_ghost_overtime_grace("sv_neo_ctg_ghost_overtime_grace");
	const bool bOvertime = status == NeoRoundStatus::Overtime;
	out.bRed = (status == NeoRoundStatus::PreRoundFreeze || status == NeoRoundStatus::Countdown || gameType == NEO_GAME_TYPE_CTG)
		? (bOvertime && NEORules()->GetRoundRemainingTime() < sv_neo_ctg_ghost_overtime_grace.GetFloat()) : bOvertime;

	TeamSides(out.teams[0], out.teams[1]);
	out.bTeamplay = NEORules()->IsTeamplay();
	for (int i = 0; i < 2 && out.bTeamplay; ++i)
	{
		C_Team *pTeam = GetGlobalTeam(out.teams[i]);
		out.won[i] = pTeam ? pTeam->GetRoundsWon() : 0;
	}
	if (gameType == NEO_GAME_TYPE_DM)
	{
		int highestTotal = 0, highestXP = 0;
		NEORules()->GetDMHighestScorers(&highestTotal, &highestXP);
		static ConVarRef sv_neo_dm_win_xp("sv_neo_dm_win_xp");
		if (sv_neo_dm_win_xp.GetInt() > 0)
			V_snwprintf(out.tally, ARRAYSIZE(out.tally), L"LEAD %d/%d", highestXP, sv_neo_dm_win_xp.GetInt());
		else
			V_snwprintf(out.tally, ARRAYSIZE(out.tally), L"LEAD %d", highestXP);
	}
	else if (gameType == NEO_GAME_TYPE_TDM || gameType == NEO_GAME_TYPE_JGR)
	{
		C_Team *pLeft = GetGlobalTeam(out.teams[0]), *pRight = GetGlobalTeam(out.teams[1]);
		V_snwprintf(out.tally, ARRAYSIZE(out.tally), L"%d : %d", pLeft ? pLeft->Get_Score() : 0, pRight ? pRight->Get_Score() : 0);
	}
	else if (g_PR)
	{
		// Players alive, as the stock "N vs M": the right side is everyone connected who isn't on the left.
		for (int i = 1; i <= gpGlobals->maxClients; ++i)
		{
			if (!g_PR->IsConnected(i))
				continue;
			const int team = g_PR->GetTeam(i);
			const int side = team == out.teams[0] ? 0 : 1;
			if (team == out.teams[0] || team == out.teams[1])
				++out.total[side];
			if (g_PR->IsAlive(i))
				++out.alive[side];
		}
		for (int i = 0; i < 2; ++i)
			out.total[i] = Max(out.total[i], out.alive[i]);
	}
}

int SquadOrder(SquadEntry out[MAX_PLAYERS])
{
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	const int team = GetLocalPlayerTeam(), self = GetLocalPlayerIndex();
	if (!g_PR || !pLocal || !NEORules()->IsTeamplay() || (team != TEAM_JINRAI && team != TEAM_NSF))
		return 0;
	static ConVarRef cl_neo_hud_scoreboard_hide_others("cl_neo_hud_scoreboard_hide_others");
	if (cl_neo_hud_scoreboard_hide_others.GetBool() && g_pNeoScoreBoard && g_pNeoScoreBoard->IsVisible())
		return 0;
	static ConVarRef sv_neo_bot_cmdr_enable("sv_neo_bot_cmdr_enable");
	const bool bCommander = sv_neo_bot_cmdr_enable.IsValid() && sv_neo_bot_cmdr_enable.GetBool();
	const int star = g_PR->GetStar(self);

	int commanded[MAX_PLAYERS], squad[MAX_PLAYERS], rest[MAX_PLAYERS];
	int nCommanded = 0, nSquad = 0, nRest = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		if (i == self || !g_PR->IsConnected(i) || g_PR->GetTeam(i) != team)
			continue;
		if (star != STAR_NONE && g_PR->GetStar(i) == star)
		{
			C_NEO_Player *pMate = ToNEOPlayer(UTIL_PlayerByIndex(i));
			if (bCommander && pMate && pMate->m_hCommandingPlayer.Get() == pLocal)
				commanded[nCommanded++] = i;
			else
				squad[nSquad++] = i;
		}
		else
		{
			rest[nRest++] = i;
		}
	}
	int count = 0;
	for (int i = 0; i < nCommanded; ++i)
		out[count++] = { commanded[i], false, true, false };
	for (int i = 0; i < nSquad; ++i)
		out[count++] = { squad[i], false, false, false };
	for (int i = 0; i < nRest; ++i)
		out[count++] = { rest[i], true, false, i == 0 && count > 0 };
	return count;
}

void SquadMateNames(int player, const char **ppName, const char **ppClass)
{
	C_NEO_Player *pMate = ToNEOPlayer(UTIL_PlayerByIndex(player));
	const char *pName;
	const char *pClass = GetNeoClassName(g_PR->GetClass(player));
	if (g_PR->IsAlive(player))
	{
		pName = pMate ? pMate->GetPlayerNameWithTakeoverContext(player) : g_PR->GetPlayerName(player);
	}
	else
	{
		C_NEO_Player *pImpersonator = pMate ? pMate->m_hSpectatorTakeoverPlayerImpersonatingMe.Get() : nullptr;
		pName = pImpersonator ? pImpersonator->GetPlayerName() : g_PR->GetPlayerName(player);
		if (pImpersonator)
			pClass = GetNeoClassName(pImpersonator->m_iClassBeforeTakeover);
	}
	*ppName = pName;
	*ppClass = pClass;
}
} // namespace NeoHud
