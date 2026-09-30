#pragma once

// What the HUD knows of the match (HUD-SYSTEM.md, the models layer): the teams' sides and colours, the round as the
// stock round state words it, the squad list's order and each mate's name. No painting: the cyberbrain's team side
// and Competitive both read it, so both carry exactly what the stock elements do.

#include "Color.h"
#include "shareddefs.h"

namespace NeoHud
{
// The left and right teams as the stock round state puts them (cl_neo_hud_team_swap_sides: yours on the left).
void TeamSides(int &left, int &right);
Color TeamColour(int team);

// The round, as the stock round state words it (neo_hud_round_state.cpp's UpdateStateForNeoHudElementDraw, kept in
// step with it by hand), quirks included: no time limit shows nothing but the status, and the clock goes red as the
// stock one does.
struct RoundReadout
{
	const wchar_t *pStatus = L"";	// the status line (may be empty), shown even with no time limit
	bool bTimed = false;			// false: no time limit, so nothing but the status
	wchar_t round[32] = L"";		// ROUND nn, PAUSED, STARTING, OVERTIME, DEATHMATCH
	bool bPaused = false;
	wchar_t clock[16] = L"";
	bool bRed = false;				// the clock in red, exactly when the stock one is
	bool bTeamplay = false;
	int teams[2] = {};				// left, right
	int won[2] = {};				// rounds won
	wchar_t tally[32] = L"";		// DM's lead or TDM / JGR's points; empty: players alive instead
	int alive[2] = {}, total[2] = {};
};
void ReadRound(RoundReadout &out);

// The squad list's order, as the stock list: with the bot commander, the bots you command, your squad, then (after a
// gap) the rest of the team small; without it, your squad then the team. Returns the count; none when it's hidden
// (the scoreboard up, as the stock list hides).
struct SquadEntry { int player; bool bSmall, bCommanded, bGapBefore; };
int SquadOrder(SquadEntry out[MAX_PLAYERS]);

// A mate's name and class as the stock list shows them: alive, the name with a takeover's context; dead, whoever is
// impersonating them, with the class they had before the takeover.
void SquadMateNames(int player, const char **ppName, const char **ppClass);
} // namespace NeoHud
