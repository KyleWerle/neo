#include "cbase.h"
#include "neo_gunplay_shots.h"
#include "neo_ironsight_optic.h"
#include "neo_ironsight_profile.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static constexpr int MUZZLE_FLASH_MASK = (1 << EF_MUZZLEFLASH_BITS) - 1;

static struct
{
	NeoGunplayShots shots;
	int frame = -1;
	int lastClip = -1;
	int lastParity = -1;
	int playerIndex = -1;
	// Handles to what shots holds: an entity deleted since this frame's update (the gun replaced) clears its
	// handle, and the next caller updates again rather than get a pointer to freed memory.
	CHandle<C_NEO_Player> hPlayer;
	CHandle<C_NEOBaseCombatWeapon> hWeapon;
} s_watch;

const NeoGunplayShots &NeoGunplayWatchShots()
{
	if (s_watch.frame == gpGlobals->framecount && s_watch.hPlayer.Get() == s_watch.shots.pPlayer
		&& s_watch.hWeapon.Get() == s_watch.shots.pWeapon)
	{
		return s_watch.shots;
	}
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_SHOTS, "NeoGunplayWatchShots");
	s_watch.frame = gpGlobals->framecount;
	NeoGunplayShots &shots = s_watch.shots;
	const float now = gpGlobals->realtime;

	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	auto *pWeapon = pPlayer ? dynamic_cast<C_NEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon()) : nullptr;
	const int index = pPlayer ? pPlayer->entindex() : -1;
	const bool bSpectating = pPlayer && pPlayer != pLocal;
	const int clip = pWeapon ? pWeapon->Clip1() : -1;
	const int parity = pPlayer ? pPlayer->GetMuzzleFlashParity() : -1;

	if (index != s_watch.playerIndex || bSpectating != shots.bSpectating)
	{
		// A new view player: nothing to compare against yet.
		s_watch.playerIndex = index;
		shots.viewChanged = now;
	}
	else if (!bSpectating && pWeapon && pWeapon == shots.pWeapon && clip >= 0 && clip < s_watch.lastClip)
	{
		++shots.count;
		shots.lastShot = now;
	}
	else if (bSpectating && s_watch.lastParity >= 0 && parity != s_watch.lastParity)
	{
		// A few shots may land between two updates: the counter's difference, wrapped.
		shots.count += (parity - s_watch.lastParity) & MUZZLE_FLASH_MASK;
		shots.lastShot = now;
	}
	shots.pPlayer = pPlayer;
	shots.pWeapon = pWeapon;
	s_watch.hPlayer = pPlayer;
	s_watch.hWeapon = pWeapon;
	shots.bSpectating = bSpectating;
	s_watch.lastClip = clip;
	s_watch.lastParity = parity;
	return shots;
}
