#include "cbase.h"
#include "neo_quickinfo_internal.h"
#include "neo_ironsights.h"
#include "vstdlib/random.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The housing's drawing (QUICKINFO.md): lines in the ghost's strokes (their outline pass), fills as flat
// translucent boxes, text in the HUD's OCR faces. Every position is in pixels at 1080p from the crosshair, on a
// layer that sways by its depth.

namespace NeoQuickInfo
{
static const Color WARN(255, 181, 71, 255);
static const Color CRIT(255, 81, 99, 255);
static const Color ECHO(90, 220, 255, 255);	// the failing signal's cyan echo

constexpr float FILL_TOP = TOP + 4.0f, FILL_BOTTOM = BOTTOM - 4.0f, FILL_H = FILL_BOTTOM - FILL_TOP;
constexpr float DASH = 4.0f, DASH_GAP = 3.0f;	// the cloak's dashes, fitted per run

float BarHalf(Kind kind)
{
	return (kind == KIND_SUPPORT) ? 72.0f : (kind == KIND_JUGGERNAUT) ? 68.0f : 60.0f;
}

static Vector2D At(const QuickFrame &f, Layer layer, float x, float y)
{
	return f.centre + (Vector2D(x, y) + f.sway[layer]) * f.s;
}
static int Alpha(const QuickFrame &f, float a)
{
	return clamp(RoundFloatToInt(f.color.a() * a * f.alpha), 0, 255);
}
static void Line(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, NeoGhostWeight weight,
	const Color &c, float a)
{
	NeoGhostBegin(c, Alpha(f, a));
	NeoGhostStroke(f.pen, At(f, layer, x0, y0), At(f, layer, x1, y1), weight);
}
static void Box(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, const Color &c, float a)
{
	NeoGhostFlush();	// strokes waiting on their outline go under the fill
	const Vector2D p0 = At(f, layer, Min(x0, x1), Min(y0, y1)), p1 = At(f, layer, Max(x0, x1), Max(y0, y1));
	vgui::surface()->DrawSetColor(c.r(), c.g(), c.b(), Alpha(f, a));
	vgui::surface()->DrawFilledRect(RoundFloatToInt(p0.x), RoundFloatToInt(p0.y), Max(RoundFloatToInt(p1.x), RoundFloatToInt(p0.x) + 1),
		Max(RoundFloatToInt(p1.y), RoundFloatToInt(p0.y) + 1));
}

static vgui::HFont Font(bool bLarge)
{
	static vgui::HFont s_fonts[2] = { vgui::INVALID_FONT, vgui::INVALID_FONT };
	vgui::HFont &font = s_fonts[bLarge ? 1 : 0];
	if (font == vgui::INVALID_FONT)
	{
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetDefaultScheme());
		font = pScheme ? pScheme->GetFont(bLarge ? "NHudOCRSmallNoAdditive" : "NHudOCRSmallerNoAdditive", true) : vgui::INVALID_FONT;
	}
	return font;
}
// Text at (x, y) (its vertical middle), aligned by align (-1 right, 0 centre, 1 left), with a dark shadow to read
// on anything.
static void Text(const QuickFrame &f, Layer layer, const wchar_t *pText, int count, float x, float y, int align, bool bLarge,
	const Color &c, float a)
{
	const vgui::HFont font = Font(bLarge);
	if (font == vgui::INVALID_FONT || count <= 0)
	{
		return;
	}
	NeoGhostFlush();
	int wide, tall;
	vgui::surface()->GetTextSize(font, pText, wide, tall);
	if (count < V_wcslen(pText))
	{
		wide = wide * count / Max(1, V_wcslen(pText));
	}
	const Vector2D at = At(f, layer, x, y);
	const int tx = RoundFloatToInt(at.x) - ((align < 0) ? wide : (align == 0) ? wide / 2 : 0), ty = RoundFloatToInt(at.y) - tall / 2;
	vgui::surface()->DrawSetTextFont(font);
	vgui::surface()->DrawSetTextColor(0, 0, 0, Alpha(f, a * 0.7f));
	vgui::surface()->DrawSetTextPos(tx + 1, ty + 1);
	vgui::surface()->DrawPrintText(pText, count);
	vgui::surface()->DrawSetTextColor(c.r(), c.g(), c.b(), Alpha(f, a));
	vgui::surface()->DrawSetTextPos(tx, ty);
	vgui::surface()->DrawPrintText(pText, count);
}

