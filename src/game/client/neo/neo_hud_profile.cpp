#include "cbase.h"
#include "neo_hud_profile.h"
#include "tier0/platform.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static double s_hudAccumulated[NEO_HUD_PROFILE__COUNT];	// seconds
static int s_hudCounts[NEO_HUD_COUNT__COUNT];
static double s_hudQuads[NEO_HUD_PROFILE__COUNT];	// queued while each section ran, since cl_neo_hud_quads last reset
int g_neoHudQueuedQuads = 0;
bool g_neoHudProfileOn = false;
bool g_neoHudQuadsCounting = false;

CNeoHudProfileScope::CNeoHudProfileScope(NeoHudProfileSection section)
	: m_section(section), m_start(g_neoHudProfileOn ? Plat_FloatTime() : 0.0), m_queued(g_neoHudQueuedQuads), m_on(g_neoHudProfileOn)
{
}

CNeoHudProfileScope::~CNeoHudProfileScope()
{
	if (!m_on)
		return;
	s_hudAccumulated[m_section] += Plat_FloatTime() - m_start;
	s_hudQuads[m_section] += g_neoHudQueuedQuads - m_queued;
}

void CNeoHudProfileScope::Switch(NeoHudProfileSection next)
{
	if (!m_on)
	{
		m_section = next;
		return;
	}
	const double now = Plat_FloatTime();
	s_hudAccumulated[m_section] += now - m_start;
	s_hudQuads[m_section] += g_neoHudQueuedQuads - m_queued;
	m_queued = g_neoHudQueuedQuads;
	m_section = next;
	m_start = now;
}

void NeoHudCount(NeoHudCounter counter, int amount)
{
	if (!g_neoHudProfileOn)
		return;
	s_hudCounts[counter] += amount;
}

double NeoHudProfileTakeMs(NeoHudProfileSection section)
{
	const double seconds = s_hudAccumulated[section];
	s_hudAccumulated[section] = 0.0;
	return 1000.0 * seconds;
}

int NeoHudProfileTakeCount(NeoHudCounter counter)
{
	const int count = s_hudCounts[counter];
	s_hudCounts[counter] = 0;
	return count;
}

const char *NeoHudProfileSectionName(NeoHudProfileSection section)
{
	static const char *const s_hudNames[NEO_HUD_PROFILE__COUNT] = { "crosshair", "vitals", "team", "gun", "v.sense", "v.bright",
		"v.backing", "v.ring", "v.groups", "v.frame", "v.words", "g.body", "g.optics", "g.weapon", "g.motion", "g.layer",
		"g.flush", "f.frame", "f.couple", "f.mark", "f.crosses",
		"f.ruler", "f.codes", "t.size", "t.print", "t.probe", "b.flush", "b.build", "b.draw", "x.extent" };
	return (section >= 0 && section < NEO_HUD_PROFILE__COUNT) ? s_hudNames[section] : "";
}

const char *NeoHudProfileCounterName(NeoHudCounter counter)
{
	static const char *const s_hudNames[NEO_HUD_COUNT__COUNT] = { "texts", "meshes", "quads", "rays", "extents", "flushes", "glyphs" };
	return (counter >= 0 && counter < NEO_HUD_COUNT__COUNT) ? s_hudNames[counter] : "";
}

// Where the batch's quads come from: the first call starts counting, the next prints each section's quads per frame since.
CON_COMMAND(cl_neo_hud_quads, "Quads queued into the HUD's stroke batch per frame, by profile section, since the last call (the first call only starts counting).")
{
	static int s_startFrame = -1, s_startQueued = 0;
	const int frames = gpGlobals->framecount - s_startFrame;
	if (s_startFrame >= 0 && frames > 0)
	{
		for (int i = 0; i < NEO_HUD_PROFILE__COUNT; ++i)
		{
			if (s_hudQuads[i] > 0.0)
				Msg("[hud quads] %-10s %7.1f\n", NeoHudProfileSectionName(static_cast<NeoHudProfileSection>(i)), s_hudQuads[i] / frames);
		}
		Msg("[hud quads] all        %7.1f a frame, over %d frames\n", static_cast<double>(g_neoHudQueuedQuads - s_startQueued) / frames, frames);
	}
	for (double &q : s_hudQuads)
		q = 0.0;
	g_neoHudQuadsCounting = true;
	s_startFrame = gpGlobals->framecount;
	s_startQueued = g_neoHudQueuedQuads;
}
