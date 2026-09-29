#include "cbase.h"
#include "neo_quickinfo_internal.h"
#include "neo_ironsights.h"
#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The band's drawing (QUICKINFO.md): the integrity bar and its number, the wings, the instrument detail, the spawn
// labels. Every position is in pixels at 1080p from the screen's centre, on a layer that sways by its depth.

namespace NeoQuickInfo
{
static const Color CRIT(255, 81, 99, 255);
static const Color ECHO(90, 220, 255, 255);	// the failing signal's cyan echo

constexpr float DASH = 4.0f, DASH_GAP = 3.0f;	// the cloak's dashes, fitted per run
constexpr float ETCHED = 0.75f;				// the channel codes' opacity: faint lines, but text you can read
constexpr float ETCHED_FLOOR = 0.5f;			// and they fade only this far at rest (the band goes to its floor)
constexpr float WING_FRAME_SHARE = 0.7f;		// a wing's frame rises with its fill this much
constexpr float CELL = 0.48f;					// a recon's jump cell, of the wing's length (the rest is the gap)

// A frame line: solid, or while cloaked a whole number of dashes fitted to the run, starting and ending on its
// corners, so corners stay solid and the mirrored wings dash identically. The dashes never move; a brightness
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

// A point along a wing, t from its inner end (0) to its tip (1), side sd (-1 left, 1 right).
static float WingX(int sd, float t) { return sd * (WING_IN + (WING_OUT - WING_IN) * t); }
static float WingY(float t) { return BAND_Y - WING_RISE * t; }

static const Color &HpColor(const QuickFrame &f)
{
	return (f.hp <= 0.25f) ? CRIT : (f.hp <= 0.5f) ? WARN : f.color;
}
// Support's armour: integrity spread over the wings first (drained from the tips in), then the bar.
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

// Integrity: segmented per class (recon fine, assault heavier, support in plates, the juggernaut one heavy doubled
// bar), draining from both ends in; the number above it.
static void PaintBar(const QuickFrame &f)
{
	const bool bGlitch = f.now - f.glitchTime < 0.07f;
	const Color &c = HpColor(f);
	const float pulse = (f.hp <= 0.25f) ? 0.55f + 0.45f * sinf(f.now * 9.0f) : 1.0f;
	const float frac = (f.kind == KIND_SUPPORT) ? SupportBar(f.hp) : f.hp;
	const float h = (f.kind == KIND_SUPPORT) ? BAR_H * 1.5f : BAR_H, y0 = BAND_Y - h * 0.5f, y1 = BAND_Y + h * 0.5f;
	if (f.bDetail && f.hp <= 0.25f)
	{
		// A failing signal: faint red and cyan echoes either side.
		const float hw = BAR_HALF * frac;
		Box(f, LAYER_BAR, -hw - 2.0f, y0 - 1.5f, hw - 2.0f, y1 - 1.5f, CRIT, 0.3f);
		Box(f, LAYER_BAR, -hw + 2.0f, y0 + 1.5f, hw + 2.0f, y1 + 1.5f, ECHO, 0.25f);
	}
	const int n = (f.kind == KIND_RECON) ? 20 : (f.kind == KIND_ASSAULT) ? 10 : (f.kind == KIND_SUPPORT) ? 6 : 1;
	if (n > 1)
	{
		const float gap = (f.kind == KIND_SUPPORT) ? 5.0f : 3.0f;
		const float segW = (BAR_HALF * 2.0f - gap * (n - 1)) / n;
		for (int i = 0; i < n; ++i)
		{
			const float x0 = -BAR_HALF + i * (segW + gap);
			const float lit = LitFromOutside(i, n, frac);
			BarBox(f, x0, y0, x0 + segW, y1, c, (lit > 0.0f) ? 0.25f + 0.6f * lit * pulse : 0.08f, bGlitch);
		}
	}
	else
	{
		BarBox(f, -BAR_HALF, y0, BAR_HALF, y1, f.color, 0.08f, bGlitch);
		const float hw = BAR_HALF * frac;
		BarBox(f, -hw, y0, hw, y1, c, 0.85f * pulse, bGlitch);
		BarBox(f, -hw, y1 + 2.0f, hw, y1 + 3.5f, c, 0.7f, bGlitch);	// the juggernaut's second line
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
			const float xa = sd * BAR_HALF * chip.to, xb = sd * BAR_HALF * outer;
			Box(f, LAYER_BAR, xa, y0 - 1.0f, xb, y1 + 1.0f, f.color, 0.9f);
			Line(f, LAYER_BAR, xb, y0 - 2.0f, xb, y1 + 2.0f, NEO_GHOST_LIGHT, f.color, 1.0f);
		}
	}
	// The end ticks, dashed with the frames while cloaked.
	float wave = 0.0f;
	for (int sd = -1; sd <= 1; sd += 2)
	{
		wave += Run(f, LAYER_BAR, sd * (BAR_HALF + 4.0f), BAND_Y - 6.0f, sd * (BAR_HALF + 4.0f), BAND_Y + 6.0f, 0.7f, wave);
	}
	// The number, flickering through digits a moment when it changes.
	wchar_t number[8];
	const bool bFlicker = f.bDetail && f.now - f.hpChangeTime < 0.09f;
	V_snwprintf(number, ARRAYSIZE(number), L"%d", bFlicker ? RandomInt(10, 98) : f.hpNumber);
	Text(f, LAYER_BAR, number, V_wcslen(number), 0.0f, NUMBER_Y, 0, FONT_LARGE, c, 0.95f);
}

