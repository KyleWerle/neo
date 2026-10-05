#include "cbase.h"
#include "neo_view_shots.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static constexpr int MUZZLE_FLASH_MASK = (1 << EF_MUZZLEFLASH_BITS) - 1;

static struct
{
	NeoViewShots shots;
	int frame = -1;
	int lastParity = -1;
	int playerIndex = -1;
} s_viewShots;

C_NEO_Player *NeoViewPlayer()
{
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	if (pLocal && pLocal->IsObserver() && pLocal->GetObserverMode() == OBS_MODE_IN_EYE)
	{
		return dynamic_cast<C_NEO_Player *>(pLocal->GetObserverTarget());
	}
	return pLocal;
}

const NeoViewShots &NeoGetViewShots()
{
	if (s_viewShots.frame == gpGlobals->framecount)
	{
		return s_viewShots.shots;
	}
	s_viewShots.frame = gpGlobals->framecount;
	NeoViewShots &shots = s_viewShots.shots;

	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	C_NEO_Player *pPlayer = NeoViewPlayer();
	auto *pWeapon = pPlayer ? dynamic_cast<C_NEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon()) : nullptr;
	const int index = pPlayer ? pPlayer->entindex() : -1;
	const bool bSpectating = pPlayer && pPlayer != pLocal;
	const int parity = bSpectating ? pPlayer->GetMuzzleFlashParity() : -1;

	if (index != s_viewShots.playerIndex || bSpectating != shots.bSpectating)
	{
		s_viewShots.playerIndex = index;
		shots.viewChanged = gpGlobals->realtime;
	}
	else if (bSpectating && s_viewShots.lastParity >= 0 && parity != s_viewShots.lastParity)
	{
		// Several shots can land between two frames: the counter's difference, wrapped.
		shots.count += (parity - s_viewShots.lastParity) & MUZZLE_FLASH_MASK;
	}
	shots.hPlayer = pPlayer;
	shots.hWeapon = pWeapon;
	shots.bSpectating = bSpectating;
	s_viewShots.lastParity = parity;
	return shots;
}
