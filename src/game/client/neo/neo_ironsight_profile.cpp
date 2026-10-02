#include "cbase.h"
#include "neo_ironsight_profile.h"
#include "tier0/platform.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static double s_accumulated[NEO_PROFILE__COUNT];	// seconds
static int s_calls[NEO_PROFILE__COUNT];
bool g_neoIronsightProfileOn = false;

CNeoIronsightProfileScope::CNeoIronsightProfileScope(NeoIronsightProfileSection section)
	: m_section(section), m_start(g_neoIronsightProfileOn ? Plat_FloatTime() : 0.0), m_on(g_neoIronsightProfileOn)
{
	if (m_on)
		++s_calls[section];
}

CNeoIronsightProfileScope::~CNeoIronsightProfileScope()
{
	if (m_on)
		s_accumulated[m_section] += Plat_FloatTime() - m_start;
}

double NeoIronsightProfileTakeMs(NeoIronsightProfileSection section)
{
	const double seconds = s_accumulated[section];
	s_accumulated[section] = 0.0;
	return 1000.0 * seconds;
}

int NeoIronsightProfileTakeCalls(NeoIronsightProfileSection section)
{
	const int calls = s_calls[section];
	s_calls[section] = 0;
	return calls;
}

const char *NeoIronsightProfileSectionName(NeoIronsightProfileSection section)
{
	static const char *const s_names[NEO_PROFILE__COUNT] = { "lens drawing", "damping", "sights", "hud", "shots" };
	return (section >= 0 && section < NEO_PROFILE__COUNT) ? s_names[section] : "";
}
