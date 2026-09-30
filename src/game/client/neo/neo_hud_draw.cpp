#include "cbase.h"
#include "neo_hud_draw.h"
#include "neo_hud_profile.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_hud_text_baked("cl_neo_hud_text_baked", "1", FCVAR_ARCHIVE,
	"The HUD's text edge: 0 = printed as copies of the text (a shadow, or five for the edge all round), 1 = baked into"
	" the font (the scheme's _Outline and _Shadow faces), one print a string.", true, 0, true, 1);

// Each face's baked variants, found with it (a fallback face has none).
struct NeoHudBaked { vgui::HFont base, edged, shadow; };
static NeoHudBaked s_baked[16];
static int s_bakedCount = 0;

static const NeoHudBaked *BakedOf(vgui::HFont font)
{
	for (int i = 0; i < s_bakedCount; ++i)
	{
		if (s_baked[i].base == font)
			return &s_baked[i];
	}
	return nullptr;
}

vgui::HFont NeoHudSchemeFont(vgui::HFont &cache, const char *pName, const char *pFallback)
{
	if (cache == vgui::INVALID_FONT)
	{
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
		if (pScheme)
		{
			cache = pScheme->GetFont(pName, true);
			if (cache != vgui::INVALID_FONT && !BakedOf(cache) && s_bakedCount < ARRAYSIZE(s_baked))
			{
				char name[128];
				NeoHudBaked &baked = s_baked[s_bakedCount];
				baked.base = cache;
				V_sprintf_safe(name, "%s_Outline", pName);
				baked.edged = pScheme->GetFont(name, true);
				V_sprintf_safe(name, "%s_Shadow", pName);
				baked.shadow = pScheme->GetFont(name, true);
				if (baked.edged != vgui::INVALID_FONT || baked.shadow != vgui::INVALID_FONT)
					++s_bakedCount;
			}
			if (cache == vgui::INVALID_FONT && pFallback)
			{
				cache = pScheme->GetFont(pFallback, true);
			}
		}
	}
	return cache;
}

// The baked face for this edge, and how far up to print it so its glyphs sit where the plain face's do: an outline
// grows the glyph's cell by a pixel all round (the advance stays), a shadow only adds its row below.
static vgui::HFont BakedFace(vgui::HFont font, NeoHudTextEdge edge, int &up)
{
	const NeoHudBaked *pBaked = cl_neo_hud_text_baked.GetBool() ? BakedOf(font) : nullptr;
	const vgui::HFont baked = !pBaked ? vgui::INVALID_FONT : edge == NEO_HUD_TEXT_EDGED ? pBaked->edged : pBaked->shadow;
	if (baked != vgui::INVALID_FONT)
		up = (vgui::surface()->GetFontTall(baked) - vgui::surface()->GetFontTall(font)) / 2;
	return baked;
}

CON_COMMAND(cl_neo_hud_text_baked_check, "Prints each HUD face beside its baked variants: height and the width of a"
	" test string (the same width means the baked edge keeps the layout; the height grows by 2 for the outline, 1 for"
	" the shadow).")
{
	static const wchar_t s_test[] = L"MMMM 0123";
	for (int i = 0; i < s_bakedCount; ++i)
	{
		const NeoHudBaked &b = s_baked[i];
		const vgui::HFont faces[3] = { b.base, b.edged, b.shadow };
		int wide[3] = {}, tall[3] = {};
		for (int k = 0; k < 3; ++k)
		{
			if (faces[k] != vgui::INVALID_FONT)
				vgui::surface()->GetTextSize(faces[k], s_test, wide[k], tall[k]);
		}
		Msg("[hud] %-24s plain %3d x %2d, outline %3d x %2d, shadow %3d x %2d\n",
			vgui::surface()->GetFontName(b.base), wide[0], tall[0], wide[1], tall[1], wide[2], tall[2]);
	}
	if (s_bakedCount == 0)
		Msg("[hud] no baked faces found yet (they're looked up the first time a style draws)\n");
}

void NeoHudPrintText(vgui::HFont font, const wchar_t *pText, int count, int x, int y, const Color &c, NeoHudTextEdge edge,
	int edgeAlpha)
{
	if (edge != NEO_HUD_TEXT_PLAIN && edgeAlpha > 0)
	{
		int up = 0;
		const vgui::HFont baked = BakedFace(font, edge, up);
		if (baked != vgui::INVALID_FONT)
		{
			vgui::surface()->DrawSetTextFont(baked);
			vgui::surface()->DrawSetTextColor(c);
			vgui::surface()->DrawSetTextPos(x, y - up);
			vgui::surface()->DrawPrintText(pText, count);
			NeoHudCount(NEO_HUD_COUNT_TEXT);
			return;
		}
	}
	vgui::surface()->DrawSetTextFont(font);
	if (edge != NEO_HUD_TEXT_PLAIN && edgeAlpha > 0)
	{
		// The shadow first, then the rest of the edge all round.
		static const int s_offsets[][2] = { { 1, 1 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
		const int copies = edge == NEO_HUD_TEXT_EDGED ? ARRAYSIZE(s_offsets) : 1;
		vgui::surface()->DrawSetTextColor(0, 0, 0, edgeAlpha);
		for (int i = 0; i < copies; ++i)
		{
			vgui::surface()->DrawSetTextPos(x + s_offsets[i][0], y + s_offsets[i][1]);
			vgui::surface()->DrawPrintText(pText, count);
		}
		NeoHudCount(NEO_HUD_COUNT_TEXT, copies);
	}
	vgui::surface()->DrawSetTextColor(c);
	vgui::surface()->DrawSetTextPos(x, y);
	vgui::surface()->DrawPrintText(pText, count);
	NeoHudCount(NEO_HUD_COUNT_TEXT);
}
