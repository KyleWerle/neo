#include "cbase.h"
#include "neo_quickinfo_internal.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The quick info's drawing helpers: lines in the ghost's strokes (their outline pass), fills as flat translucent
// boxes and strips, text in the HUD's OCR faces.

namespace NeoQuickInfo
{
const Color WARN(255, 181, 71, 255);

Vector2D At(const QuickFrame &f, Layer layer, float x, float y)
{
	return f.centre + (Vector2D(x, y) + f.sway[layer]) * f.s;
}

int Alpha(const QuickFrame &f, float a)
{
	return clamp(RoundFloatToInt(f.color.a() * a * f.alpha), 0, 255);
}

void Line(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, NeoGhostWeight weight, const Color &c, float a)
{
	NeoGhostBegin(c, Alpha(f, a));
	NeoGhostStroke(f.pen, At(f, layer, x0, y0), At(f, layer, x1, y1), weight);
}

void Box(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, const Color &c, float a)
{
	// Batched with the strokes, whole pixels as vgui's own rectangles (at least one wide).
	const Vector2D p0 = At(f, layer, Min(x0, x1), Min(y0, y1)), p1 = At(f, layer, Max(x0, x1), Max(y0, y1));
	const float left = static_cast<float>(RoundFloatToInt(p0.x)), top = static_cast<float>(RoundFloatToInt(p0.y));
	NeoGhostBegin(c, Alpha(f, a));
	NeoGhostFillRect(left, top, Max(static_cast<float>(RoundFloatToInt(p1.x)), left + 1.0f), Max(static_cast<float>(RoundFloatToInt(p1.y)), top + 1.0f));
}

void Strip(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, float h, const Color &c, float a)
{
	if (fabsf(x1 - x0) < 0.01f)
	{
		return;
	}
	if (x1 < x0)
	{
		V_swap(x0, x1);
		V_swap(y0, y1);
	}
	NeoGhostBegin(c, Alpha(f, a));
	const Vector2D corners[4] = { At(f, layer, x0, y0 - h * 0.5f), At(f, layer, x1, y1 - h * 0.5f), At(f, layer, x1, y1 + h * 0.5f),
		At(f, layer, x0, y0 + h * 0.5f) };
	NeoGhostFill(corners);
}

static vgui::HFont GetFont(Font font)
{
	static const char *s_names[] = { "NHudOCRSmallerNoAdditive", "NHudOCRSmallNoAdditive", "NHudOCRNoAdditive" };
	static vgui::HFont s_fonts[ARRAYSIZE(s_names)] = { vgui::INVALID_FONT, vgui::INVALID_FONT, vgui::INVALID_FONT };
	vgui::HFont &handle = s_fonts[font];
	if (handle == vgui::INVALID_FONT)
	{
		// The HUD's faces live in the client scheme (the default scheme is the engine's, without them: no text at all).
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
		handle = pScheme ? pScheme->GetFont(s_names[font], true) : vgui::INVALID_FONT;
	}
	return handle;
}

float Text(const QuickFrame &f, Layer layer, const wchar_t *pText, int count, float x, float y, int align, Font font,
	const Color &c, float a)
{
	const vgui::HFont handle = GetFont(font);
	if (handle == vgui::INVALID_FONT || count <= 0)
	{
		return 0.0f;
	}
	NeoGhostFlush();	// what's queued goes under the text
	int wide, tall;
	vgui::surface()->GetTextSize(handle, pText, wide, tall);
	if (count < V_wcslen(pText))
	{
		wide = wide * count / Max(1, V_wcslen(pText));
	}
	const Vector2D at = At(f, layer, x, y);
	const int tx = RoundFloatToInt(at.x) - ((align < 0) ? wide : (align == 0) ? wide / 2 : 0), ty = RoundFloatToInt(at.y) - tall / 2;
	vgui::surface()->DrawSetTextFont(handle);
	vgui::surface()->DrawSetTextColor(0, 0, 0, Alpha(f, a * 0.7f));
	vgui::surface()->DrawSetTextPos(tx + 1, ty + 1);
	vgui::surface()->DrawPrintText(pText, count);
	vgui::surface()->DrawSetTextColor(c.r(), c.g(), c.b(), Alpha(f, a));
	vgui::surface()->DrawSetTextPos(tx, ty);
	vgui::surface()->DrawPrintText(pText, count);
	return wide / f.s;
}

void Cross(const QuickFrame &f, Layer layer, float x, float y, float a)
{
	Line(f, layer, x - MARK, y, x + MARK, y, NEO_GHOST_LIGHT, f.color, a);
	Line(f, layer, x, y - MARK, x, y + MARK, NEO_GHOST_LIGHT, f.color, a);
}
} // namespace NeoQuickInfo