// A wing's frame: a parallelogram around its fill, upright ends, its long sides on the wing's slant.
static void PaintWingFrame(const QuickFrame &f, int sd)
{
	const float xi = WingX(sd, 0.0f), xo = WingX(sd, 1.0f), yi = WingY(0.0f), yo = WingY(1.0f);
	float wave = 0.0f;
	wave += Run(f, LAYER_FRAME, xi, yi - WING_FRAME, xo, yo - WING_FRAME, 0.9f, wave);
	wave += Run(f, LAYER_FRAME, xo, yo - WING_FRAME, xo, yo + WING_FRAME, 0.9f, wave);
	wave += Run(f, LAYER_FRAME, xo, yo + WING_FRAME, xi, yi + WING_FRAME, 0.9f, wave);
	Run(f, LAYER_FRAME, xi, yi + WING_FRAME, xi, yi - WING_FRAME, 0.9f, wave);
}

// A fill out along a wing from its inner end; hollow (an outline over a faint fill) while cloaked.
static void PaintFill(const QuickFrame &f, int sd, float frac, const Color &c, bool bHollow)
{
	frac = clamp(frac, 0.0f, 1.0f);
	const float xi = WingX(sd, 0.0f), yi = WingY(0.0f), xt = WingX(sd, frac), yt = WingY(frac), half = FILL_H * 0.5f;
	Strip(f, LAYER_BAR, xi, yi, WingX(sd, 1.0f), WingY(1.0f), FILL_H, f.color, 0.07f);
	if (bHollow && f.bDetail)
	{
		Strip(f, LAYER_BAR, xi, yi, xt, yt, FILL_H, c, 0.12f);
		Line(f, LAYER_BAR, xi, yi - half, xt, yt - half, NEO_GHOST_LIGHT, c, 0.6f);
		Line(f, LAYER_BAR, xi, yi + half, xt, yt + half, NEO_GHOST_LIGHT, c, 0.6f);
		Line(f, LAYER_BAR, xt, yt - half, xt, yt + half, NEO_GHOST_LIGHT, c, 0.9f);
		return;
	}
	Strip(f, LAYER_BAR, xi, yi, xt, yt, FILL_H, c, 0.75f);
}

// A recon's two jump cells, the inner one first: a full cell locks bright with a flash; a spent one's outline
// blinks twice.
static void PaintJumpCells(const QuickFrame &f, int sd)
{
	const float half = FILL_H * 0.5f;
	for (int i = 0; i < 2; ++i)
	{
		const float a = i * (1.0f - CELL), b = a + CELL;
		const float xa = WingX(sd, a), ya = WingY(a), xb = WingX(sd, b), yb = WingY(b);
		Strip(f, LAYER_BAR, xa, ya, xb, yb, FILL_H, f.color, 0.07f);
		const float fill = clamp((f.aux - i * JUMP_COST) / JUMP_COST, 0.0f, 1.0f);
		if (fill >= 1.0f)
		{
			const float flash = Max(0.0f, 1.0f - (f.now - f.jumpReady[i]) / 0.25f);
			Strip(f, LAYER_BAR, xa, ya, xb, yb, FILL_H, f.color, 0.75f + 0.25f * flash);
			if (flash > 0.0f)
			{
				Strip(f, LAYER_BAR, xa, ya, xb, yb, FILL_H + 6.0f, f.color, 0.35f * flash);
			}
			continue;
		}
		const float t = a + (b - a) * fill;
		Strip(f, LAYER_BAR, xa, ya, WingX(sd, t), WingY(t), FILL_H, f.color, 0.3f);
		Line(f, LAYER_BAR, xb, yb - half, xb, yb + half, NEO_GHOST_LIGHT, f.color, 0.4f);
		const float spent = f.now - f.jumpSpent[i];
		if (spent < 0.32f && static_cast<int>(spent / 0.08f) % 2 == 0)
		{
			Line(f, LAYER_BAR, xa, ya - half, xb, yb - half, NEO_GHOST_LIGHT, f.color, 1.0f);
			Line(f, LAYER_BAR, xa, ya + half, xb, yb + half, NEO_GHOST_LIGHT, f.color, 1.0f);
			Line(f, LAYER_BAR, xa, ya - half, xa, ya + half, NEO_GHOST_LIGHT, f.color, 1.0f);
		}
	}
}

