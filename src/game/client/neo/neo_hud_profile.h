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
	NEO_HUD_PROFILE__COUNT,
};

enum NeoHudCounter
{
	NEO_HUD_COUNT_TEXT,				// DrawPrintText calls
	NEO_HUD_COUNT_MESHES,			// batched mesh draws (one per non-empty flush, two with outlines)
	NEO_HUD_COUNT_QUADS,			// quads through the stroke batch, outlines included
	NEO_HUD_COUNT_RAYS,				// brightness rays into the world
	NEO_HUD_COUNT__COUNT,
};

#define NEO_HUD_VPROF_GROUP "NEO HUD"

class CNeoHudProfileScope
{
public:
	explicit CNeoHudProfileScope(NeoHudProfileSection section);
	~CNeoHudProfileScope();
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
