#include "cbase.h"
#include "neo_hud_profile.h"
#include "tier0/platform.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static double s_hudAccumulated[NEO_HUD_PROFILE__COUNT];	// seconds
static int s_hudCounts[NEO_HUD_COUNT__COUNT];

CNeoHudProfileScope::CNeoHudProfileScope(NeoHudProfileSection section)
	: m_section(section), m_start(Plat_FloatTime())
{
}

CNeoHudProfileScope::~CNeoHudProfileScope()
{
	s_hudAccumulated[m_section] += Plat_FloatTime() - m_start;
}

void NeoHudCount(NeoHudCounter counter, int amount)
{
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
	static const char *const s_hudNames[NEO_HUD_PROFILE__COUNT] = { "crosshair", "vitals", "team", "gun" };
	return (section >= 0 && section < NEO_HUD_PROFILE__COUNT) ? s_hudNames[section] : "";
}

const char *NeoHudProfileCounterName(NeoHudCounter counter)
{
	static const char *const s_hudNames[NEO_HUD_COUNT__COUNT] = { "texts", "meshes", "quads", "rays" };
	return (counter >= 0 && counter < NEO_HUD_COUNT__COUNT) ? s_hudNames[counter] : "";
}
