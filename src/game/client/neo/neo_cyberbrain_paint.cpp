#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The cyberbrain's drawing helpers: lines and fills batched through the ghost's strokes (one draw call a flush),
// text in NT's own faces from the client scheme, NT's grey plates.

namespace NeoCyberbrain
{
const Color WARN(255, 181, 71, 255), CRIT(255, 90, 74, 255), TEAM_OURS(130, 220, 120, 255);

int Alpha(const Frame &f, float a)
{
	return clamp(RoundFloatToInt(f.color.a() * a * f.alpha), 0, 255);
}

void Line(const Frame &f, const Vector2D &a, const Vector2D &b, NeoGhostWeight weight, const Color &c, float alpha)
{
	NeoGhostBegin(c, Alpha(f, alpha));
	NeoGhostStroke(f.pen, a, b, weight);
}

void Rect(const Frame &f, const Vector2D &a, const Vector2D &b, const Color &c, float alpha)
{
	NeoGhostBegin(c, Alpha(f, alpha));
	NeoGhostFillRect(Min(a.x, b.x), Min(a.y, b.y), Max(Max(a.x, b.x), Min(a.x, b.x) + 1.0f), Max(Max(a.y, b.y), Min(a.y, b.y) + 1.0f));
}

void RectOutline(const Frame &f, const Vector2D &a, const Vector2D &b, NeoGhostWeight weight, const Color &c, float alpha)
{
	const Vector2D p1(b.x, a.y), p3(a.x, b.y);
	Line(f, a, p1, weight, c, alpha);
	Line(f, p1, b, weight, c, alpha);
	Line(f, b, p3, weight, c, alpha);
	Line(f, p3, a, weight, c, alpha);
}

void Arc(const Frame &f, const Vector2D &centre, const Vector2D &radii, float from, float to, NeoGhostWeight weight, const Color &c, float alpha)
{
	const int steps = Max(2, static_cast<int>(fabsf(to - from) / 8.0f));
	Vector2D last;
	for (int i = 0; i <= steps; ++i)
	{
		const float q = DEG2RAD(from + (to - from) * i / steps);
		const Vector2D p(centre.x + sinf(q) * radii.x, centre.y - cosf(q) * radii.y);
		if (i > 0)
		{
			Line(f, last, p, weight, c, alpha);
		}
		last = p;
	}
}

static vgui::HFont GetFont(Font font)
{
	static const char *s_names[FONT__COUNT] = { "NHudCyberValue", "NHudCyberValueLarge", "NHudCyberLabel", "NHudCyberPlate", "NHudCyberKanji", "NHudCyberIntegrity" };
	static const char *s_fallbacks[FONT__COUNT] = { "NHudOCRSmallerNoAdditive", "NHudOCRNoAdditive", "NHudOCRSmallerNoAdditive", "NHudOCRSmallerNoAdditive", nullptr, "NHudOCRNoAdditive" };
	static vgui::HFont s_fonts[FONT__COUNT] = { vgui::INVALID_FONT, vgui::INVALID_FONT, vgui::INVALID_FONT, vgui::INVALID_FONT, vgui::INVALID_FONT, vgui::INVALID_FONT };
	vgui::HFont &handle = s_fonts[font];
	if (handle == vgui::INVALID_FONT)
	{
		// The HUD's faces live in the client scheme (the default scheme is the engine's, without them).
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
		if (pScheme)
		{
			handle = pScheme->GetFont(s_names[font], true);
			if (handle == vgui::INVALID_FONT && s_fallbacks[font])
			{
				handle = pScheme->GetFont(s_fallbacks[font], true);
			}
		}
	}
	return handle;
}

void PrintFonts()
{
	static const char *s_slots[FONT__COUNT] = { "value", "value large", "label", "plate", "kanji", "integrity" };
	for (int i = 0; i < FONT__COUNT; ++i)
	{
		const vgui::HFont handle = GetFont(static_cast<Font>(i));
		const char *pFamily = (handle != vgui::INVALID_FONT) ? vgui::surface()->GetFontFamilyName(handle) : nullptr;
		int wide = 0, tall = 0;
		if (handle != vgui::INVALID_FONT)
		{
			vgui::surface()->GetTextSize(handle, L"Ag", wide, tall);
		}
		Msg("[cyberbrain] %-12s %s (%d px tall)\n", s_slots[i], pFamily ? pFamily : "(none)", tall);
	}
}
CON_COMMAND(cl_neo_hud_fonts, "Prints which font each of the cyberbrain HUD's text slots resolved to.")
{
	PrintFonts();
}

float Text(const Frame &f, const wchar_t *pText, float x, float y, int align, Font font, const Color &c, float alpha)
{
	const vgui::HFont handle = GetFont(font);
	const int count = pText ? V_wcslen(pText) : 0;
	if (handle == vgui::INVALID_FONT || count <= 0 || Alpha(f, alpha) <= 0)
	{
		return 0.0f;
	}
	NeoGhostFlush();	// what's queued goes under the text
	int wide, tall;
	vgui::surface()->GetTextSize(handle, pText, wide, tall);
	const int tx = RoundFloatToInt(x) - ((align < 0) ? wide : (align == 0) ? wide / 2 : 0), ty = RoundFloatToInt(y) - tall / 2;
	vgui::surface()->DrawSetTextFont(handle);
	// A shadow on a dark scene; a dark edge all round on a bright one.
	const int edge = Alpha(f, alpha * (0.6f + 0.35f * f.contrast));
	vgui::surface()->DrawSetTextColor(0, 0, 0, edge);
	static const int s_offsets[][2] = { { 1, 1 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	for (int i = 0; i < (f.contrast > 0.3f ? 5 : 1); ++i)
	{
		vgui::surface()->DrawSetTextPos(tx + s_offsets[i][0], ty + s_offsets[i][1]);
		vgui::surface()->DrawPrintText(pText, count);
	}
	vgui::surface()->DrawSetTextColor(c.r(), c.g(), c.b(), Alpha(f, alpha));
	vgui::surface()->DrawSetTextPos(tx, ty);
	vgui::surface()->DrawPrintText(pText, count);
	return static_cast<float>(wide);
}

void Plate(const Frame &f, const wchar_t *pText, float x, float y, int align, float alpha, const wchar_t *pKanji)
{
	const vgui::HFont handle = GetFont(FONT_PLATE);
	if (handle == vgui::INVALID_FONT || !pText)
	{
		return;
	}
	int wide, tall;
	vgui::surface()->GetTextSize(handle, pText, wide, tall);
	const float pad = 5.0f * f.s, w = wide + pad * 2.0f, h = tall + 2.0f * f.s;
	const float x0 = (align < 0) ? x - w : (align == 0) ? x - w * 0.5f : x;
	Rect(f, Vector2D(x0, y - h * 0.5f), Vector2D(x0 + w, y + h * 0.5f), Color(198, 203, 206, 255), 0.85f * alpha);
	NeoGhostFlush();
	vgui::surface()->DrawSetTextFont(handle);
	vgui::surface()->DrawSetTextColor(18, 22, 25, Alpha(f, alpha));
	vgui::surface()->DrawSetTextPos(RoundFloatToInt(x0 + pad), RoundFloatToInt(y - tall * 0.5f));
	vgui::surface()->DrawPrintText(pText, V_wcslen(pText));
	static ConVarRef cl_neo_hud_kanji("cl_neo_hud_kanji");
	if (pKanji && align != 0 && cl_neo_hud_kanji.GetBool())
	{
		// Beside the plate, on its open side: a right-aligned plate has it to the left.
		const float kx = (align < 0) ? x0 - 6.0f * f.s : x0 + w + 6.0f * f.s;
		Text(f, pKanji, kx, y, (align < 0) ? -1 : 1, FONT_KANJI, f.color, 0.6f * alpha);
	}
}

void Tank(const Frame &f, const Vector2D &a, const Vector2D &b, float fill, const TankStyle &style, float alpha)
{
	fill = clamp(fill, 0.0f, 1.0f);
	const float s = f.s, pad = 2.0f * s;
	const bool bVertical = style.dir == 0;
	// The vessel and its cap.
	RectOutline(f, a, b, NEO_GHOST_MEDIUM, f.color, 0.8f * alpha);
	if (bVertical)
	{
		const float cx = (a.x + b.x) * 0.5f, cw = Max(2.0f * s, (b.x - a.x) * 0.25f);
		Rect(f, Vector2D(cx - cw, a.y - 3.0f * s), Vector2D(cx + cw, a.y - 1.0f * s), f.color, 0.8f * alpha);
	}
	else
	{
		const float cy = (a.y + b.y) * 0.5f, ch = Max(2.0f * s, (b.y - a.y) * 0.25f), x = style.dir > 0 ? b.x + 1.0f * s : a.x - 3.0f * s;
		Rect(f, Vector2D(x, cy - ch), Vector2D(x + 2.0f * s, cy + ch), f.color, 0.8f * alpha);
	}
	// Inside: the fill from its end, a hard edge; the rest hatched.
	const Vector2D in0 = a + Vector2D(pad, pad), in1 = b - Vector2D(pad, pad);
	const float length = bVertical ? in1.y - in0.y : in1.x - in0.x;
	const float edge = length * fill;
	Vector2D f0 = in0, f1 = in1, e0 = in0, e1 = in1;	// the filled part, and the empty part
	if (bVertical)
	{
		f0.y = in1.y - edge;
		e1.y = f0.y;
	}
	else if (style.dir > 0)
	{
		f1.x = in0.x + edge;
		e0.x = f1.x;
	}
	else
	{
		f0.x = in1.x - edge;
		e1.x = f0.x;
	}
	const Color fc = style.fill.a() ? style.fill : f.color;
	if (edge > 0.5f)
	{
		if (style.bHollow)
		{
			RectOutline(f, f0, f1, NEO_GHOST_LIGHT, fc, 0.85f * alpha);
		}
		else
		{
			Rect(f, f0, f1, fc, 0.8f * alpha);
		}
		// Segment gaps, cut into the fill.
		for (int i = 1; i < style.segments; ++i)
		{
			const float t = static_cast<float>(i) / style.segments;
			if (bVertical)
			{
				const float y = in1.y - length * t;
				if (y > f0.y)
					Rect(f, Vector2D(in0.x, y - 0.75f * s), Vector2D(in1.x, y + 0.75f * s), Color(0, 0, 0, 255), 0.6f * alpha);
			}
			else
			{
				const float x = style.dir > 0 ? in0.x + length * t : in1.x - length * t;
				if (style.dir > 0 ? x < f1.x : x > f0.x)
					Rect(f, Vector2D(x - 0.75f * s, in0.y), Vector2D(x + 0.75f * s, in1.y), Color(0, 0, 0, 255), 0.6f * alpha);
			}
		}
		// The leading edge: steady and dimmer while it charges, flaring as each segment fills (an event, not a loop;
		// read from the fill itself, just past a segment's line).
		const float seg = fill * style.segments, past = seg - floorf(seg);
		const float pulse = !style.bCharging ? 0.9f : (seg >= 1.0f && past < 0.08f) ? 1.0f : 0.6f;
		if (bVertical)
			Line(f, Vector2D(in0.x, f0.y), Vector2D(in1.x, f0.y), NEO_GHOST_MEDIUM, fc, pulse * alpha);
		else
		{
			const float x = style.dir > 0 ? f1.x : f0.x;
			Line(f, Vector2D(x, in0.y), Vector2D(x, in1.y), NEO_GHOST_MEDIUM, fc, pulse * alpha);
		}
	}
	// Hatching in the empty part: fine diagonals, clipped to it.
	const float hatchH = e1.y - e0.y, hatchW = e1.x - e0.x;
	if (hatchH > 1.0f && hatchW > 1.0f)
	{
		const float step = 5.0f * s;
		for (float c = e0.x - hatchH; c < e1.x; c += step)
		{
			// From (c, e1.y) up to (c + hatchH, e0.y), clipped to [e0.x, e1.x].
			const float x0 = Max(c, e0.x), x1 = Min(c + hatchH, e1.x);
			if (x1 <= x0)
				continue;
			Line(f, Vector2D(x0, e1.y - (x0 - c)), Vector2D(x1, e1.y - (x1 - c)), NEO_GHOST_LIGHT, f.color, 0.22f * alpha);
		}
	}
}

// Your noise state as an icon: a dot and 0 to 3 arcs (silent struck through, loud in amber).
void NoiseIcon(const Frame &f, const Vector2D &p, int arcs, bool bSilent, const Color &c, float a)
{
	Rect(f, p + Vector2D(-2, -2) * f.s, p + Vector2D(2, 2) * f.s, c, a);
	for (int i = 1; i <= arcs; ++i)
	{
		Arc(f, p, Vector2D(5.0f + i * 5.0f, 5.0f + i * 5.0f) * f.s, 45.0f, 135.0f, NEO_GHOST_MEDIUM, c, a);
		Arc(f, p, Vector2D(5.0f + i * 5.0f, 5.0f + i * 5.0f) * f.s, 225.0f, 315.0f, NEO_GHOST_MEDIUM, c, a);
	}
	if (bSilent)
	{
		Arc(f, p, Vector2D(9.0f, 9.0f) * f.s, 0.0f, 360.0f, NEO_GHOST_LIGHT, c, a);
		Line(f, p + Vector2D(-7, 7) * f.s, p + Vector2D(7, -7) * f.s, NEO_GHOST_LIGHT, c, a);
	}
}

int NoiseArcs(const Senses &s)
{
	float loudest = 0.0f;
	for (int i = 0; i < s.noiseCount; ++i)
		loudest = Max(loudest, s.noise[i].metres);
	return loudest >= 60.0f ? 3 : loudest >= 16.0f ? 2 : loudest > 0.0f ? 1 : 0;
}

void Cross(const Frame &f, const Vector2D &at, float size, float alpha)
{
	Line(f, at - Vector2D(size, 0.0f), at + Vector2D(size, 0.0f), NEO_GHOST_LIGHT, f.color, alpha);
	Line(f, at - Vector2D(0.0f, size), at + Vector2D(0.0f, size), NEO_GHOST_LIGHT, f.color, alpha);
}
} // namespace NeoCyberbrain
