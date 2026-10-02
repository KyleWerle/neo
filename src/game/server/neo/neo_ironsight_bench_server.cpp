#include "cbase.h"
#include "neo_player.h"
#include "weapon_parse.h"
#include "ammodef.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Server side of the ironsight benchmark (client/neo/neo_ironsight_bench.cpp): the two things it can't do
// from the client with ordinary commands. Cheats only, for the player who runs them.

CON_COMMAND_F(neo_ironsight_bench_equip, "Ironsight benchmark helper: puts the named weapon in its slot (replacing"
	" what is there) and switches to it. Usage: neo_ironsight_bench_equip <weapon_name>", FCVAR_CHEAT)
{
	CNEO_Player *pPlayer = ToNEOPlayer(UTIL_GetCommandClient());
	if (!pPlayer || !pPlayer->IsAlive() || args.ArgC() < 2)
	{
		return;
	}
	const WEAPON_FILE_INFO_HANDLE handle = LookupWeaponInfoSlot(args[1]);
	if (handle == GetInvalidWeaponInfoHandle())
	{
		Warning("neo_ironsight_bench_equip: no weapon called %s\n", args[1]);
		return;
	}
	CBaseCombatWeapon *pWeapon = pPlayer->Weapon_OwnsThisType(args[1]);
	if (!pWeapon)
	{
		// Dropped as a pickup drops it (holstered, out of the hand first), then cleared away, so it doesn't lie in the
		// bench's view. Until 2026-10-01 it was detached and deleted in hand, which no game does.
		if (CBaseCombatWeapon *pInSlot = pPlayer->Weapon_GetSlot(GetFileWeaponInfoFromHandle(handle)->iSlot))
		{
			pPlayer->Weapon_Drop(pInSlot);
			if (pInSlot->GetOwner() == pPlayer)
			{
				pPlayer->Weapon_Detach(pInSlot);	// one that can't be dropped stays held
			}
			UTIL_Remove(pInSlot);
		}
		pPlayer->GiveNamedItem(args[1]);
		pWeapon = pPlayer->Weapon_OwnsThisType(args[1]);
	}
	if (pWeapon)
	{
		pPlayer->Weapon_Switch(pWeapon);
	}
}

CON_COMMAND_F(neo_ironsight_bench_refill, "Ironsight benchmark helper: fills the active weapon's clip once it is"
	" nearly empty, and its reserve. The fire set calls it while shooting. Usage: neo_ironsight_bench_refill", FCVAR_CHEAT)
{
	CNEO_Player *pPlayer = ToNEOPlayer(UTIL_GetCommandClient());
	CBaseCombatWeapon *pWeapon = pPlayer && pPlayer->IsAlive() ? pPlayer->GetActiveWeapon() : nullptr;
	if (!pWeapon || !pWeapon->UsesClipsForAmmo1())
	{
		return;
	}
	// Gunplay sees a shot as the clip going down (neo_gunplay_shots.cpp): fill it rarely, so few shots are missed.
	if (pWeapon->Clip1() <= pWeapon->GetMaxClip1() / 3)
	{
		pWeapon->m_iClip1 = pWeapon->GetMaxClip1();
	}
	if (pWeapon->GetPrimaryAmmoType() >= 0)
	{
		pPlayer->SetAmmoCount(GetAmmoDef()->MaxCarry(pWeapon->GetPrimaryAmmoType()), pWeapon->GetPrimaryAmmoType());
	}
}

CON_COMMAND_F(neo_ironsight_bench_class, "Ironsight benchmark helper: sets the class straight away, without a"
	" respawn. Usage: neo_ironsight_bench_class <recon|assault|support>", FCVAR_CHEAT)
{
	CNEO_Player *pPlayer = ToNEOPlayer(UTIL_GetCommandClient());
	if (!pPlayer || args.ArgC() < 2)
	{
		return;
	}
	if (V_stricmp(args[1], "recon") == 0)
	{
		pPlayer->SetClass(NEO_CLASS_RECON);
	}
	else if (V_stricmp(args[1], "assault") == 0)
	{
		pPlayer->SetClass(NEO_CLASS_ASSAULT);
	}
	else if (V_stricmp(args[1], "support") == 0)
	{
		pPlayer->SetClass(NEO_CLASS_SUPPORT);
	}
}
