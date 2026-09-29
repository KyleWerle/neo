#include "cbase.h"
#include "neo_gunplay_reserve.h"
#include "neo_crosshair_family.h"
#include "weapon_neobasecombatweapon.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_crosshair_reserve("cl_neo_gunplay_crosshair_reserve", "1", FCVAR_ARCHIVE,
	"With the crosshair layer: the magazines left (a shotgun's shells and slugs) below the crosshair.", true, 0, true, 1);

namespace NeoGunplayReserve
{
// Pixels at 1080p from the aim crosshair.
static constexpr float BELOW = 96.0f;			// past the quick info's deadzone (85), above its brackets' feet (70) and between them
static constexpr float GLYPH_W = 4.0f;			// a magazine: a narrow upright box, its top cut at a slant
static constexpr float GLYPH_H = 9.0f;
static constexpr float GLYPH_GAP = 4.0f;		// between the glyph and the number
static constexpr float CHANGE_TIME = 0.25f;	// a change brightens it this long

static struct
{
	const C_NEOBaseCombatWeapon *pWeapon = nullptr;
	wchar_t last[16] = L"";
	float changed = -100.0f;
} s_reserve;

static vgui::HFont Font()
{
	static vgui::HFont s_font = vgui::INVALID_FONT;
	if (s_font == vgui::INVALID_FONT)
	{
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetDefaultScheme());
		s_font = pScheme ? pScheme->GetFont("NHudOCRSmallNoAdditive", true) : vgui::INVALID_FONT;
	}
	return s_font;
}
} // namespace NeoGunplayReserve

void NeoGunplayPaintReserve(const NeoCrosshairFrame &frame)
{
	using namespace NeoGunplayReserve;
	C_NEOBaseCombatWeapon *pWeapon = frame.pWeapon;
	// A watched player's ammo goes to them alone (frame.clip -1); no magazines for melee, throwables, the detpack.
	if (!cl_neo_gunplay_crosshair_reserve.GetBool() || frame.clip < 0 || !pWeapon || !pWeapon->UsesClipsForAmmo1()
		|| pWeapon->GetMaxClip1() <= 0 || pWeapon->IsMeleeWeapon() || (pWeapon->GetNeoWepBits() & NEO_WEP_DETPACK))
	{
		return;
	}
	const vgui::HFont font = Font();
	if (font == vgui::INVALID_FONT)
	{
		return;
	}
	// As the ammo panel counts them: magazines left (a part-used one counts), or the Supa 7's shells and slugs.
	const int ammo = pWeapon->m_iPrimaryAmmoCount;
	wchar_t text[16];
	int left;
	if (pWeapon->GetNeoWepBits() & NEO_WEP_SUPA7)
	{
		left = ammo + pWeapon->m_iSecondaryAmmoCount.Get();
		V_snwprintf(text, ARRAYSIZE(text), L"%d+%d", ammo, pWeapon->m_iSecondaryAmmoCount.Get());
	}
	else
	{
		left = static_cast<int>(ceilf(fabsf(static_cast<float>(ammo) / pWeapon->GetMaxClip1())));
		V_snwprintf(text, ARRAYSIZE(text), L"%d", left);
	}
	const float now = gpGlobals->realtime;
	if (pWeapon != s_reserve.pWeapon || V_wcscmp(text, s_reserve.last) != 0)
	{
		s_reserve.changed = (pWeapon == s_reserve.pWeapon) ? now : -100.0f;	// a weapon switch isn't a change
		s_reserve.pWeapon = pWeapon;
		V_wcsncpy(s_reserve.last, text, sizeof(s_reserve.last));
	}

	// The glyph and the number side by side, centred under the aim crosshair; none left reads in the warning colour.
	static const Color s_warn(255, 181, 71, 255);
	const Color &c = (left <= 0) ? s_warn : frame.color;
	const float bright = Max(0.0f, 1.0f - (now - s_reserve.changed) / CHANGE_TIME);
	const int alpha = clamp(RoundFloatToInt(frame.color.a() * (0.8f + 0.2f * bright)), 0, 255);
	int wide, tall;
	vgui::surface()->GetTextSize(font, text, wide, tall);
	const float s = frame.s;
	const float total = (GLYPH_W + GLYPH_GAP) * s + wide;
	const Vector2D at = frame.centre + frame.aimOffset + Vector2D(0.0f, BELOW * s);
	const float left0 = at.x - total * 0.5f;

	NeoGhostBegin(c, alpha);
	const float gx0 = left0, gx1 = left0 + GLYPH_W * s, gy1 = at.y + GLYPH_H * 0.5f * s, gy0 = at.y - GLYPH_H * 0.5f * s;
	NeoGhostStroke(frame.pen, Vector2D(gx0, gy1), Vector2D(gx1, gy1), NEO_GHOST_LIGHT);
	NeoGhostStroke(frame.pen, Vector2D(gx0, gy0 + 1.5f * s), Vector2D(gx0, gy1), NEO_GHOST_LIGHT);
	NeoGhostStroke(frame.pen, Vector2D(gx1, gy0), Vector2D(gx1, gy1), NEO_GHOST_LIGHT);
	NeoGhostStroke(frame.pen, Vector2D(gx0, gy0 + 1.5f * s), Vector2D(gx1, gy0), NEO_GHOST_LIGHT);	// the slanted top
	NeoGhostFlush();

	const int tx = RoundFloatToInt(left0 + (GLYPH_W + GLYPH_GAP) * s), ty = RoundFloatToInt(at.y) - tall / 2;
	vgui::surface()->DrawSetTextFont(font);
	vgui::surface()->DrawSetTextColor(0, 0, 0, alpha * 7 / 10);
	vgui::surface()->DrawSetTextPos(tx + 1, ty + 1);
	vgui::surface()->DrawPrintText(text, V_wcslen(text));
	vgui::surface()->DrawSetTextColor(c.r(), c.g(), c.b(), alpha);
	vgui::surface()->DrawSetTextPos(tx, ty);
	vgui::surface()->DrawPrintText(text, V_wcslen(text));
}
