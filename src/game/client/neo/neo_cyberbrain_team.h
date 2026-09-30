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

void PaintScore(const Frame &f);
// The squad list under the squad's star, top left; returns nothing drawn when the scoreboard hides it.
void PaintSquad(const Frame &f);

// The kill feed's own copy of the death notice's events (the stock one keeps its side effects).
void FeedEvent(IGameEvent *pEvent);
void ResetFeed();
void PaintFeed(const Frame &f);
} // namespace NeoCyberbrain