// A housing line: solid, or while cloaked a whole number of dashes fitted to the run, starting and ending on its
// corners, so corners stay solid and the mirrored brackets dash identically. The dashes never move; a brightness
// wave passes along them (its phase carried across the runs by wave0). Returns the run's length.
static float Run(const QuickFrame &f, Layer layer, float x0, float y0, float x1, float y1, float a, float wave0)
{
	const float length = FastSqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
	if (!f.bCloaked || length < 0.01f)
	{
		Line(f, layer, x0, y0, x1, y1, NEO_GHOST_MEDIUM, f.color, a);
		return length;
	}
	const int n = Max(1, RoundFloatToInt((length + DASH_GAP) / (DASH + DASH_GAP)));
	const float dash = (length - DASH_GAP * (n - 1)) / n;
	const float ux = (x1 - x0) / length, uy = (y1 - y0) / length;
	const float trim = 0.7f;	// the stroke's square caps run on by half its width
	const float low = (f.cloak < 0.15f) ? ((sinf(f.now * 40.0f) > 0.0f) ? 1.0f : 0.4f) : 1.0f;
	for (int i = 0; i < n; ++i)
	{
		const float a0 = i * (dash + DASH_GAP), a1 = a0 + dash;
		const float along = wave0 + (a0 + a1) * 0.5f;
		const float wave = 0.55f + 0.45f * Max(0.0f, cosf(along * 0.045f - f.now * 5.0f));
		const float t0 = (i == 0) ? a0 : a0 + trim, t1 = (i == n - 1) ? a1 : a1 - trim;
		Line(f, layer, x0 + ux * t0, y0 + uy * t0, x0 + ux * t1, y0 + uy * t1, NEO_GHOST_MEDIUM, f.color, a * wave * low);
	}
	return length;
}

static const Color &HpColor(const QuickFrame &f)
{
	return (f.hp <= 0.25f) ? CRIT : (f.hp <= 0.5f) ? WARN : f.color;
}
// Support's armour: integrity spread over the brackets first (drained from the bottom up), then the bar.
static float SupportBar(float hp) { return clamp(hp / 0.4f, 0.0f, 1.0f); }
static float SupportSide(float hp) { return clamp((hp - 0.4f) / 0.6f, 0.0f, 1.0f); }
// Segments go dark from the outside in, a pair at a time.
static float LitFromOutside(int i, int n, float frac)
{
	const int rank = Min(i, n - 1 - i), pairs = (n + 1) / 2;
	return clamp(frac * pairs - (pairs - 1 - rank), 0.0f, 1.0f);
}

// A bar box; during a hit's hitch its top half shifts right and its bottom half left.
static void BarBox(const QuickFrame &f, float x0, float y0, float x1, float y1, const Color &c, float a, bool bGlitch)
{
	if (!bGlitch)
	{
		Box(f, LAYER_BAR, x0, y0, x1, y1, c, a);
		return;
	}
	const float mid = (y0 + y1) * 0.5f;
	Box(f, LAYER_BAR, x0 + 2.0f, y0, x1 + 2.0f, mid, c, a);
	Box(f, LAYER_BAR, x0 - 2.0f, mid, x1 - 2.0f, y1, c, a);
}

