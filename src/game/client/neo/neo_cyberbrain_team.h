#pragma once

// The cyberbrain's team furniture, painted by the NHudCyberbrainTeam element: the score (neo_cyberbrain_score.cpp),
// the squad list (neo_cyberbrain_squad.cpp) and the kill feed (neo_cyberbrain_feed.cpp). Deliberately quiet: no
// attention depths, the same plates, crosses and faces as the receptor groups. Each carries everything its stock
// element shows (the parity inventory is in HUD-REDESIGN.md, "The rest of the UI: parity").

#include "neo_cyberbrain_internal.h"

class IGameEvent;

namespace NeoCyberbrain
{
// The left and right teams as the stock round state puts them (cl_neo_hud_team_swap_sides: yours on the left).
void TeamSides(int &left, int &right);
Color TeamColour(int team);

// The round, as the stock round state words it: read once, drawn by the cyberbrain's score and the Competitive HUD.
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
// gap) the rest of the team small; without it, your squad then the team. Returns the count; none when it's hidden.
struct SquadEntry { int player; bool bSmall, bCommanded, bGapBefore; };
int SquadOrder(SquadEntry out[MAX_PLAYERS]);

void PaintScore(const Frame &f);
// The squad list under the squad's star, top left; returns nothing drawn when the scoreboard hides it.
void PaintSquad(const Frame &f);

// The kill feed's own copy of the death notice's events (the stock one keeps its side effects). An entry is its
// segments in order: names (name face), words (label face) and NT's killfeed glyphs (icons face).
constexpr int FEED_MAX = 8, FEED_SEGMENTS = 12;
struct FeedSegment { wchar_t text[64]; Font font; Color color; };
struct FeedEntry { FeedSegment seg[FEED_SEGMENTS]; int count; float hide; bool bInvolved; };
// Retires the expired entries and returns the rest, oldest first; none while the scoreboard hides the feed.
int FeedEntries(const FeedEntry **ppEntries);
void FeedEvent(IGameEvent *pEvent);
void ResetFeed();
void PaintFeed(const Frame &f);
} // namespace NeoCyberbrain
