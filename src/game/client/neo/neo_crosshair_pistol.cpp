#include "cbase.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The pistol family: minimal. Two short side ticks at the spread's edge, and a chevron below that opens as the gun
// cycles and snaps shut when it's ready. The Kyla revolver shows its cylinder beside them: six chambers, loaded or
// spent, the cylinder turning a chamber each shot.

namespace NeoCrosshairPistol
{
// Sizes in pixels at 1080p.
static constexpr float GAP = 5.0f;				// beyond the spread's edge
static constexpr float TICK = 3.0f;			// each side tick's half-height
static constexpr float CHEVRON_GAP = 3.0f;		// the chevron's tip below the spread
static constexpr float CHEVRON_SHUT = 2.5f;	// its half-width when ready, and fully open
static constexpr float CHEVRON_OPEN = 6.0f;
static constexpr float CHEVRON_DROP = 2.5f;	// how far its arms drop
static constexpr float SNAP_TIME = 0.06f;		// shutting
static constexpr int CHAMBERS = 6;
static constexpr float CYLINDER_RADIUS = 5.0f;
static constexpr float CYLINDER_GAP = 7.0f;	// right of the right tick
static constexpr float CYLINDER_TURN = 0.09f;	// the turn's ease, seconds

static struct
{
	float open = 0.0f;			// the chevron, 0 shut to 1 open
	float turn = 0.0f;			// the cylinder's angle, in chambers (eased toward the shots fired)
	float turned = 0.0f;
} s_pistol;

} // namespace NeoCrosshairPistol

void NeoCrosshairPaintPistol(const NeoCrosshairFrame &frame)
{
	using namespace NeoCrosshairPistol;
	const float s = frame.s;
	if (frame.bBoot)
	{
		s_pistol.open = 0.0f;
		s_pistol.turn = s_pistol.turned = 0.0f;
	}
	if (frame.bShot)
	{
		s_pistol.turned += 1.0f;
	}
	s_pistol.turn += (s_pistol.turned - s_pistol.turn) * Min(1.0f, frame.dt / CYLINDER_TURN);
	// Open while cycling, snapping shut when ready.
	const float target = (frame.ready < 1.0f) ? 1.0f - frame.ready : 0.0f;
	s_pistol.open = (target > s_pistol.open) ? target : Approach(target, s_pistol.open, frame.dt / SNAP_TIME);

	const Vector2D near = frame.centre + frame.deviation + frame.jitter;
	const float edge = frame.spread + GAP * s;
	NeoGhostBegin(frame.color, frame.Alpha(1.0f));
	for (int side = -1; side <= 1; side += 2)
	{
		const Vector2D at = near + Vector2D(side * edge, 0.0f);
		NeoGhostStroke(frame.pen, at - Vector2D(0.0f, TICK * s), at + Vector2D(0.0f, TICK * s), NEO_GHOST_MEDIUM);
	}
	// The chevron below, its tip up.
	const Vector2D tip = near + Vector2D(0.0f, edge + CHEVRON_GAP * s);
	const float half = Lerp(NeoSmoothStep(s_pistol.open), CHEVRON_SHUT, CHEVRON_OPEN) * s;
	const NeoGhostWeight weight = (s_pistol.open <= 0.0f) ? NEO_GHOST_HEAVY : NEO_GHOST_MEDIUM;
	NeoGhostStroke(frame.pen, tip + Vector2D(-half, CHEVRON_DROP * s), tip, weight);
	NeoGhostStroke(frame.pen, tip, tip + Vector2D(half, CHEVRON_DROP * s), weight);

	// The Kyla's cylinder: the loaded chambers bright, the spent faint; the chamber at the top fires next.
	if ((frame.pWeapon->GetNeoWepBits() & NEO_WEP_KYLA) && frame.clip >= 0)
	{
		const Vector2D centre = near + Vector2D(edge + (CYLINDER_GAP + CYLINDER_RADIUS) * s, 0.0f);
		const float step = 2.0f * M_PI_F / CHAMBERS;
		const float offset = (s_pistol.turn - s_pistol.turned) * step;	// easing into place after a shot
		for (int i = 0; i < CHAMBERS; ++i)
		{
			// Chamber 0 is up next; the ones after it follow clockwise, and the spent ones are the last.
			const float angle = -0.5f * M_PI_F + i * step + offset;
			const bool bLoaded = i < frame.clip;
			NeoGhostBegin(frame.color, frame.Alpha((bLoaded ? 1.0f : 0.3f) * Max(frame.aim, 0.5f)));
			NeoCrosshairDot(frame, centre + Vector2D(cosf(angle), sinf(angle)) * (CYLINDER_RADIUS * s),
				bLoaded ? NEO_GHOST_HEAVY : NEO_GHOST_LIGHT);
		}
	}
}