static void PaintBar(const QuickFrame &f)
{
	const bool bGlitch = f.now - f.glitchTime < 0.07f;
	const float half = BarHalf(f.kind), h = BAR_H;
	const Color &c = HpColor(f);
	const float pulse = (f.hp <= 0.25f) ? 0.55f + 0.45f * sinf(f.now * 9.0f) : 1.0f;
	if (f.kind == KIND_ASSAULT || f.kind == KIND_SUPPORT)
	{
		const int n = (f.kind == KIND_ASSAULT) ? 10 : 6;
		const float gap = (f.kind == KIND_ASSAULT) ? 2.5f : 4.0f, hh = (f.kind == KIND_SUPPORT) ? h * 1.6f : h;
		const float segW = (half * 2.0f - gap * (n - 1)) / n;
		for (int i = 0; i < n; ++i)
		{
			const float x0 = -half + i * (segW + gap);
			const float lit = LitFromOutside(i, n, (f.kind == KIND_SUPPORT) ? SupportBar(f.hp) : f.hp);
			BarBox(f, x0, BAR_Y - hh * 0.5f, x0 + segW, BAR_Y + hh * 0.5f, c, (lit > 0.0f) ? 0.25f + 0.6f * lit * pulse : 0.08f, bGlitch);
		}
	}
	else
	{
		BarBox(f, -half, BAR_Y - h * 0.5f, half, BAR_Y + h * 0.5f, f.color, 0.08f, bGlitch);
		const float hw = half * f.hp;
		BarBox(f, -hw, BAR_Y - h * 0.5f, hw, BAR_Y + h * 0.5f, c, 0.85f * pulse, bGlitch);
		if (f.kind == KIND_JUGGERNAUT)
		{
			BarBox(f, -hw, BAR_Y + h, hw, BAR_Y + h + 1.2f, c, 0.7f, bGlitch);
		}
		if (f.bDetail && f.hp <= 0.25f)
		{
			// A failing signal: faint red and cyan echoes either side.
			Box(f, LAYER_BAR, -hw - 1.5f, BAR_Y - h * 0.5f - 1.0f, hw - 1.5f, BAR_Y + h * 0.5f - 1.0f, CRIT, 0.3f);
			Box(f, LAYER_BAR, -hw + 1.5f, BAR_Y - h * 0.5f + 1.0f, hw + 1.5f, BAR_Y + h * 0.5f + 1.0f, ECHO, 0.25f);
		}
	}
	// The chips: a hit's lost chunk held bright a moment, then wiped inward to the new value.
	for (int i = 0; i < f.chips; ++i)
	{
		const Chip &chip = f.pChips[i];
		const float wipe = NeoSmoothStep((f.now - chip.time - 0.2f) / 0.25f);
		const float outer = chip.from - (chip.from - chip.to) * wipe;
		if (outer <= chip.to + 0.001f)
		{
			continue;
		}
		for (int sd = -1; sd <= 1; sd += 2)
		{
			const float xa = sd * half * chip.to, xb = sd * half * outer;
			Box(f, LAYER_BAR, xa, BAR_Y - h * 0.5f - 1.0f, xb, BAR_Y + h * 0.5f + 1.0f, f.color, 0.9f);
			Line(f, LAYER_BAR, xb, BAR_Y - h * 0.5f - 2.0f, xb, BAR_Y + h * 0.5f + 2.0f, NEO_GHOST_LIGHT, f.color, 1.0f);
		}
	}
	// The frame's end ticks, dashed with the rest while cloaked.
	float wave = 0.0f;
	for (int sd = -1; sd <= 1; sd += 2)
	{
		wave += Run(f, LAYER_BAR, sd * (half + 3.0f), BAR_Y - 5.0f, sd * (half + 3.0f), BAR_Y + 5.0f, 0.7f, wave);
	}
	// The number, flickering through digits a moment when it changes.
	wchar_t number[8];
	const bool bFlicker = f.bDetail && f.now - f.hpChangeTime < 0.09f;
	V_snwprintf(number, ARRAYSIZE(number), L"%d", bFlicker ? RandomInt(10, 98) : f.hpNumber);
	Text(f, LAYER_BAR, number, V_wcslen(number), 0.0f, BAR_Y - 12.0f, 0, true, c, 0.95f);
}

static void PaintBracket(const QuickFrame &f, int sd)
{
	const float x = sd * BX, xf = sd * (BX - FOOT);
	float wave = 0.0f;
	wave += Run(f, LAYER_BRACKET, xf, TOP, x, TOP, 0.9f, wave);
	wave += Run(f, LAYER_BRACKET, x, TOP, x, BOTTOM, 0.9f, wave);
	Run(f, LAYER_BRACKET, x, BOTTOM, xf, BOTTOM, 0.9f, wave);
}
static float TrackOuter(int sd) { return sd * (BX - FILL_GAP); }
static float TrackInner(int sd) { return sd * (BX - FILL_GAP - FILL_W); }

// A fill rising from the bottom of a bracket's track; hollow (an outline over a faint fill) while cloaked.
static void PaintFill(const QuickFrame &f, int sd, float frac, const Color &c, bool bHollow)
{
	const float x0 = TrackOuter(sd), x1 = TrackInner(sd);
	Box(f, LAYER_BRACKET, x0, FILL_TOP, x1, FILL_BOTTOM, f.color, 0.07f);
	const float top = FILL_BOTTOM - FILL_H * clamp(frac, 0.0f, 1.0f);
	if (bHollow && f.bDetail)
	{
		Box(f, LAYER_BRACKET, x0, top, x1, FILL_BOTTOM, c, 0.12f);
		Line(f, LAYER_BRACKET, x0, top, x1, top, NEO_GHOST_LIGHT, c, 0.9f);
		Line(f, LAYER_BRACKET, x0, top, x0, FILL_BOTTOM, NEO_GHOST_LIGHT, c, 0.6f);
		Line(f, LAYER_BRACKET, x1, top, x1, FILL_BOTTOM, NEO_GHOST_LIGHT, c, 0.6f);
		return;
	}
	Box(f, LAYER_BRACKET, x0, top, x1, FILL_BOTTOM, c, 0.75f);
}

