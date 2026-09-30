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

void PaintScore(const Frame &f)
{
	const float cx = f.centre.x, s = f.s;
	RoundReadout r;
	ReadRound(r);
	if (r.pStatus[0])
		Text(f, r.pStatus, cx, STATUS_Y * s, 0, FONT_LABEL, f.color, 0.95f);
	if (!r.bTimed)
		return;
	if (r.bPaused)
		Text(f, r.round, cx, SCORE_Y * s, 0, FONT_LABEL, CRIT, 1.0f);
	else
		Plate(f, r.round, cx, SCORE_Y * s, 0, 0.85f);
	Text(f, r.clock, cx, CLOCK_Y * s, 0, FONT_VALUE_LARGE, r.bRed ? CRIT : f.color, 1.0f);

	// The frame: registration crosses at the corners, a rule under the clock.
	Cross(f, Vector2D(cx - 172.0f * s, 6.0f * s), 5.0f * s, 0.45f);
	Cross(f, Vector2D(cx + 172.0f * s, 6.0f * s), 5.0f * s, 0.45f);
	Line(f, Vector2D(cx - 60.0f * s, 64.0f * s), Vector2D(cx + 60.0f * s, 64.0f * s), NEO_GHOST_LIGHT, f.color, 0.3f);

	if (r.bTeamplay)
	{
		// The rounds each team has won.
		for (int i = 0; i < 2; ++i)
		{
			const float x = cx + (i ? TEAM_X : -TEAM_X) * s;
			TeamPlate(f, r.teams[i], Vector2D(x, (CLOCK_Y - 2.0f) * s));
			wchar_t won[8];
			V_snwprintf(won, ARRAYSIZE(won), L"%d", r.won[i]);
			Text(f, won, x, (CLOCK_Y - 2.0f) * s, 0, FONT_INTEGRITY, TeamColour(r.teams[i]), 0.95f);
		}
	}
	if (r.tally[0])
	{
		Text(f, r.tally, cx, TALLY_Y * s, 0, FONT_VALUE, f.color, 0.9f);
	}
	else if (r.total[0] + r.total[1] > 0)
	{
		AlivePips(f, cx - 20.0f * s, TALLY_Y * s, -1, r.alive[0], r.total[0], TeamColour(r.teams[0]));
		AlivePips(f, cx + 20.0f * s, TALLY_Y * s, 1, r.alive[1], r.total[1], TeamColour(r.teams[1]));
		Text(f, L"VS", cx, TALLY_Y * s, 0, FONT_LABEL, f.color, 0.5f);
	}
}
} // namespace NeoCyberbrain
