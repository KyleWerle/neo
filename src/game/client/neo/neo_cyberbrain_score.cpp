#include "cbase.h"
#include "neo_cyberbrain_team.h"
#include "c_neo_player.h"
#include "c_team.h"
#include "c_playerresource.h"
#include "neo_gamerules.h"
#include "ui/neo_scoreboard.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The score, top centre: the round's plate, the clock, the rounds each team has won beside it over NT's team plates,
// and under it the mode's tally (players alive as pips in CTG and VIP, the points in TDM and JGR, the lead in DM); the
// round's status under that. Everything the stock round state shows (neo_hud_round_state.cpp's
// UpdateStateForNeoHudElementDraw, kept in step with it by hand), including its quirks: no time limit shows nothing
// but the status, and the clock goes red as the stock one does.

namespace NeoCyberbrain
{
constexpr float SCORE_Y = 16.0f, CLOCK_Y = 46.0f, TALLY_Y = 80.0f, STATUS_Y = 104.0f;
constexpr float TEAM_X = 118.0f, LOGO = 58.0f;
constexpr int MAX_PIPS = 16;

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

// NT's team plate (ts_jinrai / ts_nsf) as a faint square behind a score.
static void TeamPlate(const Frame &f, int team, const Vector2D &centre)
{
	static int s_textures[2] = { -1, -1 };
	const int slot = team == TEAM_NSF ? 1 : 0;
	if (s_textures[slot] < 0)
	{
		s_textures[slot] = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile(s_textures[slot], slot ? "vgui/ts_nsf" : "vgui/ts_jinrai", true, false);
	}
	NeoGhostFlush();
	const Color c = TeamColour(team);
	vgui::surface()->DrawSetTexture(s_textures[slot]);
	vgui::surface()->DrawSetColor(c.r(), c.g(), c.b(), Alpha(f, 0.3f));
	const float h = LOGO * 0.5f * f.s;
	vgui::surface()->DrawTexturedRect(RoundFloatToInt(centre.x - h), RoundFloatToInt(centre.y - h), RoundFloatToInt(centre.x + h),
		RoundFloatToInt(centre.y + h));
}

// Players alive as pips, growing outward from x (dir -1 left, 1 right); past the pips' room, the count.
static void AlivePips(const Frame &f, float x, float y, int dir, int alive, int total, const Color &c)
{
	if (total > MAX_PIPS)
	{
		wchar_t count[16];
		V_snwprintf(count, ARRAYSIZE(count), L"%d/%d", alive, total);
		Text(f, count, x, y, dir, FONT_VALUE, c, 0.9f);
		return;
	}
	for (int i = 0; i < total; ++i)
	{
		const float px = x + dir * (i * 11.0f) * f.s - (dir < 0 ? 7.0f * f.s : 0.0f);
		Rect(f, Vector2D(px, y - 3.5f * f.s), Vector2D(px + 7.0f * f.s, y + 3.5f * f.s), c, i < alive ? 0.9f : 0.15f);
	}
}

void PaintScore(const Frame &f)
{
	const float cx = f.centre.x, s = f.s;
	const NeoRoundStatus status = NEORules()->GetRoundStatus();
	const int gameType = NEORules()->GetGameType();
	float left = NEORules()->GetRoundRemainingTime();

	// The status, always (the stock element shows it even when there's no time limit).
	const wchar_t *pStatus = Status();
	if (pStatus[0])
		Text(f, pStatus, cx, STATUS_Y * s, 0, FONT_LABEL, f.color, 0.95f);
	// Exactly no time left means no time limit: nothing else, as the stock element.
	if (left == 0.0f)
		return;
	left = Max(left, 0.0f);

	// The round's plate.
	wchar_t round[32];
	if (status == NeoRoundStatus::Pause)
	{
		V_wcsncpy(round, L"PAUSED", sizeof(round));
		left = NEORules()->m_flPauseEnd.Get() - gpGlobals->curtime;
	}
	else if (status == NeoRoundStatus::Countdown)
		V_wcsncpy(round, L"STARTING", sizeof(round));
	else if (status == NeoRoundStatus::Overtime)
		V_wcsncpy(round, L"OVERTIME", sizeof(round));
	else if (gameType == NEO_GAME_TYPE_DM)
		V_wcsncpy(round, L"DEATHMATCH", sizeof(round));
	else
		V_snwprintf(round, ARRAYSIZE(round), L"ROUND %02d", NEORules()->roundNumber());
	if (status == NeoRoundStatus::Pause)
		Text(f, round, cx, SCORE_Y * s, 0, FONT_LABEL, CRIT, 1.0f);
	else
		Plate(f, round, cx, SCORE_Y * s, 0, 0.85f);

	// The clock (the freeze's own, CTG's overtime), red as the stock one goes.
	if (status == NeoRoundStatus::PreRoundFreeze)
		left = NEORules()->GetRemainingPreRoundFreezeTime(true);
	const int secs = (status == NeoRoundStatus::Overtime && gameType == NEO_GAME_TYPE_CTG) ? RoundFloatToInt(NEORules()->GetCTGOverTime())
		: RoundFloatToInt(Max(left, 0.0f));
	wchar_t clock[16];
	V_snwprintf(clock, ARRAYSIZE(clock), L"%02d:%02d", secs / 60, secs % 60);
	static ConVarRef sv_neo_ctg_ghost_overtime_grace("sv_neo_ctg_ghost_overtime_grace");
	const bool bOvertime = status == NeoRoundStatus::Overtime;
	const bool bRed = (status == NeoRoundStatus::PreRoundFreeze || status == NeoRoundStatus::Countdown || gameType == NEO_GAME_TYPE_CTG)
		? (bOvertime && NEORules()->GetRoundRemainingTime() < sv_neo_ctg_ghost_overtime_grace.GetFloat()) : bOvertime;
	Text(f, clock, cx, CLOCK_Y * s, 0, FONT_VALUE_LARGE, bRed ? CRIT : f.color, 1.0f);

	// The frame: registration crosses at the corners, a rule under the clock.
	Cross(f, Vector2D(cx - 172.0f * s, 6.0f * s), 5.0f * s, 0.45f);
	Cross(f, Vector2D(cx + 172.0f * s, 6.0f * s), 5.0f * s, 0.45f);
	Line(f, Vector2D(cx - 60.0f * s, 64.0f * s), Vector2D(cx + 60.0f * s, 64.0f * s), NEO_GHOST_LIGHT, f.color, 0.3f);

	int leftTeam, rightTeam;
	TeamSides(leftTeam, rightTeam);
	const bool bTeamplay = NEORules()->IsTeamplay();
	if (bTeamplay)
	{
		// The rounds each team has won.
		const int teams[2] = { leftTeam, rightTeam };
		for (int i = 0; i < 2; ++i)
		{
			const float x = cx + (i ? TEAM_X : -TEAM_X) * s;
			TeamPlate(f, teams[i], Vector2D(x, (CLOCK_Y - 2.0f) * s));
			C_Team *pTeam = GetGlobalTeam(teams[i]);
			wchar_t won[8];
			V_snwprintf(won, ARRAYSIZE(won), L"%d", pTeam ? pTeam->GetRoundsWon() : 0);
			Text(f, won, x, (CLOCK_Y - 2.0f) * s, 0, FONT_INTEGRITY, TeamColour(teams[i]), 0.95f);
		}
	}

	// The tally.
	wchar_t tally[32] = L"";
	if (gameType == NEO_GAME_TYPE_DM)
	{
		int highestTotal = 0, highestXP = 0;
		NEORules()->GetDMHighestScorers(&highestTotal, &highestXP);
		static ConVarRef sv_neo_dm_win_xp("sv_neo_dm_win_xp");
		if (sv_neo_dm_win_xp.GetInt() > 0)
			V_snwprintf(tally, ARRAYSIZE(tally), L"LEAD %d/%d", highestXP, sv_neo_dm_win_xp.GetInt());
		else
			V_snwprintf(tally, ARRAYSIZE(tally), L"LEAD %d", highestXP);
	}
	else if (gameType == NEO_GAME_TYPE_TDM || gameType == NEO_GAME_TYPE_JGR)
	{
		C_Team *pLeft = GetGlobalTeam(leftTeam), *pRight = GetGlobalTeam(rightTeam);
		V_snwprintf(tally, ARRAYSIZE(tally), L"%d : %d", pLeft ? pLeft->Get_Score() : 0, pRight ? pRight->Get_Score() : 0);
	}
	if (tally[0])
	{
		Text(f, tally, cx, TALLY_Y * s, 0, FONT_VALUE, f.color, 0.9f);
	}
	else if (g_PR)
	{
		// Players alive, as the stock "N vs M": the right side is everyone connected who isn't on the left.
		int alive[2] = {}, total[2] = {};
		for (int i = 1; i <= gpGlobals->maxClients; ++i)
		{
			if (!g_PR->IsConnected(i))
				continue;
			const int team = g_PR->GetTeam(i);
			const int side = team == leftTeam ? 0 : 1;
			if (team == leftTeam || team == rightTeam)
				++total[side];
			if (g_PR->IsAlive(i))
				++alive[side];
		}
		AlivePips(f, cx - 20.0f * s, TALLY_Y * s, -1, alive[0], Max(total[0], alive[0]), TeamColour(leftTeam));
		AlivePips(f, cx + 20.0f * s, TALLY_Y * s, 1, alive[1], Max(total[1], alive[1]), TeamColour(rightTeam));
		Text(f, L"VS", cx, TALLY_Y * s, 0, FONT_LABEL, f.color, 0.5f);
	}
}
} // namespace NeoCyberbrain