// A recon's two jump cells, bottom one first: a full cell locks bright with a flash; a spent one's outline blinks
// twice.
static void PaintJumpCells(const QuickFrame &f, int sd)
{
	const float x0 = TrackOuter(sd), x1 = TrackInner(sd), gap = 4.0f, cell = (FILL_H - gap) * 0.5f;
	Box(f, LAYER_BRACKET, x0, FILL_TOP, x1, FILL_BOTTOM, f.color, 0.07f);
	for (int i = 0; i < 2; ++i)
	{
		const float bottom = FILL_BOTTOM - i * (cell + gap), top = bottom - cell;
		const float fill = clamp((f.aux - i * JUMP_COST) / JUMP_COST, 0.0f, 1.0f);
		if (fill >= 1.0f)
		{
			const float flash = Max(0.0f, 1.0f - (f.now - f.jumpReady[i]) / 0.25f);
			Box(f, LAYER_BRACKET, x0, top, x1, bottom, f.color, 0.75f + 0.25f * flash);
			if (flash > 0.0f)
			{
				Box(f, LAYER_BRACKET, sd * (BX - 1.0f), top, sd * (BX - FILL_GAP - FILL_W - 3.0f), bottom, f.color, 0.35f * flash);
			}
			continue;
		}
		Box(f, LAYER_BRACKET, x0, bottom - cell * fill, x1, bottom, f.color, 0.3f);
		Line(f, LAYER_BRACKET, x0, top, x1, top, NEO_GHOST_LIGHT, f.color, 0.4f);
		const float spent = f.now - f.jumpSpent[i];
		if (spent < 0.32f && static_cast<int>(spent / 0.08f) % 2 == 0)
		{
			Line(f, LAYER_BRACKET, x0, top, x0, bottom, NEO_GHOST_LIGHT, f.color, 1.0f);
			Line(f, LAYER_BRACKET, x1, top, x1, bottom, NEO_GHOST_LIGHT, f.color, 1.0f);
			Line(f, LAYER_BRACKET, x0, bottom, x1, bottom, NEO_GHOST_LIGHT, f.color, 1.0f);
		}
	}
}

// Sprint stamina: a tick at the minimum to start a sprint; draining, a bright edge rides the tip.
static void PaintSprint(const QuickFrame &f, int sd)
{
	PaintFill(f, sd, f.aux / 100.0f, (f.aux < 20.0f) ? WARN : f.color, false);
	const float minimum = FILL_BOTTOM - FILL_H * 0.02f - 2.0f;
	Line(f, LAYER_BRACKET, sd * (BX - FILL_GAP - FILL_W - 1.0f), minimum, sd * (BX - FILL_GAP - FILL_W - 4.0f), minimum, NEO_GHOST_LIGHT, f.color, 0.6f);
	if (f.bSprinting)
	{
		const float tip = FILL_BOTTOM - FILL_H * f.aux / 100.0f;
		Line(f, LAYER_BRACKET, sd * (BX - FILL_GAP + 1.0f), tip, sd * (BX - FILL_GAP - FILL_W - 1.0f), tip, NEO_GHOST_MEDIUM, f.color, 1.0f);
	}
}

// Instrument detail, far back and faint: graduations up each track, channel codes, registration marks.
static void PaintDetail(const QuickFrame &f)
{
	static const wchar_t *s_codes[][2] = { { L"TOC.CH2", L"JMP.CH1" }, { L"TOC.CH2", L"AUX.CH1" }, { L"ARM.CH0", L"ARM.CH0" },
		{ L"AUX.CH1", L"AUX.CH1" } };
	for (int sd = -1; sd <= 1; sd += 2)
	{
		const float xi = sd * (BX - FILL_GAP - FILL_W - 2.0f);
		for (int k = 0; k <= 10; ++k)
		{
			const bool bMajor = (k == 0 || k == 5 || k == 10);
			const float y = FILL_BOTTOM - FILL_H * k / 10.0f;
			Line(f, LAYER_DETAIL, xi, y, xi - sd * (bMajor ? 4.0f : 2.0f), y, NEO_GHOST_LIGHT, f.color, bMajor ? 0.45f : 0.25f);
		}
		const wchar_t *pCode = s_codes[f.kind][(sd < 0) ? 0 : 1];
		Text(f, LAYER_DETAIL, pCode, V_wcslen(pCode), sd * (BX + 8.0f), 40.0f, (sd < 0) ? -1 : 1, false, f.color, 0.3f);
	}
	Text(f, LAYER_DETAIL, L"INT.CH0", 7, -(BarHalf(f.kind) + 10.0f), BAR_Y, -1, false, f.color, 0.3f);
	for (int corner = 0; corner < 4; ++corner)
	{
		const float x = ((corner & 1) ? 1.0f : -1.0f) * (BX + 12.0f), y = ((corner & 2) ? 1.0f : -1.0f) * (BOTTOM + 12.0f);
		Line(f, LAYER_DETAIL, x - 2.5f, y, x + 2.5f, y, NEO_GHOST_LIGHT, f.color, 0.35f);
		Line(f, LAYER_DETAIL, x, y - 2.5f, x, y + 2.5f, NEO_GHOST_LIGHT, f.color, 0.35f);
	}
}

