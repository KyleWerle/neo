#include "cbase.h"
#include "neo_hud_draw.h"
#include "neo_hud_profile.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

vgui::HFont NeoHudSchemeFont(vgui::HFont &cache, const char *pName, const char *pFallback)
{
	if (cache == vgui::INVALID_FONT)
	{
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
		if (pScheme)
		{
			cache = pScheme->GetFont(pName, true);
			if (cache == vgui::INVALID_FONT && pFallback)
			{
				cache = pScheme->GetFont(pFallback, true);
			}
		}
	}
	return cache;
}

void NeoHudPrintText(vgui::HFont font, const wchar_t *pText, int count, int x, int y, const Color &c, NeoHudTextEdge edge,
	int edgeAlpha)
{
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
