#pragma once

#include "tier0/vprof.h"

// Timing for the ironsight features. NEO_IRONSIGHT_PROFILE(section, "name") at the top of a function:
//   - always: a VPROF budget scope in the "Ironsights" group (+showbudget, vprof_generate_report);
//   - on the client: the time also adds up in its section, which the benchmark (neo_ironsight_bench)
//     reads per frame to split the CPU cost by feature.
enum NeoIronsightProfileSection
{
	NEO_PROFILE_LENS,		// the glass: its art, and leaving it out of the gun
	NEO_PROFILE_DAMPING,		// the viewmodel's motion: bob, idle and recoil damping on its bones, the animation
							// blend, the recoil spring and the spread pivot
	NEO_PROFILE_SIGHTS,		// sight points, dots, the dot's afterimage and the sight ghost
	NEO_PROFILE_HUD,		// the crosshair layer and the quick info band (HUD linework)
	NEO_PROFILE__COUNT,
};

#define NEO_IRONSIGHT_VPROF_GROUP "Ironsights"

#ifdef CLIENT_DLL
class CNeoIronsightProfileScope
{
public:
	explicit CNeoIronsightProfileScope(NeoIronsightProfileSection section);
	~CNeoIronsightProfileScope();
private:
	NeoIronsightProfileSection m_section;
	double m_start;
};

// The section's time since the last call, in milliseconds, and resets it.
double NeoIronsightProfileTakeMs(NeoIronsightProfileSection section);
// How many times the section ran since the last call, and resets it.
int NeoIronsightProfileTakeCalls(NeoIronsightProfileSection section);
const char *NeoIronsightProfileSectionName(NeoIronsightProfileSection section);

#define NEO_IRONSIGHT_PROFILE(section, name) \
	VPROF_BUDGET(name, NEO_IRONSIGHT_VPROF_GROUP); \
	CNeoIronsightProfileScope neoIronsightProfileScope(section)
#else
#define NEO_IRONSIGHT_PROFILE(section, name) VPROF_BUDGET(name, NEO_IRONSIGHT_VPROF_GROUP)
#endif
