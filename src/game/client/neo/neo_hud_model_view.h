#pragma once

// What's ahead of and around the view (HUD-SYSTEM.md, the models layer): the objective, as the stock compass finds it,
// and the range to what you look at, as the stock rangefinder traces it. The styles keep their own ways of showing
// them.

#include "mathlib/vector.h"

class C_NEO_Player;

namespace NeoHud
{
// The objective: the ghost, or the juggernaut's marker in JGR.
struct Objective
{
	Vector pos;
	int carrierTeam;	// who carries it: TEAM_JINRAI, TEAM_NSF, or anything else for nobody
	bool bYours;		// you carry it yourself
};
// False when there's none.
bool ReadObjective(C_NEO_Player *pPlayer, Objective &out);

// Metres from the view to what it looks at (MASK_SHOT, the whole trace length); false when that's the sky.
bool ReadRange(C_NEO_Player *pPlayer, float &metres);
} // namespace NeoHud
