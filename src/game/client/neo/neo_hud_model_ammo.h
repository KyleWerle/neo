#pragma once

// What the HUD knows of the active weapon (HUD-SYSTEM.md, the models layer): everything the stock ammo panel shows,
// read by whichever style draws it (the cyberbrain's weapon group, the racer band, Competitive). No painting.

class C_NEO_Player;

namespace NeoHud
{
// The active weapon, as the ammo panel shows it.
struct Ammo
{
	bool bShown = false;			// a weapon, and the rules don't hide the ammo
	wchar_t name[48] = L"";
	const wchar_t *pMode = nullptr;	// AUTO, SEMI, BUCK, SLUG, THROW; none for the ghost and melee
	int rounds = 0, maxRounds = 0;	// maxRounds 0: the name alone
	wchar_t bullet = 0;				// the stock panel's glyph for a round (NHudBullets, NOCR); 0: none (ticks)
	bool bHeat = false;				// the BALC: a heat meter in the ticks' row
	float heat = 0.0f;				// 0 to 1
	bool bOverheated = false;
	wchar_t mags[16] = L"";			// magazines left, or the Supa 7's shells + slugs; empty for none
	int magCount = 0, slugCount = 0;	// the same as numbers (the Supa 7: shells and slugs)
	bool bMagsOut = false;
};

void ReadAmmo(C_NEO_Player *pPlayer, Ammo &ammo);
} // namespace NeoHud