void PaintHousing(const QuickFrame &f)
{
	if (f.bDetail)
	{
		PaintDetail(f);
	}
	PaintBar(f);
	for (int sd = -1; sd <= 1; sd += 2)
	{
		PaintBracket(f, sd);
	}
	switch (f.kind)
	{
	case KIND_RECON:
		PaintFill(f, -1, f.cloak, (f.cloak < 0.2f) ? WARN : f.color, f.bCloaked);
		PaintJumpCells(f, 1);
		break;
	case KIND_ASSAULT:
		PaintFill(f, -1, f.cloak, (f.cloak < 0.2f) ? WARN : f.color, f.bCloaked);
		PaintSprint(f, 1);
		break;
	case KIND_SUPPORT:
		for (int sd = -1; sd <= 1; sd += 2)
		{
			PaintFill(f, sd, SupportSide(f.hp), HpColor(f), false);
		}
		break;
	case KIND_JUGGERNAUT:
		for (int sd = -1; sd <= 1; sd += 2)
		{
			PaintSprint(f, sd);
		}
		break;
	}
	// Vision: four static dots, lit while the mode is on (it has no drain, so nothing moves).
	if (f.bHasVision)
	{
		const float r = f.bVision ? 1.0f : 0.65f;
		for (int corner = 0; corner < 4; ++corner)
		{
			const float x = ((corner & 1) ? 1.0f : -1.0f) * DOT, y = ((corner & 2) ? 1.0f : -1.0f) * DOT;
			Box(f, LAYER_DOTS, x - r, y - r, x + r, y + r, f.color, f.bVision ? 1.0f : 0.3f);
		}
	}
	NeoGhostFlush();
}

// The spawn labels, typed in and back out, placed clear of the housing and of each other: above the number, beside
// the brackets' upper halves (the channel codes sit by the lower halves), below the bottom.
void PaintLabels(const QuickFrame &f)
{
	if (f.labels < 0.0f || f.labels > 3.4f)
	{
		return;
	}
	const float t = f.labels;
	const float typed = (t < 2.8f) ? clamp((t - 0.25f) / 0.45f, 0.0f, 1.0f) : clamp(1.0f - (t - 2.8f) / 0.35f, 0.0f, 1.0f);
	const auto put = [&](const wchar_t *pText, float x, float y, int align) {
		const int length = V_wcslen(pText);
		const int count = RoundFloatToInt(length * typed);
		if (count <= 0)
		{
			return;
		}
		wchar_t shown[32];
		V_wcsncpy(shown, pText, sizeof(shown));
		int shownCount = count;
		if (count < length && count < ARRAYSIZE(shown) - 1)
		{
			shown[count] = L'_';
			shownCount = count + 1;
		}
		Text(f, LAYER_LABELS, shown, shownCount, x, y, align, true, f.color, 1.0f);
	};
	static const wchar_t *s_left[] = { L"THERM-OPTIC", L"THERM-OPTIC", L"ARMOUR", L"AUX" };
	static const wchar_t *s_right[] = { L"JUMP", L"AUX", L"ARMOUR", L"AUX" };
	put(L"INTEGRITY", 0.0f, BAR_Y - 30.0f, 0);
	put(s_left[f.kind], -(BX + 12.0f), -20.0f, -1);
	put(s_right[f.kind], BX + 12.0f, -20.0f, 1);
	if (f.bHasVision && f.pVisionName)
	{
		wchar_t vision[32];
		V_UTF8ToUnicode(f.pVisionName, vision, sizeof(vision));
		put(vision, 0.0f, BOTTOM + 20.0f, 0);
	}
	NeoGhostFlush();
}
} // namespace NeoQuickInfo
