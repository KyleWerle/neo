#include "cbase.h"
#include "neo_gunplay_aim.h"
#include "neo_crosshair_family.h"
#include "neo_ironsights.h"
#include "weapon_neobasecombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_crosshair_aim("cl_neo_gunplay_crosshair_aim", "1", FCVAR_ARCHIVE,
	"The aim crosshair: small and steady, leaning a little toward each bullet, never leaving the inner part of the"
	" spread. Drawn with any crosshair style (a Custom one stays as it is, dead centre).", true, 0, true, 1);
ConVar cl_neo_gunplay_crosshair_aim_share("cl_neo_gunplay_crosshair_aim_share", "0.3", FCVAR_ARCHIVE,
	"How much of the gun's turn (knock and pivot) the aim crosshair follows.", true, 0, true, 1);
ConVar cl_neo_gunplay_crosshair_aim_time("cl_neo_gunplay_crosshair_aim_time", "0.12", FCVAR_ARCHIVE,
	"The aim crosshair's damping: its spring's time constant, seconds.", true, 0.01f, true, 1);
ConVar cl_neo_gunplay_crosshair_aim_cap("cl_neo_gunplay_crosshair_aim_cap", "0.25", FCVAR_ARCHIVE,
	"The furthest the aim crosshair goes from the aim point, as a share of the spread's radius.", true, 0, true, 0.5f);
ConVar cl_neo_gunplay_crosshair_link_debug("cl_neo_gunplay_crosshair_link_debug", "0", FCVAR_NONE,
	"Debug: print the link (separation, recovery, readiness) when the layers lock.");

namespace NeoGunplayAim
{
static constexpr float LINK_DIST = 2.5f;		// layers closer than this (pixels at 1080p) start to link
static constexpr float RECOVERED = 0.25f;		// the last share of the accuracy penalty's recovery links the layers
static constexpr float LINK_RISE = 0.12f;		// the bridges trace in this quickly
static constexpr float MIN_BREAK = 0.05f;		// a shot keeps them off at least this long
static constexpr float LOCKED = 0.98f;			// the link this high is a lock
static constexpr float LOCK_FLASH = 0.08f;		// the joints flash heavy this long when they lock

static struct
{
	Vector2D offset = Vector2D(0.0f, 0.0f);
	Vector2D velocity = Vector2D(0.0f, 0.0f);
	float link = 0.0f;
	bool bBroken = false;	// a shot broke the lock: no bridges until the gun is ready again
	bool bLocked = false;
	float lockTime = -1.0f;
} s_aim;
} // namespace NeoGunplayAim

