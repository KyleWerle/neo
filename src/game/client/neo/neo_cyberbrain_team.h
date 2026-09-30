#pragma once

// The cyberbrain's team furniture, painted by the NHudCyberbrainTeam element: the score (neo_cyberbrain_score.cpp),
// the squad list (neo_cyberbrain_squad.cpp) and the kill feed (neo_cyberbrain_feed.cpp). Deliberately quiet: no
// attention depths, the same plates, crosses and faces as the receptor groups. Each carries everything its stock
// element shows (the parity inventory is in HUD-REDESIGN.md, "The rest of the UI: parity").

#include "neo_cyberbrain_internal.h"
#include "neo_hud_model_team.h"
#include "neo_hud_model_feed.h"

namespace NeoCyberbrain
{
// What the match is and who's in it (neo_hud_model_team.h), and the kill feed (neo_hud_model_feed.h).
using NeoHud::TeamSides;
using NeoHud::TeamColour;
using NeoHud::RoundReadout;
using NeoHud::ReadRound;
using NeoHud::SquadEntry;
using NeoHud::SquadOrder;
using NeoHud::SquadMateNames;

void PaintScore(const Frame &f);
// The squad list under the squad's star, top left; returns nothing drawn when the scoreboard hides it.
void PaintSquad(const Frame &f);

void PaintFeed(const Frame &f);
} // namespace NeoCyberbrain
