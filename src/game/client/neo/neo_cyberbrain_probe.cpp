#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The layout probe (HUD-REWORK.md, R6): every text box and plate the cyberbrain draws this frame, from both of its
// elements (the groups and the team side), with its owner; with cl_neo_hud_layout_debug 1, every pair that crosses is
// outlined in red and printed once a second. Same-owner pairs count too: the weapon stack's collisions are inside one
// group. A frame's boxes are checked when the next frame starts, so what's drawn is a frame old.

ConVar cl_neo_hud_layout_debug("cl_neo_hud_layout_debug", "0", FCVAR_NONE,
	"The cyberbrain HUD's layout probe: 1 = outline in red every two text boxes that cross, and print the pairs once a"
	" second.", true, 0, true, 1);

namespace NeoCyberbrain
{
constexpr int PROBE_BOXES = 256, PROBE_PAIRS = 64, PROBE_TEXT = 20;
constexpr float PROBE_FROM = 0.05f;	// fainter than this doesn't count as drawn
constexpr float PROBE_SLACK = 1.0f;	// pixels two boxes may share (antialiased edges)

static const char *s_owners[PROBE__COUNT] = { "body", "optics", "weapon", "link", "motion", "ring", "score", "squad", "feed" };

struct ProbeBox { Vector2D lo, hi; int owner; wchar_t text[PROBE_TEXT]; };
struct ProbePair { Vector2D lo, hi; int a, b; };
static struct
{
	int frame = -1, owner = PROBE_BODY;
	ProbeBox boxes[PROBE_BOXES];
	int count = 0;
	ProbePair pairs[PROBE_PAIRS];
	int pairCount = 0;
	ProbeBox shown[PROBE_BOXES];	// the checked frame's boxes, for the pairs' names
	float printed = -100.0f;
} s_probe;

static bool ProbeOn()
{
	return cl_neo_hud_layout_debug.GetBool();
}

// The frame just painted: find its crossing pairs.
static void ProbeCheck()
{
	s_probe.pairCount = 0;
	for (int i = 0; i < s_probe.count; ++i)
	{
		s_probe.shown[i] = s_probe.boxes[i];
		for (int j = i + 1; j < s_probe.count && s_probe.pairCount < PROBE_PAIRS; ++j)
		{
			const ProbeBox &a = s_probe.boxes[i], &b = s_probe.boxes[j];
			const Vector2D lo(Max(a.lo.x, b.lo.x), Max(a.lo.y, b.lo.y)), hi(Min(a.hi.x, b.hi.x), Min(a.hi.y, b.hi.y));
			if (hi.x - lo.x <= PROBE_SLACK || hi.y - lo.y <= PROBE_SLACK)
				continue;
			s_probe.pairs[s_probe.pairCount++] = { lo, hi, i, j };
		}
	}
	const float now = gpGlobals->realtime;
	if (s_probe.pairCount > 0 && now - s_probe.printed >= 1.0f)
	{
		s_probe.printed = now;
		Msg("[cyberbrain layout] %d crossing text pair%s:\n", s_probe.pairCount, s_probe.pairCount == 1 ? "" : "s");
		for (int p = 0; p < s_probe.pairCount; ++p)
		{
			const ProbePair &pair = s_probe.pairs[p];
			const ProbeBox &a = s_probe.shown[pair.a], &b = s_probe.shown[pair.b];
			Msg("  %-6s \"%ls\" x %-6s \"%ls\": %.0f x %.0f px\n", s_owners[a.owner], a.text, s_owners[b.owner], b.text,
				pair.hi.x - pair.lo.x, pair.hi.y - pair.lo.y);
		}
	}
}

static void ProbeFrame()
{
	if (s_probe.frame == gpGlobals->framecount)
		return;
	if (s_probe.frame >= 0)
		ProbeCheck();
	s_probe.frame = gpGlobals->framecount;
	s_probe.count = 0;
}

void ProbeOwner(ProbeOwnerId owner)
{
	s_probe.owner = owner;
}

void ProbeText(const Vector2D &lo, const Vector2D &hi, const wchar_t *pText, float alpha)
{
	if (!ProbeOn() || alpha < PROBE_FROM)
		return;
	ProbeFrame();
	if (s_probe.count >= PROBE_BOXES)
		return;
	ProbeBox &box = s_probe.boxes[s_probe.count++];
	box.lo = lo;
	box.hi = hi;
	box.owner = s_probe.owner;
	V_wcsncpy(box.text, pText ? pText : L"", sizeof(box.text));
}

void PaintProbe()
{
	if (!ProbeOn())
		return;
	ProbeFrame();	// a frame with no text still clears the last one's
	NeoGhostFlush();
	for (int p = 0; p < s_probe.pairCount; ++p)
	{
		const ProbePair &pair = s_probe.pairs[p];
		for (const int k : { pair.a, pair.b })
		{
			const ProbeBox &box = s_probe.shown[k];
			vgui::surface()->DrawSetColor(255, 90, 74, 140);
			vgui::surface()->DrawOutlinedRect(RoundFloatToInt(box.lo.x) - 1, RoundFloatToInt(box.lo.y) - 1, RoundFloatToInt(box.hi.x) + 1,
				RoundFloatToInt(box.hi.y) + 1);
		}
		vgui::surface()->DrawSetColor(255, 40, 30, 110);
		vgui::surface()->DrawFilledRect(RoundFloatToInt(pair.lo.x), RoundFloatToInt(pair.lo.y), RoundFloatToInt(pair.hi.x), RoundFloatToInt(pair.hi.y));
	}
}
} // namespace NeoCyberbrain
