#include "cbase.h"
#include "neo_hud_model_ammo.h"
#include "c_neo_player.h"
#include "neo_gamerules.h"
#include "weapon_neobasecombatweapon.h"
#include "weapon_supa7.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace NeoHud
{
void ReadAmmo(C_NEO_Player *pPlayer, Ammo &ammo)
{
	ammo = Ammo();
	auto *pWeapon = static_cast<C_NEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon());
	if (!pWeapon || (NEORules() && (NEORules()->GetHiddenHudElements() & NEO_HUD_ELEMENT_AMMO)))
	{
		return;
	}
	ammo.bShown = true;
	char name[ARRAYSIZE(ammo.name)];
	V_strcpy_safe(name, pWeapon->GetPrintName());
	V_strupr(name);
	V_UTF8ToUnicode(name, ammo.name, sizeof(ammo.name));
	const int bits = pWeapon->GetNeoWepBits();
	if (pWeapon->IsGhost() || pWeapon->IsMeleeWeapon())
	{
		return;
	}
	if (bits & NEO_WEP_THROWABLE)
	{
		// Grenades (and the detpack): a tick each, the ammo panel's own count.
		ammo.rounds = ammo.maxRounds = abs(pWeapon->m_iPrimaryAmmoCount.Get());
		ammo.pMode = L"THROW";
		// The stock panel's grenade glyphs (the detpack has none).
		ammo.bullet = (bits & NEO_WEP_FRAG_GRENADE) ? L'g' : (bits & NEO_WEP_SMOKE_GRENADE) ? L'f' : 0;
		return;
	}
	ammo.pMode = pWeapon->IsAutomatic() ? L"AUTO" : L"SEMI";
	ammo.bullet = static_cast<wchar_t>(static_cast<unsigned char>(pWeapon->GetNEOWpnData().szBulletCharacter[0]));
	if (bits & NEO_WEP_SUPA7)
	{
		ammo.pMode = static_cast<CWeaponSupa7 *>(pWeapon)->SlugLoaded() ? L"SLUG" : L"BUCK";
	}
	if (bits & NEO_WEP_BALC)
	{
		const int charge = pWeapon->GetPrimaryAmmoCount();
		ammo.bHeat = true;
		ammo.heat = clamp(1.0f - charge / static_cast<float>(Max(1, pWeapon->GetDefaultClip1())), 0.0f, 1.0f);
		ammo.bOverheated = charge == 0;
		return;
	}
	ammo.maxRounds = Max(0, pWeapon->GetMaxClip1());
	ammo.rounds = clamp(pWeapon->Clip1(), 0, ammo.maxRounds);
	if (ammo.maxRounds <= 0 || !pWeapon->UsesClipsForAmmo1() || (bits & NEO_WEP_DETPACK))
	{
		return;
	}
	// As the ammo panel counts them: magazines left (a part-used one counts), or the Supa 7's shells and slugs.
	const int reserve = pWeapon->m_iPrimaryAmmoCount;
	if (bits & NEO_WEP_SUPA7)
	{
		const int slugs = pWeapon->m_iSecondaryAmmoCount.Get();
		V_snwprintf(ammo.mags, ARRAYSIZE(ammo.mags), L"%d+%d", reserve, slugs);
		ammo.bMagsOut = reserve + slugs <= 0;
	}
	else
	{
		const int mags = static_cast<int>(ceilf(fabsf(static_cast<float>(reserve) / ammo.maxRounds)));
		V_snwprintf(ammo.mags, ARRAYSIZE(ammo.mags), L"%d", mags);
		ammo.bMagsOut = mags <= 0;
	}
}
} // namespace NeoHud
