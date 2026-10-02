#pragma once

#include "tier0/vprof.h"

// Timing and counts for the HUD (HUD-SYSTEM.md). NEO_HUD_PROFILE(section, "name") at the top of a block: a VPROF
// budget scope in the "NEO HUD" group (+showbudget, vprof_generate_report), and the time adds up in its section,
// which neo_hud_bench reads per frame. NeoHudCount adds to a per-frame count (text prints, batch draws...), which
// says more than desktop microseconds do about a low-end machine.
enum NeoHudProfileSection
{
	NEO_HUD_PROFILE_CROSSHAIR,		// the crosshair layer
	NEO_HUD_PROFILE_VITALS,			// the style's own vitals: the cyberbrain's groups and ring, the racer band, Competitive's
	NEO_HUD_PROFILE_TEAM,			// score, squad and kill feed (the cyberbrain's team side, Competitive's)
	NEO_HUD_PROFILE_GUN,			// the cyberbrain's viewmodel pass: its prep every frame, the reload scan
	// The cyberbrain's vitals in parts, timed inside NEO_HUD_PROFILE_VITALS (so they don't add to it): what it reads
	// and where the groups go, the brightness rays, the backings and chassis, the ring, the groups and the last flush.
	NEO_HUD_PROFILE_VITALS_SENSE,
	NEO_HUD_PROFILE_VITALS_BRIGHT,
	NEO_HUD_PROFILE_VITALS_BACKING,
	NEO_HUD_PROFILE_VITALS_RING,
	NEO_HUD_PROFILE_VITALS_GROUPS,
	NEO_HUD_PROFILE_VITALS_FRAME,		// the frame, the grid's corners and the couplings (split from backing 2026-10-01)
	NEO_HUD_PROFILE_VITALS_WORDS,		// the language budget (once a frame, inside the groups' draw; timed on its own)
	// Slices of v.groups and v.frame (OPTIMIZATION.md step 1): each group's paint with its measure, its layer, the
	// groups' last flush; the frame against the couplings. Nested, so they don't add to their parent either.
	NEO_HUD_PROFILE_GROUP_BODY,
	NEO_HUD_PROFILE_GROUP_OPTICS,
	NEO_HUD_PROFILE_GROUP_WEAPON,
	NEO_HUD_PROFILE_GROUP_MOTION,
	NEO_HUD_PROFILE_GROUP_LAYER,
	NEO_HUD_PROFILE_GROUP_FLUSH,
	NEO_HUD_PROFILE_FRAME_FRAME,
	NEO_HUD_PROFILE_FRAME_COUPLE,
	// Slices of f.frame: the faction mark, the crosses (or the grid's corners), the rulers, the channel codes.
	NEO_HUD_PROFILE_FRAME_MARK,
	NEO_HUD_PROFILE_FRAME_CROSSES,
	NEO_HUD_PROFILE_FRAME_RULER,
	NEO_HUD_PROFILE_FRAME_CODES,
	// All the HUD's text, wherever it's drawn (OPTIMIZATION.md, "where the 9 us goes"): measuring a string
	// (GetTextSize), printing it (NeoHudPrintText), and the cyberbrain's bookkeeping per string (its measure, the probe).
	// Nested inside the parts above, so they don't add to them either.
	NEO_HUD_PROFILE_TEXT_SIZE,
	NEO_HUD_PROFILE_TEXT_PRINT,
	NEO_HUD_PROFILE_TEXT_PROBE,
	NEO_HUD_PROFILE_BATCH_FLUSH,	// NeoGhostFlush with quads waiting: the stroke batch's mesh built and drawn
	NEO_HUD_PROFILE_BATCH_BUILD,	// inside b.flush: writing the quads' vertices
	NEO_HUD_PROFILE_BATCH_DRAW,		// inside b.flush: the mesh's End and Draw (the rest of b.flush: getting the mesh, Begin)
	NEO_HUD_PROFILE__COUNT,
};

enum NeoHudCounter
{
	NEO_HUD_COUNT_TEXT,				// DrawPrintText calls
	NEO_HUD_COUNT_MESHES,			// batched mesh draws (one per non-empty flush, two with outlines)
	NEO_HUD_COUNT_QUADS,			// quads through the stroke batch, outlines included
	NEO_HUD_COUNT_RAYS,				// brightness rays into the world
	NEO_HUD_COUNT_EXTENTS,			// the cyberbrain's GroupExtent calls
	NEO_HUD_COUNT_FLUSHES,			// NeoGhostFlush calls, empty ones included
	NEO_HUD_COUNT_GLYPHS,			// characters through DrawPrintText (each print's length, edge copies included)
	NEO_HUD_COUNT__COUNT,
};

#define NEO_HUD_VPROF_GROUP "NEO HUD"

class CNeoHudProfileScope
{
public:
	explicit CNeoHudProfileScope(NeoHudProfileSection section);
	~CNeoHudProfileScope();
	// Closes this section's time and goes on timing the next, for a block in parts.
	void Switch(NeoHudProfileSection next);
private:
	NeoHudProfileSection m_section;
	double m_start;
};

void NeoHudCount(NeoHudCounter counter, int amount = 1);
// The section's time since the last call, in milliseconds, and resets it.
double NeoHudProfileTakeMs(NeoHudProfileSection section);
// The counter's total since the last call, and resets it.
int NeoHudProfileTakeCount(NeoHudCounter counter);
const char *NeoHudProfileSectionName(NeoHudProfileSection section);
const char *NeoHudProfileCounterName(NeoHudCounter counter);

#define NEO_HUD_PROFILE(section, name) \
	VPROF_BUDGET(name, NEO_HUD_VPROF_GROUP); \
	CNeoHudProfileScope neoHudProfileScope(section)
