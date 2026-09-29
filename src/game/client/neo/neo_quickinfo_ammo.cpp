#include "cbase.h"
#include "neo_quickinfo_internal.h"
#include "c_neo_player.h"
#include "neo_gamerules.h"
#include "weapon_neobasecombatweapon.h"
#include "weapon_supa7.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The ammo readout, above the integrity bar (QUICKINFO.md): everything the ammo panel shows, since the band
// replaces that panel too. Top down: the weapon's name at the row's left edge and its magazines left at the right
// edge; then the round ticks centred, rounds left to their left and the fire mode to their right (where the old
// panel had it). The BALC's heat meter takes the ticks' row. The ghost and melee weapons: the name alone.

namespace NeoQuickInfo
{
constexpr float MAX_PITCH = 8.0f;		// a tick's spacing at most (fewer rounds spread no wider)
constexpr float SIDE_GAP = 12.0f;		// between the ticks and the text either side
constexpr float LOW = 0.2f;				// the ticks go amber at this much of a magazine left
constexpr float OVERHEAT = 0.8f;		// the BALC's meter goes amber this hot

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
		return;
	}
	ammo.pMode = pWeapon->IsAutomatic() ? L"AUTO" : L"SEMI";
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

// A magazine: a narrow upright box, its top cut at a slant, standing with its right side at x.
static void MagGlyph(const QuickFrame &f, float x, float y, const Color &c)
{
	const float w = 7.0f, h = 8.0f;
	Line(f, LAYER_BAR, x - w, y + h, x, y + h, NEO_GHOST_LIGHT, c, 0.9f);
	Line(f, LAYER_BAR, x, y + h, x, y - h, NEO_GHOST_LIGHT, c, 0.9f);
	Line(f, LAYER_BAR, x, y - h, x - w, y - h + 2.0f, NEO_GHOST_LIGHT, c, 0.9f);
	Line(f, LAYER_BAR, x - w, y - h + 2.0f, x - w, y + h, NEO_GHOST_LIGHT, c, 0.9f);
}

void PaintAmmo(const QuickFrame &f)
{
	const Ammo &a = f.ammo;
	if (!a.bShown)
	{
		return;
	}
	// The header: fixed anchors at the row's edges, whatever the magazine's size.
	Text(f, LAYER_BAR, a.name, V_wcslen(a.name), -TICK_HALF, HEADER_Y, 1, FONT_LARGE, f.color, 0.95f);
	if (a.bOverheated)
	{
		// The BALC's overheat reads where a magazine count would (it has none), clear of the wings.
		Text(f, LAYER_BAR, L"OVERHEAT", 8, TICK_HALF, HEADER_Y, -1, FONT_LARGE, WARN, 0.95f);
	}
	else if (a.mags[0])
	{
		const Color &c = a.bMagsOut ? WARN : f.color;
		const float wide = Text(f, LAYER_BAR, a.mags, V_wcslen(a.mags), TICK_HALF, HEADER_Y, -1, FONT_LARGE, c, 0.95f);
		MagGlyph(f, TICK_HALF - wide - 5.0f, HEADER_Y, c);
	}
	if (!a.bHeat && a.maxRounds <= 0)
	{
		NeoGhostFlush();
		return;
	}

	float edge = TICK_HALF;
	wchar_t left[16];
	bool bLow;
	if (a.bHeat)
	{
		// The BALC: its heat meter in the ticks' row, a quarter tick under it, amber when near overheating.
		bLow = a.heat >= OVERHEAT;
		const Color &c = bLow ? WARN : f.color;
		const float y0 = TICKS_Y - TICK_H * 0.5f, y1 = TICKS_Y + TICK_H * 0.5f;
		Line(f, LAYER_BAR, -TICK_HALF, y0, TICK_HALF, y0, NEO_GHOST_LIGHT, f.color, 0.6f);
		Line(f, LAYER_BAR, TICK_HALF, y0, TICK_HALF, y1, NEO_GHOST_LIGHT, f.color, 0.6f);
		Line(f, LAYER_BAR, TICK_HALF, y1, -TICK_HALF, y1, NEO_GHOST_LIGHT, f.color, 0.6f);
		Line(f, LAYER_BAR, -TICK_HALF, y1, -TICK_HALF, y0, NEO_GHOST_LIGHT, f.color, 0.6f);
		if (a.heat > 0.0f)
		{
			Box(f, LAYER_BAR, -TICK_HALF + 2.0f, y0 + 2.0f, -TICK_HALF + 2.0f + (TICK_HALF * 2.0f - 4.0f) * a.heat, y1 - 2.0f, c, 0.85f);
		}
		for (int k = 1; k < 4; ++k)
		{
			const float x = -TICK_HALF + k * TICK_HALF * 0.5f;
			Line(f, LAYER_BAR, x, y1 + 2.0f, x, y1 + 5.0f, NEO_GHOST_LIGHT, f.color, 0.4f);
		}
		V_snwprintf(left, ARRAYSIZE(left), L"%d%%", RoundFloatToInt(a.heat * 100.0f));
	}
	else
	{
		// A tick a round (several past MAX_TICKS), spent ones dark; amber when the magazine runs low.
		const int per = (a.maxRounds + MAX_TICKS - 1) / MAX_TICKS;
		const int n = (a.maxRounds + per - 1) / per, lit = (a.rounds + per - 1) / per;
		const float pitch = Min(MAX_PITCH, TICK_HALF * 2.0f / n), tick = Max(2.0f, pitch * 0.55f);
		bLow = a.maxRounds > 1 && a.rounds <= a.maxRounds * LOW;
		const Color &c = bLow ? WARN : f.color;
		edge = n * pitch * 0.5f;
		for (int i = 0; i < n; ++i)
		{
			const float x = -edge + i * pitch + (pitch - tick) * 0.5f;
			Box(f, LAYER_BAR, x, TICKS_Y - TICK_H * 0.5f, x + tick, TICKS_Y + TICK_H * 0.5f, (i < lit) ? c : f.color, (i < lit) ? 0.9f : 0.15f);
		}
		V_snwprintf(left, ARRAYSIZE(left), L"%d", a.rounds);
	}
	Text(f, LAYER_BAR, left, V_wcslen(left), -edge - SIDE_GAP, TICKS_Y, -1, FONT_SMALL, bLow ? WARN : f.color, 0.9f);
	if (a.pMode)
	{
		Text(f, LAYER_BAR, a.pMode, V_wcslen(a.pMode), edge + SIDE_GAP, TICKS_Y, 1, FONT_SMALL, f.color, 0.9f);
	}
	NeoGhostFlush();
}
} // namespace NeoQuickInfo
