#include "cbase.h"
#include "neo_gunplay_marks.h"
#include "neo_crosshair_family.h"
#include "neo_gunplay_shots.h"
#include "neo_ironsights.h"
#include "neo_ironsight_profile.h"
#include "weapon_neobasecombatweapon.h"
#include "view.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_crosshair_marks("cl_neo_gunplay_crosshair_marks", "0.7", FCVAR_ARCHIVE,
	"Impact marks where your shots went (exact, from each shot's seed): their opacity; 0 = none.", true, 0, true, 1);

namespace NeoGunplayMarks
{
// Sizes in pixels at 1080p, times in seconds.
static constexpr int MAX_MARKS = 48;			// a shotgun shot is a mark a pellet
static constexpr float RIFLE_HALF = 1.5f;		// a small square tick
static constexpr float RIFLE_LIFE = 0.5f;
static constexpr float SMG_LIFE = 0.45f;		// a dot; long enough for a burst to show as a cloud
static constexpr float SMG_HOLD = 0.1f;			// full, then dimming
static constexpr float SMG_DOT = 0.3f;			// the dot's length before its square caps (medium: about 2 px square)
static constexpr float MG_HALF = 2.5f;			// a short dash, a tick a shot like the belt
static constexpr float MG_LIFE = 0.9f;			// the longest: the pattern builds
static constexpr float PISTOL_RADIUS = 2.0f;		// a small hexagon on the point (the Kyla's chambers), held until the next shot
static constexpr float PISTOL_HOLD = 2.5f;		// at most
static constexpr float SCOPED_HALF = 2.5f;		// a crisp X, held through the bolt
static constexpr float SCOPED_HOLD = 3.0f;		// at most
static constexpr float PELLET_LIFE = 0.28f;		// the pellets: a brief flash of points
static constexpr float SLUG_HALF = 2.2f;		// the slug: a small diamond, as the crosshair's slug morph
static constexpr float SLUG_LIFE = 0.6f;
static constexpr float FOLD = 0.4f;				// the last share of a timed mark's life folds it
static constexpr float CLOSE = 0.12f;			// a held mark closes this quickly once released

struct Mark
{
	Vector position;	// where it hit, in the world (traced at the shot)
	float time;
	float endTime;		// held marks: when released (-1 while held)
	NeoCrosshairFamily family;
	bool bSlug;			// the shotgun family: a slug, not a pellet
};
static Mark s_marks[MAX_MARKS];
static int s_iNext = 0;
static int s_iCount = 0;
static float s_viewChanged = -100.0f;

static bool IsHeld(NeoCrosshairFamily family)
{
	return family == NEO_CROSSHAIR_PISTOL || family == NEO_CROSSHAIR_SCOPED;
}

// 1 while whole, down to 0 as it folds away; below 0 once gone.
static float Remaining(const Mark &mark, float now)
{
	float life;
	switch (mark.family)
	{
	case NEO_CROSSHAIR_PISTOL:
	case NEO_CROSSHAIR_SCOPED:
	{
		const float hold = (mark.family == NEO_CROSSHAIR_PISTOL) ? PISTOL_HOLD : SCOPED_HOLD;
		const float end = (mark.endTime >= 0.0f) ? Min(mark.endTime, mark.time + hold) : mark.time + hold;
		return (now < end) ? 1.0f : 1.0f - (now - end) / CLOSE;
	}
	case NEO_CROSSHAIR_SHOTGUN:	life = mark.bSlug ? SLUG_LIFE : PELLET_LIFE; break;
	case NEO_CROSSHAIR_SMG:	life = SMG_LIFE; break;
	case NEO_CROSSHAIR_MG:	life = MG_LIFE; break;
	default:				life = RIFLE_LIFE; break;
	}
	const float t = (now - mark.time) / life;
	return (t < 1.0f - FOLD) ? 1.0f : (1.0f - t) / FOLD;
}

static void PaintMark(const NeoCrosshairFrame &frame, const Mark &mark, const Vector2D &at, float left,
	float opacity, float now)
{
	const float s = frame.s;
	const float fold = NeoSmoothStep(left);
	int alpha = RoundFloatToInt(frame.color.a() * opacity);
	switch (mark.family)
	{
	case NEO_CROSSHAIR_SMG:
	{
		// Dims out, after a moment full.
		const float dim = 1.0f - clamp((now - mark.time - SMG_HOLD) / (SMG_LIFE - SMG_HOLD), 0.0f, 1.0f);
		NeoGhostBegin(frame.color, RoundFloatToInt(alpha * NeoSmoothStep(dim)));
		const Vector2D half(SMG_DOT * s, 0.0f);
		NeoGhostStroke(frame.pen, at - half, at + half, NEO_GHOST_MEDIUM);
		return;
	}
	case NEO_CROSSHAIR_SHOTGUN:
	{
		NeoGhostBegin(frame.color, alpha);
		if (mark.bSlug)
		{
			// Shrinks to its point.
			const float half = Max(SLUG_HALF * s * fold, 0.3f * s);
			const Vector2D corners[4] = { at + Vector2D(0.0f, -half), at + Vector2D(half, 0.0f),
				at + Vector2D(0.0f, half), at + Vector2D(-half, 0.0f) };
			for (int c = 0; c < 4; ++c)
			{
				NeoGhostStroke(frame.pen, corners[c], corners[(c + 1) % 4], NEO_GHOST_LIGHT);
			}
			return;
		}
		// A point a pellet, gone as it fades.
		NeoGhostBegin(frame.color, RoundFloatToInt(alpha * fold));
		NeoCrosshairDot(frame, at, NEO_GHOST_LIGHT);
		return;
	}
	case NEO_CROSSHAIR_MG:
	{
		// Draws in from both ends.
		NeoGhostBegin(frame.color, alpha);
		const Vector2D half(MG_HALF * s * fold, 0.0f);
		NeoGhostStroke(frame.pen, at - half, at + half, NEO_GHOST_LIGHT);
		return;
	}
	case NEO_CROSSHAIR_PISTOL:
	{
		// Closes by shrinking to its point.
		NeoGhostBegin(frame.color, alpha);
		const float radius = Max(PISTOL_RADIUS * s * fold, 0.3f * s);
		for (int i = 0; i < 6; ++i)
		{
			const float a0 = DEG2RAD(60.0f * i + 30.0f), a1 = DEG2RAD(60.0f * (i + 1) + 30.0f);
			NeoGhostStroke(frame.pen, at + Vector2D(cosf(a0), sinf(a0)) * radius, at + Vector2D(cosf(a1), sinf(a1)) * radius,
				NEO_GHOST_LIGHT);
		}
		return;
	}
	case NEO_CROSSHAIR_SCOPED:
	{
		// The arms draw in to the point.
		NeoGhostBegin(frame.color, alpha);
		const float half = SCOPED_HALF * s * fold;
		NeoGhostStroke(frame.pen, at + Vector2D(-half, -half), at + Vector2D(half, half), NEO_GHOST_LIGHT);
		NeoGhostStroke(frame.pen, at + Vector2D(-half, half), at + Vector2D(half, -half), NEO_GHOST_LIGHT);
		return;
	}
	default:
	{
		// Shrinks to its point.
		NeoGhostBegin(frame.color, alpha);
		const float half = Max(RIFLE_HALF * s * fold, 0.3f * s);
		const Vector2D corners[4] = { at + Vector2D(-half, -half), at + Vector2D(half, -half),
			at + Vector2D(half, half), at + Vector2D(-half, half) };
		for (int c = 0; c < 4; ++c)
		{
			NeoGhostStroke(frame.pen, corners[c], corners[(c + 1) % 4], NEO_GHOST_LIGHT);
		}
		return;
	}
	}
}
// Where a bullet from the weapon's owner along direction hit: a point in the world, so the mark stays on it as the
// player moves (as a direction alone it slid across near walls with every step). The sky: far along the line.
static Vector HitPoint(C_NEOBaseCombatWeapon *pWeapon, const Vector &direction)
{
	C_BasePlayer *pOwner = pWeapon ? ToBasePlayer(pWeapon->GetOwner()) : nullptr;
	if (!pOwner)
	{
		return direction * MAX_TRACE_LENGTH;
	}
	const Vector start = pOwner->Weapon_ShootPosition();
	const Vector end = start + direction.Normalized() * MAX_TRACE_LENGTH;
	trace_t trace;
	UTIL_TraceLine(start, end, MASK_SHOT, pOwner, COLLISION_GROUP_NONE, &trace);
	return trace.endpos;
}

static void AddMark(const Vector &position, NeoCrosshairFamily family, bool bSlug)
{
	const float now = gpGlobals->realtime;
	// A held mark is released by the next shot.
	if (s_iCount > 0)
	{
		Mark &last = s_marks[(s_iNext + MAX_MARKS - 1) % MAX_MARKS];
		if (IsHeld(last.family) && last.endTime < 0.0f)
		{
			last.endTime = now;
		}
	}
	s_marks[s_iNext] = { position, now, -1.0f, family, bSlug };
	s_iNext = (s_iNext + 1) % MAX_MARKS;
	s_iCount = Min(s_iCount + 1, MAX_MARKS);
}
} // namespace NeoGunplayMarks