// Sprint stamina: a tick at the minimum to start a sprint; draining, a bright edge rides the tip.
static void PaintSprint(const QuickFrame &f, int sd)
{
	const float t = f.aux / 100.0f, half = FILL_H * 0.5f;
	PaintFill(f, sd, t, (f.aux < 20.0f) ? WARN : f.color, false);
	const float xm = WingX(sd, 0.02f), ym = WingY(0.02f);
	Line(f, LAYER_BAR, xm, ym - half - 1.0f, xm, ym - half - 3.5f, NEO_GHOST_LIGHT, f.color, 0.6f);
	if (f.bSprinting)
	{
		const float xt = WingX(sd, t), yt = WingY(t);
		Line(f, LAYER_BAR, xt, yt - half - 1.0f, xt, yt + half + 1.0f, NEO_GHOST_MEDIUM, f.color, 1.0f);
	}
}

// Instrument detail, far back and faint: graduations under the bar and each wing, the etched rail with its
// registration marks, and the channel codes on one baseline: at the left tip, centred, at the right tip.
static void PaintDetail(const QuickFrame &f)
{
	static const wchar_t *s_codes[][2] = { { L"TOC.CH2", L"JMP.CH1" }, { L"TOC.CH2", L"AUX.CH1" }, { L"ARM.CH0", L"ARM.CH0" },
		{ L"AUX.CH1", L"AUX.CH1" } };
	for (int k = 0; k <= 4; ++k)
	{
		if (k == 2)
		{
			continue;	// the number sits there
		}
		const bool bMajor = (k % 2 == 0);
		const float x = -BAR_HALF + k * BAR_HALF * 0.5f;
		Line(f, LAYER_DETAIL, x, BAND_Y + 10.0f, x, BAND_Y + (bMajor ? 17.0f : 14.0f), NEO_GHOST_LIGHT, f.color, bMajor ? 0.45f : 0.25f);
	}
	for (int sd = -1; sd <= 1; sd += 2)
	{
		for (int k = 0; k <= 10; ++k)
		{
			const bool bMajor = (k == 0 || k == 5 || k == 10);
			const float t = k / 10.0f, x = WingX(sd, t), y = WingY(t) + WING_FRAME + 3.0f;
			Line(f, LAYER_DETAIL, x, y, x, y + (bMajor ? 6.0f : 3.0f), NEO_GHOST_LIGHT, f.color, bMajor ? 0.45f : 0.25f);
		}
		// Registration crosses: at the rail's end, over the bar's end (above the ammo), past the wing's tip.
		Cross(f, LAYER_DETAIL, sd * (WING_OUT + 16.0f), RAIL_Y, 0.45f);
		Cross(f, LAYER_DETAIL, sd * BAR_HALF, HEADER_Y - 16.0f, 0.45f);
		Cross(f, LAYER_DETAIL, sd * (WING_OUT + 16.0f), BAND_Y - WING_RISE, 0.45f);
	}
	// The rail as a ruler: a fine tick every tenth of each half, a longer one at the centre.
	Line(f, LAYER_DETAIL, -WING_OUT, RAIL_Y, WING_OUT, RAIL_Y, NEO_GHOST_LIGHT, f.color, 0.35f);
	for (int k = -10; k <= 10; ++k)
	{
		const float x = k * WING_OUT / 10.0f;
		const float up = (k == 0) ? 5.0f : 0.0f, down = (k == 0) ? 5.0f : (k % 5 == 0) ? 4.5f : 2.5f;
		Line(f, LAYER_DETAIL, x, RAIL_Y - up, x, RAIL_Y + down, NEO_GHOST_LIGHT, f.color, (k == 0) ? 0.45f : 0.3f);
	}
	// The codes: faint at 30% of a band already faded to its floor they were near invisible.
	QuickFrame etched = f;
	etched.alpha = Max(f.alpha, ETCHED_FLOOR * f.reveal);
	const wchar_t *pLeft = s_codes[f.kind][0], *pRight = s_codes[f.kind][1];
	Text(etched, LAYER_DETAIL, pLeft, V_wcslen(pLeft), -WING_OUT, CODES_Y, 1, FONT_SMALL, f.color, ETCHED);
	Text(etched, LAYER_DETAIL, L"INT.CH0", 7, 0.0f, CODES_Y, 0, FONT_SMALL, f.color, ETCHED);
	Text(etched, LAYER_DETAIL, pRight, V_wcslen(pRight), WING_OUT, CODES_Y, -1, FONT_SMALL, f.color, ETCHED);
}