void NeoGunplayAimUpdate(NeoCrosshairFrame &frame)
{
	using namespace NeoGunplayAim;
	const float now = gpGlobals->realtime;

	// A share of the gun's turn, on a critically damped spring.
	const Vector2D target = frame.deviation * cl_neo_gunplay_crosshair_aim_share.GetFloat();
	const float omega = 1.0f / cl_neo_gunplay_crosshair_aim_time.GetFloat();
	NeoCrosshairSpring(s_aim.offset, s_aim.velocity, target, omega, frame.dt);
	if (frame.bBoot || !s_aim.offset.IsValid() || !s_aim.velocity.IsValid())
	{
		s_aim.offset.Init(0.0f, 0.0f);
		s_aim.velocity.Init(0.0f, 0.0f);
		s_aim.bBroken = false;
	}

	// Enforced, not tuned: never past the cap's share of the spread, so the aim point is always inside it. A gun with
	// no spread pins it dead centre.
	const float cap = frame.spread * cl_neo_gunplay_crosshair_aim_cap.GetFloat();
	const float length = s_aim.offset.Length();
	if (length > cap)
	{
		s_aim.offset *= (length > 0.0f) ? cap / length : 0.0f;
	}
	frame.aimOffset = s_aim.offset;

	// The link: the layers close together, the spread back to its minimum, and the gun ready. All three, so a lock
	// always means the next shot is at its best.
	const float separation = (frame.deviation - s_aim.offset).Length() / (LINK_DIST * frame.s);
	const float closeness = 1.0f - NeoSmoothStep(separation);
	const float recovery = 1.0f - frame.pWeapon->GetAccuracyPenaltyFraction();
	const float recovered = NeoSmoothStep((recovery - (1.0f - RECOVERED)) / RECOVERED);
	const float link = closeness * recovered * frame.ready;
	// Eased as drawn: traced in, faded out. It only ever lags the truth, so it never shows a lock early. A shot cuts
	// it at once, in every family: bridges left up through a shot's knock swung and stretched at odd angles.
	// Latched: nothing traces back in until the gun is ready again (its inputs can still read settled for a few
	// frames while the knock builds, which brought back partial bridges mid-kick).
	// A shot shows first as the gun's readiness dropping (its predicted clock), the shot count (the clip) a frame or
	// so later: either cuts it. Waiting for the count let the first shot's bridges fade under the knock.
	if (frame.bShot || frame.ready < 1.0f)
	{
		s_aim.bBroken = true;
	}
	else if (s_aim.bBroken && frame.sinceShot > MIN_BREAK)
	{
		s_aim.bBroken = false;
	}
	if (s_aim.bBroken || frame.bBoot)
	{
		s_aim.link = s_aim.bBroken ? 0.0f : link;
	}
	else
	{
		// Traced in; never faded out, cut (Kyle: removed instantly, or the knock catches them at odd angles).
		s_aim.link = (link < s_aim.link) ? link : Approach(link, s_aim.link, frame.dt / LINK_RISE);
	}
	frame.link = s_aim.link;

	const bool bLocked = s_aim.link >= LOCKED;
	if (bLocked && !s_aim.bLocked && !frame.bBoot)
	{
		s_aim.lockTime = now;
		if (cl_neo_gunplay_crosshair_link_debug.GetBool())
		{
			Msg("[link] locked %.2f s after the shot: close %.2f, recovered %.2f, ready %.2f\n",
				frame.sinceShot, closeness, recovered, frame.ready);
		}
	}
	else if (bLocked && frame.bBoot)
	{
		s_aim.lockTime = now - 1.0f;	// already settled: no flash
	}
	s_aim.bLocked = bLocked;
	frame.sinceLock = bLocked ? now - s_aim.lockTime : -1.0f;
}

void NeoCrosshairBridge(const NeoCrosshairFrame &frame, const Vector2D &inner, const Vector2D &outer,
	NeoGhostWeight weight)
{
	using namespace NeoGunplayAim;
	if (frame.link <= 0.01f)
	{
		return;
	}
	const bool bFlash = frame.sinceLock >= 0.0f && frame.sinceLock < LOCK_FLASH;
	NeoGhostBegin(frame.color, frame.Alpha(bFlash ? 1.0f : 0.9f));
	// Eased: each half starts and lands softly.
	const Vector2D half = (outer - inner) * (0.5f * NeoSmoothStep(frame.link));
	const NeoGhostWeight drawn = bFlash ? NEO_GHOST_HEAVY : weight;
	NeoGhostStroke(frame.pen, inner, inner + half, drawn);
	NeoGhostStroke(frame.pen, outer - half, outer, drawn);
}

void NeoGunplayPaintAim(const NeoCrosshairFrame &frame)
{
	const bool bGlyph = cl_neo_gunplay_crosshair_aim.GetBool();
	const Vector2D at = frame.centre + frame.aimOffset;
	switch (NeoCrosshairFamilyOf(frame.pWeapon))
	{
	case NEO_CROSSHAIR_SMG:		NeoCrosshairAimSmg(frame, at, bGlyph); break;
	case NEO_CROSSHAIR_MG:		NeoCrosshairAimMg(frame, at, bGlyph); break;
	case NEO_CROSSHAIR_SHOTGUN:	NeoCrosshairAimShotgun(frame, at, bGlyph); break;
	case NEO_CROSSHAIR_PISTOL:	NeoCrosshairAimPistol(frame, at, bGlyph); break;
	case NEO_CROSSHAIR_SCOPED:	NeoCrosshairAimScoped(frame, at, bGlyph); break;
	case NEO_CROSSHAIR_RIFLE:
	default:					NeoCrosshairAimRifle(frame, at, bGlyph); break;
	}
}
