#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_hud_draw.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The cyberbrain's drawing helpers: lines and fills batched through the ghost's strokes (one draw call a flush),
// text in NT's own faces from the client scheme, NT's grey plates.

ConVar cl_neo_hud_stroke_floor("cl_neo_hud_stroke_floor", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's lines: 1 = no hairlines (light strokes drawn medium, 1.4 px at 1080p), 0 = as drawn (0.8 px).",
	true, 0, true, 1);

namespace NeoCyberbrain
{
const Color WARN(255, 181, 71, 255), CRIT(255, 90, 74, 255), TEAM_OURS(130, 220, 120, 255);

int Alpha(const Frame &f, float a)
{
	return clamp(RoundFloatToInt(f.color.a() * a * f.alpha), 0, 255);
}

constexpr float MEASURE_FROM = 0.05f;	// strength under this doesn't count as drawn
constexpr float DRAWN_SHRINK = 0.4f;	// seconds the measured box takes to shrink (it grows at once)
static struct
{
	bool bOn = false, bPaused = false, bAny = false;
	Vector2D lo, hi;
} s_measure;

static void Measure(const Vector2D &a, const Vector2D &b, float alpha)
{
	if (!s_measure.bOn || s_measure.bPaused || alpha < MEASURE_FROM)
		return;
	const Vector2D lo(Min(a.x, b.x), Min(a.y, b.y)), hi(Max(a.x, b.x), Max(a.y, b.y));
	if (!s_measure.bAny)
	{
		s_measure.lo = lo;
		s_measure.hi = hi;
		s_measure.bAny = true;
		return;
	}
	s_measure.lo.Init(Min(s_measure.lo.x, lo.x), Min(s_measure.lo.y, lo.y));
	s_measure.hi.Init(Max(s_measure.hi.x, hi.x), Max(s_measure.hi.y, hi.y));
}

void MeasureBegin()
{
	s_measure.bOn = true;
	s_measure.bPaused = s_measure.bAny = false;
}

void MeasurePause(bool bPaused)
{
	s_measure.bPaused = bPaused;
}

void MeasureEnd(Place &place, float now)
{
	s_measure.bOn = false;
	if (!s_measure.bAny)
	{
		place.bDrawn = false;
		return;
	}
	const Vector2D centre = (s_measure.lo + s_measure.hi) * 0.5f - place.pos, half = (s_measure.hi - s_measure.lo) * 0.5f;
	const float dt = clamp(now - place.drawnTime, 0.0f, 0.1f);
	place.drawnTime = now;
	if (!place.bDrawn)
	{
		place.drawnCentre = centre;
		place.drawnHalf = half;
		place.bDrawn = true;
		return;
	}
	// Growing at once (nothing drawn outside it), shrinking eased (no pop as a part goes).
	const float ease = Min(1.0f, dt / DRAWN_SHRINK);
	Vector2D lo = place.drawnCentre - place.drawnHalf, hi = place.drawnCentre + place.drawnHalf;
	const Vector2D newLo = centre - half, newHi = centre + half;
	lo.x = newLo.x < lo.x ? newLo.x : lo.x + (newLo.x - lo.x) * ease;
	lo.y = newLo.y < lo.y ? newLo.y : lo.y + (newLo.y - lo.y) * ease;
	hi.x = newHi.x > hi.x ? newHi.x : hi.x + (newHi.x - hi.x) * ease;
	hi.y = newHi.y > hi.y ? newHi.y : hi.y + (newHi.y - hi.y) * ease;
	place.drawnCentre = (lo + hi) * 0.5f;
	place.drawnHalf = (hi - lo) * 0.5f;
}

void Line(const Frame &f, const Vector2D &a, const Vector2D &b, NeoGhostWeight weight, const Color &c, float alpha)
{
	Measure(a, b, alpha);
	NeoGhostBegin(c, Alpha(f, alpha));
	// The stroke floor (Kyle, 2026-10-01: minimal, but not too thin): no hairlines in the HUD, light draws as medium.
	if (weight == NEO_GHOST_LIGHT && cl_neo_hud_stroke_floor.GetBool())
		weight = NEO_GHOST_MEDIUM;
	NeoGhostStroke(f.pen, a, b, weight);
}

void Rect(const Frame &f, const Vector2D &a, const Vector2D &b, const Color &c, float alpha)
{
	Measure(a, b, alpha);
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
	static const char *s_names[FONT__COUNT] = { "NHudCyberValue", "NHudCyberValueLarge", "NHudCyberLabel", "NHudCyberPlate", "NHudCyberKanji", "NHudCyberIntegrity", "NHudKillfeedIcons", "NHudCyberLabel",
		"NHudCyberBullets", "NHudCyberPlateShort", "NHudCyberMachine", "NHudCyberMachineSmall", "NHudCyberNumber" };
	static const char *s_fallbacks[FONT__COUNT] = { "NHudOCRSmallerNoAdditive", "NHudOCRNoAdditive", "NHudOCRSmallerNoAdditive", "NHudOCRSmallerNoAdditive", nullptr, "NHudOCRNoAdditive", nullptr, "NHudOCRSmallerNoAdditive", "NHudBullets",
		"NHudOCRSmallerNoAdditive", "NHudOCRSmallerNoAdditive", "NHudOCRSmallerNoAdditive", "NHudOCRNoAdditive" };
	static vgui::HFont s_fonts[FONT__COUNT];
	static bool s_bInit = false;
	if (!s_bInit)
	{
		for (vgui::HFont &h : s_fonts)
			h = vgui::INVALID_FONT;
		s_bInit = true;
	}
	return NeoHudSchemeFont(s_fonts[font], s_names[font], s_fallbacks[font]);
}

void PrintFonts()
{
	// The engine gives no family name for these, and a missing face falls back silently at the asked height; what
	// does tell: NOCR is fixed pitch (every glyph as wide), the fallbacks aren't, and the others are proportional.
	static const char *s_slots[FONT__COUNT] = { "value", "value large", "label", "plate", "kanji", "integrity", "icons", "name", "bullets",
		"plate short", "machine", "machine small", "number" };
	static const bool s_bNOCR[FONT__COUNT] = { true, true, false, false, false, false, false, false, true, false, false, false, false };
	for (int i = 0; i < FONT__COUNT; ++i)
	{
		const vgui::HFont handle = GetFont(static_cast<Font>(i));
		if (handle == vgui::INVALID_FONT)
		{
			Msg("[cyberbrain] %-12s missing (no scheme entry)\n", s_slots[i]);
			continue;
		}
		int narrow = 0, wide = 0, tall = 0;
		vgui::surface()->GetTextSize(handle, L"iiii", narrow, tall);
		vgui::surface()->GetTextSize(handle, L"MMMM", wide, tall);
		const bool bFixed = narrow == wide;
		const char *pVerdict = s_bNOCR[i] ? (bFixed ? "NOCR loaded (fixed pitch)" : "NOT NOCR: proportional, a fallback face")
			: (bFixed ? "fixed pitch: a fallback?" : "proportional, as expected");
		Msg("[cyberbrain] %-12s %2d px tall, iiii %3d px, MMMM %3d px: %s\n", s_slots[i], tall, narrow, wide, pVerdict);
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
	Measure(Vector2D(static_cast<float>(tx), static_cast<float>(ty)), Vector2D(static_cast<float>(tx + wide), static_cast<float>(ty + tall)), alpha);
	ProbeText(Vector2D(static_cast<float>(tx), static_cast<float>(ty)), Vector2D(static_cast<float>(tx + wide), static_cast<float>(ty + tall)), pText, alpha);
	// A shadow on a dark scene; a dark edge all round on a bright one.
	NeoHudPrintText(handle, pText, count, tx, ty, Color(c.r(), c.g(), c.b(), Alpha(f, alpha)),
		f.contrast > 0.3f ? NEO_HUD_TEXT_EDGED : NEO_HUD_TEXT_SHADOW, Alpha(f, alpha * (0.6f + 0.35f * f.contrast)));
	return static_cast<float>(wide);
}

float TextWidth(const wchar_t *pText, Font font)
{
	const vgui::HFont handle = GetFont(font);
	if (handle == vgui::INVALID_FONT || !pText || !pText[0])
		return 0.0f;
	int wide, tall;
	vgui::surface()->GetTextSize(handle, pText, wide, tall);
	return static_cast<float>(wide);
}

float FontTall(Font font)
{
	const vgui::HFont handle = GetFont(font);
	return handle == vgui::INVALID_FONT ? 0.0f : static_cast<float>(vgui::surface()->GetFontTall(handle));
}

// The plate's box and text, returning its left edge and width. A bar, if given, marks its leading edge.
static bool PlateBox(const Frame &f, Font font, const wchar_t *pText, float x, float y, int align, float alpha, const Color &bg, const Color &fg,
	float &x0, float &w, const Color *pBar = nullptr)
{
	const vgui::HFont handle = GetFont(font);
	if (handle == vgui::INVALID_FONT || !pText)
	{
		return false;
	}
	int wide, tall;
	vgui::surface()->GetTextSize(handle, pText, wide, tall);
	const float pad = 5.0f * f.s, h = tall + 2.0f * f.s;
	w = wide + pad * 2.0f;
	x0 = (align < 0) ? x - w : (align == 0) ? x - w * 0.5f : x;
	Rect(f, Vector2D(x0, y - h * 0.5f), Vector2D(x0 + w, y + h * 0.5f), bg, 0.85f * alpha);
	ProbeText(Vector2D(x0, y - h * 0.5f), Vector2D(x0 + w, y + h * 0.5f), pText, alpha);
	if (pBar)
		Rect(f, Vector2D(x0, y - h * 0.5f), Vector2D(x0 + Max(2.0f, 3.0f * f.s), y + h * 0.5f), *pBar, alpha);
	NeoGhostFlush();
	NeoHudPrintText(handle, pText, V_wcslen(pText), RoundFloatToInt(x0 + pad), RoundFloatToInt(y - tall * 0.5f),
		Color(fg.r(), fg.g(), fg.b(), Alpha(f, alpha)), NEO_HUD_TEXT_PLAIN, 0);
	return true;
}

void MachinePlate(const Frame &f, const wchar_t *pText, float x, float y, int align, float alpha, bool bCritical, bool bSmall)
{
	static const Color s_paper(223, 226, 220, 255), s_ink(21, 25, 27, 255), s_bar(232, 200, 50, 255);
	float x0, w;
	PlateBox(f, bSmall ? FONT_MACHINE_SMALL : FONT_MACHINE, pText, x, y, align, alpha, bCritical ? CRIT : s_paper,
		bCritical ? Color(252, 235, 235, 255) : s_ink, x0, w, bCritical ? nullptr : &s_bar);
}

void Plate(const Frame &f, const wchar_t *pText, float x, float y, int align, float alpha, const wchar_t *pKanji)
{
	float x0, w;
	const Font font = pText && V_wcslen(pText) <= 4 ? FONT_PLATE_SHORT : FONT_PLATE;
	if (!PlateBox(f, font, pText, x, y, align, alpha, Color(198, 203, 206, 255), Color(18, 22, 25, 255), x0, w))
	{
		return;
	}
	static ConVarRef cl_neo_hud_kanji("cl_neo_hud_kanji");
	if (pKanji && align != 0 && cl_neo_hud_kanji.GetBool())
	{
		// Beside the plate, on its open side: a right-aligned plate has it to the left.
		const float kx = (align < 0) ? x0 - 6.0f * f.s : x0 + w + 6.0f * f.s;
		Text(f, pKanji, kx, y, (align < 0) ? -1 : 1, FONT_KANJI, f.color, 0.6f * alpha);
	}
}

// One cell's outline or fill: its top corner on the chamfer side cut by c.
static void CellShape(const Frame &f, float x0, float y0, float x1, float y1, float c, int chamfer, bool bFill, const Color &col,
	float alpha)
{
	const Vector2D tl(x0, y0), tr(x1, y0), br(x1, y1), bl(x0, y1);
	const Vector2D cutA = chamfer > 0 ? Vector2D(x1 - c, y0) : Vector2D(x0 + c, y0);
	const Vector2D cutB = chamfer > 0 ? Vector2D(x1, y0 + c) : Vector2D(x0, y0 + c);
	if (bFill)
	{
		Measure(tl, br, alpha);
		NeoGhostBegin(col, Alpha(f, alpha));
		if (chamfer > 0)
		{
			const Vector2D left[4] = { tl, cutA, Vector2D(cutA.x, y1), bl };
			const Vector2D right[4] = { cutA, cutB, br, Vector2D(cutA.x, y1) };
			NeoGhostFill(left);
			NeoGhostFill(right);
		}
		else
		{
			const Vector2D left[4] = { cutB, cutA, Vector2D(cutA.x, y1), bl };
			const Vector2D right[4] = { cutA, tr, br, Vector2D(cutA.x, y1) };
			NeoGhostFill(left);
			NeoGhostFill(right);
		}
		return;
	}
	const Vector2D outline[6] = { chamfer > 0 ? tl : cutB, cutA, chamfer > 0 ? cutB : tr, br, bl, chamfer > 0 ? tl : cutB };
	for (int i = 0; i < 5; ++i)
		Line(f, outline[i], outline[i + 1], NEO_GHOST_LIGHT, col, alpha);
}

void Cells(const Frame &f, const Vector2D &a, const Vector2D &b, float fill, const CellStyle &style, float alpha)
{
	fill = clamp(fill, 0.0f, 1.0f);
	const int n = Max(style.count, 1);
	if (style.bRow)
	{
		// Left to right: each cell full, filling from its left, or empty.
		const float gap = 3.0f * f.s, cw = (b.x - a.x - gap * (n - 1)) / n, c = Min(4.0f * f.s, Min(cw, b.y - a.y) * 0.35f);
		for (int i = 0; i < n; ++i)
		{
			const float x0 = a.x + i * (cw + gap), x1 = x0 + cw, t = clamp(fill * n - i, 0.0f, 1.0f);
			if (t >= 0.999f)
			{
				CellShape(f, x0, a.y, x1, b.y, c, style.chamfer, true, style.fill, 0.85f * alpha);
				continue;
			}
			CellShape(f, x0, a.y, x1, b.y, c, style.chamfer, false, f.color, (t > 0.0f ? 0.8f : 0.25f) * alpha);
			if (t > 0.0f)
			{
				const float pad = 1.5f * f.s;
				Rect(f, Vector2D(x0 + pad, a.y + c), Vector2D(x0 + pad + (cw - 2.0f * pad) * t, b.y - pad), style.fill, 0.6f * alpha);
			}
		}
		return;
	}
	const float gap = 3.0f * f.s, h = (b.y - a.y - gap * (n - 1)) / n, w = b.x - a.x;
	const float c = Min(4.0f * f.s, Min(w, h) * 0.35f);
	for (int i = 0; i < n; ++i)
	{
		const float y1 = b.y - i * (h + gap), y0 = y1 - h;
		const float t = clamp(fill * n - i, 0.0f, 1.0f);
		if (t >= 0.999f)
		{
			CellShape(f, a.x, y0, b.x, y1, c, style.chamfer, true, style.fill, 0.85f * alpha);
			continue;
		}
		CellShape(f, a.x, y0, b.x, y1, c, style.chamfer, false, f.color, (t > 0.0f ? 0.8f : 0.25f) * alpha);
		if (t > 0.0f)
		{
			// Filling from the bottom, clear of the cut corner; its edge flares just past each quarter (an event read
			// from the fill itself, no loop).
			const float pad = 1.5f * f.s, top = Max(y1 - (h - pad) * t, y0 + c);
			Rect(f, Vector2D(a.x + pad, top), Vector2D(b.x - pad, y1 - pad), style.fill, 0.6f * alpha);
			const float quarter = t * 4.0f - floorf(t * 4.0f);
			const float edge = style.bCharging && quarter < 0.1f ? 1.0f : 0.7f;
			Line(f, Vector2D(a.x + pad, top), Vector2D(b.x - pad, top), NEO_GHOST_MEDIUM, style.fill, edge * alpha);
		}
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