void PaintBand(const QuickFrame &f)
{
	// The chassis (frames, rail, codes) at its steady fade; each part at its own. A wing's frame brightens a little
	// with its fill, so a lit wing isn't a bright bar in a dim outline.
	if (f.bDetail)
	{
		PaintDetail(f);
	}
	const QuickFrame wings[2] = { WithAlpha(f, f.parts[PART_LEFT]), WithAlpha(f, f.parts[PART_RIGHT]) };
	for (int sd = -1; sd <= 1; sd += 2)
	{
		PaintWingFrame(WithAlpha(f, Max(f.alpha, WING_FRAME_SHARE * wings[(sd < 0) ? 0 : 1].alpha)), sd);
	}
	const QuickFrame &left = wings[0], &right = wings[1];
	switch (f.kind)
	{
	case KIND_RECON:
		PaintFill(left, -1, f.cloak, (f.cloak < 0.2f) ? WARN : f.color, f.bCloaked);
		PaintJumpCells(right, 1);
		break;
	case KIND_ASSAULT:
		PaintFill(left, -1, f.cloak, (f.cloak < 0.2f) ? WARN : f.color, f.bCloaked);
		PaintSprint(right, 1);
		break;
	case KIND_SUPPORT:
		PaintFill(left, -1, SupportSide(f.hp), HpColor(f), false);
		PaintFill(right, 1, SupportSide(f.hp), HpColor(f), false);
		break;
	case KIND_JUGGERNAUT:
		PaintSprint(left, -1);
		PaintSprint(right, 1);
		break;
	}
	PaintBar(WithAlpha(f, f.parts[PART_INTEGRITY]));
	// Vision: four static dots either side of integrity's number, lit while the mode is on (no drain, so nothing moves).
	if (f.bHasVision)
	{
		const QuickFrame dots = WithAlpha(f, f.parts[PART_VISION]);
		const float r = f.bVision ? 1.5f : 1.0f;
		for (int sd = -1; sd <= 1; sd += 2)
		{
			for (const float x : { DOT_NEAR, DOT_FAR })
			{
				Box(dots, LAYER_DOTS, sd * x - r, NUMBER_Y - r, sd * x + r, NUMBER_Y + r, f.color, f.bVision ? 1.0f : 0.3f);
			}
		}
	}
	NeoGhostFlush();
}

// The spawn labels, typed in and back out: each part's name under its channel code, on the code's own anchor, and
// the vision mode above the ammo. Nothing shares a row, so nothing overlaps.
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
		Text(f, LAYER_LABELS, shown, shownCount, x, y, align, FONT_SMALL, f.color, 1.0f);
	};
	static const wchar_t *s_left[] = { L"THERM-OPTIC", L"THERM-OPTIC", L"ARMOUR", L"AUX" };
	static const wchar_t *s_right[] = { L"JUMP", L"AUX", L"ARMOUR", L"AUX" };
	put(s_left[f.kind], -WING_OUT, LABELS_Y, 1);
	put(L"INTEGRITY", 0.0f, LABELS_Y, 0);
	put(s_right[f.kind], WING_OUT, LABELS_Y, -1);
	if (f.bHasVision && f.pVisionName)
	{
		wchar_t vision[32];
		V_UTF8ToUnicode(f.pVisionName, vision, sizeof(vision));
		put(vision, 0.0f, HEADER_Y - 30.0f, 0);
	}
	NeoGhostFlush();
}
} // namespace NeoQuickInfo