void NeoGunplayMarkShot(C_NEOBaseCombatWeapon *pWeapon, const Vector &direction)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_SHOTS, "NeoGunplayMarkShot");
	using namespace NeoGunplayMarks;
	const NeoCrosshairFamily family = NeoCrosshairFamilyOf(pWeapon);
	if (family == NEO_CROSSHAIR_SHOTGUN)
	{
		return;	// the shotguns report their pellets (NeoGunplayMarkPellets)
	}
	AddMark(HitPoint(pWeapon, direction), family, false);
}

void NeoGunplayMarkPellets(C_NEOBaseCombatWeapon *pWeapon, const Vector *pDirections, int count)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_SHOTS, "NeoGunplayMarkPellets");
	using namespace NeoGunplayMarks;
	for (int i = 0; i < count; ++i)
	{
		AddMark(HitPoint(pWeapon, pDirections[i]), NEO_CROSSHAIR_SHOTGUN, count == 1);
	}
}

void NeoGunplayMarkHit(C_NEOBaseCombatWeapon *pWeapon, const Vector &point)
{
	NeoGunplayMarks::AddMark(point, NeoCrosshairFamilyOf(pWeapon), false);
}

void NeoGunplayPaintMarks(const NeoCrosshairFrame &frame)
{
	using namespace NeoGunplayMarks;
	const float opacity = cl_neo_gunplay_crosshair_marks.GetFloat();
	const NeoGunplayShots &shots = NeoGunplayWatchShots();
	// A new view (a spawn, a spectate target): the marks were someone else's view of the world.
	if (shots.viewChanged != s_viewChanged)
	{
		s_viewChanged = shots.viewChanged;
		s_iCount = 0;
	}
	if (opacity <= 0.0f || s_iCount == 0)
	{
		return;
	}
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if (!pLocal)
	{
		return;
	}
	// Through the camera as drawn: the crosshair's centre is its forward.
	const Vector &origin = MainViewOrigin(), &forward = MainViewForward(), &right = MainViewRight(), &up = MainViewUp();
	const float now = gpGlobals->realtime;
	for (int i = 0; i < s_iCount; ++i)
	{
		Mark &mark = s_marks[(s_iNext + MAX_MARKS - s_iCount + i) % MAX_MARKS];
		// The scoped X is held through the bolt: released once the gun is ready again.
		if (mark.family == NEO_CROSSHAIR_SCOPED && mark.endTime < 0.0f && i == s_iCount - 1
			&& frame.ready >= 1.0f && now - mark.time > 0.05f)
		{
			mark.endTime = now;
		}
		const float left = Remaining(mark, now);
		if (left <= 0.0f)
		{
			continue;
		}
		const Vector toMark = mark.position - origin;
		const float ahead = toMark.Dot(forward);
		if (ahead < 1.0f)
		{
			continue;
		}
		const Vector2D at = frame.centre + Vector2D(toMark.Dot(right), -toMark.Dot(up))
			* (frame.pixelsPerTangent / ahead);
		PaintMark(frame, mark, at, clamp(left, 0.0f, 1.0f), opacity, now);
	}
}
