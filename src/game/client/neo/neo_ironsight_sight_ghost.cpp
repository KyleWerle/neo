#include "cbase.h"
#include "neo_ironsight_sight_ghost.h"
#include "neo_ironsight_profile.h"
#include "neo_ironsights.h"
#include "neo_ironsight_optic.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "iviewrender.h"
#include "ivrenderview.h"
#include "view_shared.h"
#include "mathlib/vmatrix.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_ironsight_sight_ghost("cl_neo_ironsight_sight_ghost", "1", FCVAR_ARCHIVE,
	"Sight ghost (HUD linework over the sights while aiming): 0 = the glowing sight dots instead, 1 = while cloaked,"
	" 2 = whenever aiming.", true, 0, true, 2);
ConVar cl_neo_ironsight_sight_ghost_alpha("cl_neo_ironsight_sight_ghost_alpha", "1", FCVAR_ARCHIVE,
	"Opacity of the sight ghost's linework.", true, 0, true, 1);

// Animation timings, in seconds: quick and crisp.
static constexpr float GHOST_TRACE_TIME = 0.11f;	// coming online, the strokes trace out from their middles
static constexpr float GHOST_TYPE_DELAY = 0.04f;	// then the ammo count types in
static constexpr float GHOST_TYPE_TIME = 0.16f;
static constexpr float GHOST_LOCK_SETTLE = 0.08f;	// sights held aligned this long lock in
static constexpr float GHOST_LOCK_TIME = 0.06f;
static constexpr float GHOST_SHOT_TIME = 0.09f;	// a shot scrambles the linework this long

static struct
{
	int frame = -1;
	float aim = 0.0f;	// 0 to 1 over the second half of aiming
	const CNEOWeaponInfo *pData = nullptr;
	Vector points[3];	// front post, rear left, rear right
} s_ghost;

bool NeoIronsightSightGhostEnabled()
{
	return cl_neo_ironsight_sight_ghost.GetInt() > 0;
}

bool NeoIronsightSightGhostShown(bool bCloaked)
{
	const int mode = cl_neo_ironsight_sight_ghost.GetInt();
	return mode == 2 || (mode == 1 && bCloaked);
}

void NeoIronsightRecordSightGhost(const CNEOWeaponInfo &data, const Vector points[3], float ironsightBlend)
{
	const float aim = clamp((ironsightBlend - 0.5f) / 0.5f, 0.0f, 1.0f);
	if (aim <= 0.0f)
	{
		return;
	}
	s_ghost.frame = gpGlobals->framecount;
	s_ghost.aim = aim;
	s_ghost.pData = &data;
	for (int i = 0; i < 3; ++i)
	{
		s_ghost.points[i] = points[i];
	}
}

//-----------------------------------------------------------------------------
// Drawing: each glyph is drawn in a frame that follows the rear sight's cant on screen: x along the notch
// (rear left to rear right), y down across it.
//-----------------------------------------------------------------------------
namespace
{
struct GhostFrame
{
	Vector2D axis, down;
	Vector2D At(const Vector2D &origin, float x, float y) const { return origin + axis * x + down * y; }
};

// Stroke weights, in pixels at 1080p: heavy for what the eye aligns (notch edges, front tip), medium for
// structure, light for accents. Each stroke is a thin quad, so any weight turns with the gun's cant.
enum GhostWeight
{
	HEAVY,
	MEDIUM,
	LIGHT,
};
float s_flScale = 1.0f;
float s_flTrace = 1.0f;	// how far each stroke has traced out from its middle (booting in or out)
int s_iWhiteTexture = -1;

void Stroke(const Vector2D &a, const Vector2D &b, GhostWeight weight)
{
	static const float s_widths[] = { 2.4f, 1.4f, 0.8f };
	const float width = Max(1.0f, s_widths[weight] * s_flScale);
	const Vector2D middle = (a + b) * 0.5f;
	const Vector2D from = middle + (a - middle) * s_flTrace, to = middle + (b - middle) * s_flTrace;
	Vector2D along = to - from;
	const float length = along.Length();
	if (length < 0.01f)
	{
		return;
	}
	along /= length;
	// Square caps: each end runs on by half the width, so strokes meeting at a corner join solid.
	const Vector2D across(-along.y * width * 0.5f, along.x * width * 0.5f);
	const Vector2D start = from - along * (width * 0.5f), end = to + along * (width * 0.5f);
	vgui::Vertex_t quad[4];
	quad[0].Init(start - across, Vector2D(0, 0));
	quad[1].Init(end - across, Vector2D(1, 0));
	quad[2].Init(end + across, Vector2D(1, 1));
	quad[3].Init(start + across, Vector2D(0, 1));
	vgui::surface()->DrawTexturedPolygon(4, quad);
}

void DrawRear(int style, const GhostFrame &f, const Vector2D &left, const Vector2D &right, float s)
{
	switch (style)
	{
	case NEO_GHOST_REAR_TICKS:
		// Two bare verticals marking the notch edges.
		Stroke(f.At(left, 0, -4 * s), f.At(left, 0, 4 * s), HEAVY);
		Stroke(f.At(right, 0, -4 * s), f.At(right, 0, 4 * s), HEAVY);
		break;
	case NEO_GHOST_REAR_GATE:
		// The notch as an open U: heavy walls, a light floor.
		Stroke(f.At(left, 0, 0), f.At(left, 0, 6 * s), HEAVY);
		Stroke(f.At(left, 0, 6 * s), f.At(right, 0, 6 * s), LIGHT);
		Stroke(f.At(right, 0, 6 * s), f.At(right, 0, 0), HEAVY);
		break;
	case NEO_GHOST_REAR_CORNERS:
	{
		// Four corner marks framing the notch: medium horizontals, light verticals.
		const float pad = 3 * s, h = 4 * s, c = 3 * s;
		const Vector2D tl = f.At(left, -pad, -h), tr = f.At(right, pad, -h), bl = f.At(left, -pad, h), br = f.At(right, pad, h);
		Stroke(tl, tl + f.axis * c, MEDIUM);
		Stroke(tl, tl + f.down * c, LIGHT);
		Stroke(tr, tr - f.axis * c, MEDIUM);
		Stroke(tr, tr + f.down * c, LIGHT);
		Stroke(bl, bl + f.axis * c, MEDIUM);
		Stroke(bl, bl - f.down * c, LIGHT);
		Stroke(br, br - f.axis * c, MEDIUM);
		Stroke(br, br - f.down * c, LIGHT);
		break;
	}
	case NEO_GHOST_REAR_BRACKETS:
	default:
		// Brackets facing each other across the notch: heavy edges, light ticks.
		for (int side = 0; side < 2; ++side)
		{
			const Vector2D &o = side ? right : left;
			const float in = side ? -3 * s : 3 * s, h = 5 * s;
			Stroke(f.At(o, 0, -h), f.At(o, 0, h), HEAVY);
			Stroke(f.At(o, 0, -h), f.At(o, in, -h), LIGHT);
			Stroke(f.At(o, 0, h), f.At(o, in, h), LIGHT);
		}
		break;
	}
}

void DrawFront(int style, const GhostFrame &f, const Vector2D &tip, float s)
{
	switch (style)
	{
	case NEO_GHOST_FRONT_POST:
		// The post itself, a heavy cap on a medium stem.
		Stroke(tip, f.At(tip, 0, 6 * s), MEDIUM);
		Stroke(f.At(tip, -2 * s, 0), f.At(tip, 2 * s, 0), HEAVY);
		break;
	case NEO_GHOST_FRONT_DIAMOND:
	{
		// Heavier on top, where the tip is read.
		const float r = 3 * s;
		const Vector2D top = f.At(tip, 0, -r), rgt = f.At(tip, r, 0), bot = f.At(tip, 0, r), lft = f.At(tip, -r, 0);
		Stroke(top, rgt, HEAVY);
		Stroke(rgt, bot, LIGHT);
		Stroke(bot, lft, LIGHT);
		Stroke(lft, top, HEAVY);
		break;
	}
	case NEO_GHOST_FRONT_SPLIT:
		// A heavy bar broken at the tip, with a light drop below it.
		Stroke(f.At(tip, -5 * s, 0), f.At(tip, -1.5f * s, 0), HEAVY);
		Stroke(f.At(tip, 1.5f * s, 0), f.At(tip, 5 * s, 0), HEAVY);
		Stroke(f.At(tip, 0, 1.5f * s), f.At(tip, 0, 3.5f * s), LIGHT);
		break;
	case NEO_GHOST_FRONT_CHEVRON:
	default:
		// An open chevron, its point on the post tip.
		Stroke(f.At(tip, -4 * s, 4 * s), tip, HEAVY);
		Stroke(tip, f.At(tip, 4 * s, 4 * s), HEAVY);
		break;
	}
}
}

static struct
{
	int lastFrame = -1;
	float bootStart = 0.0f;
	float alignedSince = -1.0f;
	float lock = 0.0f;
	float shotTime = -1.0f;
	const CNEOWeaponInfo *pShotWeapon = nullptr;
	int lastClip = -1;	// the viewed weapon's rounds left, drawn beside the rear sight
	int maxClip = -1;
} s_anim;

// Notes a shot when the viewed weapon's clip drops, and keeps its count for the readout.
static void WatchShots()
{
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	auto *pWeapon = pPlayer ? dynamic_cast<CNEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon()) : nullptr;
	const CNEOWeaponInfo *pWeaponData = pWeapon ? &pWeapon->GetNEOWpnData() : nullptr;
	const int clip = pWeapon ? pWeapon->Clip1() : -1;
	if (pWeaponData && pWeaponData == s_anim.pShotWeapon && clip >= 0 && clip < s_anim.lastClip)
	{
		s_anim.shotTime = gpGlobals->realtime;
	}
	s_anim.pShotWeapon = pWeaponData;
	s_anim.lastClip = clip;
	s_anim.maxClip = pWeapon ? pWeapon->GetMaxClip1() : -1;
}

void NeoIronsightPaintSightGhost(const Color &color)
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_SIGHTS, "NeoIronsightPaintSightGhost");
	const CNEOWeaponInfo *pData = s_ghost.pData;
	const CViewSetup *pView = view ? view->GetViewSetup() : nullptr;
	if (s_ghost.frame != gpGlobals->framecount || !pData || !pView)
	{
		return;
	}

	// Where the viewmodel is drawn: the main view with the viewmodel's field of view.
	CViewSetup viewModelView = *pView;
	viewModelView.fov = pView->fovViewmodel;
	VMatrix worldToView, viewToProjection, worldToProjection, worldToPixels;
	render->GetMatricesForView(viewModelView, &worldToView, &viewToProjection, &worldToProjection, &worldToPixels);
	Vector forward;
	AngleVectors(pView->angles, &forward);
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	Vector2D screen[3];
	for (int i = 0; i < 3; ++i)
	{
		if (DotProduct(s_ghost.points[i] - pView->origin, forward) <= 0.1f)
		{
			return;
		}
		Vector ndc;
		Vector3DMultiplyPositionProjective(worldToProjection, s_ghost.points[i], ndc);
		screen[i].Init((ndc.x * 0.5f + 0.5f) * wide, (0.5f - ndc.y * 0.5f) * tall);
	}

	// The frame follows the notch: rear left to rear right, whatever the gun's cant.
	Vector2D &tip = screen[0], &left = screen[1], &right = screen[2];
	GhostFrame frame;
	frame.axis = right - left;
	const float gap = frame.axis.Length();
	frame.axis = (gap > 0.5f) ? frame.axis / gap : Vector2D(1.0f, 0.0f);
	frame.down.Init(-frame.axis.y, frame.axis.x);
	const float s = (tall / 1080.0f) * pData->m_flIronGhostScale;

	// Coming online: whenever the ghost wasn't drawn the frame before, it boots again.
	const float now = gpGlobals->realtime;
	if (s_anim.lastFrame != gpGlobals->framecount - 1)
	{
		s_anim.bootStart = now;
		s_anim.alignedSince = -1.0f;
		s_anim.lock = 0.0f;
	}
	s_anim.lastFrame = gpGlobals->framecount;
	const float boot = now - s_anim.bootStart;
	s_flTrace = Min(s_ghost.aim, clamp(boot / GHOST_TRACE_TIME, 0.0f, 1.0f));

	// A shot scrambles the linework for a moment: the whole ghost jitters and flickers.
	WatchShots();
	const float sinceShot = (s_anim.shotTime >= 0.0f) ? now - s_anim.shotTime : GHOST_SHOT_TIME;
	const bool bScrambled = sinceShot < GHOST_SHOT_TIME;

	// Alignment lock: the front tip held in the notch (only the gun's sway moves it out) locks in.
	const Vector2D offset = tip - (left + right) * 0.5f;
	const bool bAligned = s_ghost.aim >= 1.0f && !bScrambled
		&& fabsf(offset.Dot(frame.axis)) < Max(2.0f * s, gap * 0.3f)
		&& fabsf(offset.Dot(frame.down)) < Max(4.0f * s, gap * 0.8f);
	if (!bAligned)
	{
		s_anim.alignedSince = -1.0f;
	}
	else if (s_anim.alignedSince < 0.0f)
	{
		s_anim.alignedSince = now;
	}
	const bool bLock = bAligned && now - s_anim.alignedSince >= GHOST_LOCK_SETTLE;
	const float lockStep = gpGlobals->frametime / GHOST_LOCK_TIME;
	s_anim.lock = bLock ? Min(1.0f, s_anim.lock + lockStep) : Max(0.0f, s_anim.lock - lockStep * 1.5f);

	float flicker = 1.0f;
	if (bScrambled)
	{
		const float strength = 1.0f - sinceShot / GHOST_SHOT_TIME;
		const Vector2D jitter(random->RandomFloat(-2.0f, 2.0f), random->RandomFloat(-2.0f, 2.0f));
		for (int i = 0; i < 3; ++i)
		{
			screen[i] += jitter * (strength * s) + Vector2D(random->RandomFloat(-0.5f, 0.5f) * s, 0.0f);
		}
		flicker = (gpGlobals->framecount & 1) ? 0.35f : 1.0f;
	}

	const int alpha = RoundFloatToInt(255.0f * cl_neo_ironsight_sight_ghost_alpha.GetFloat() * flicker);
	if (s_iWhiteTexture < 0)
	{
		s_iWhiteTexture = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile(s_iWhiteTexture, "vgui/white", true, false);
	}
	s_flScale = tall / 1080.0f;
	vgui::surface()->DrawSetTexture(s_iWhiteTexture);
	vgui::surface()->DrawSetColor(color.r(), color.g(), color.b(), alpha);
	// Locked: the rear sight steps in on the tip, and a tick appears under the notch.
	const Vector2D lockIn = frame.axis * (1.5f * s * s_anim.lock);
	DrawRear(pData->m_iIronGhostRear, frame, left + lockIn, right - lockIn, s);
	DrawFront(pData->m_iIronGhostFront, frame, tip, s);
	if (s_anim.lock > 0.0f)
	{
		const Vector2D under = (left + right) * 0.5f;
		Stroke(frame.At(under, -2.0f * s * s_anim.lock, 9.0f * s), frame.At(under, 2.0f * s * s_anim.lock, 9.0f * s), MEDIUM);
	}

	if (s_anim.lastClip >= 0)
	{
		static vgui::HFont s_font = vgui::INVALID_FONT;
		if (s_font == vgui::INVALID_FONT)
		{
			vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
			s_font = pScheme ? pScheme->GetFont("NHudOCRSmallerNoAdditive", true) : vgui::INVALID_FONT;
		}
		// Typed in a character at a time behind a cursor, and taken back as the gun comes off the sights.
		const float typed = Min(s_ghost.aim, clamp((boot - GHOST_TYPE_DELAY) / GHOST_TYPE_TIME, 0.0f, 1.0f));
		if (s_font != vgui::INVALID_FONT && typed > 0.0f)
		{
			// Padded to the magazine's width so the count doesn't shift as it drops.
			wchar_t text[16];
			const int digits = (s_anim.maxClip >= 100) ? 3 : 2;
			V_snwprintf(text, ARRAYSIZE(text) - 1, L"%0*d", digits, s_anim.lastClip);
			const int length = V_wcslen(text);
			int count = Min(length, static_cast<int>(ceilf(length * typed)));
			if (typed < 1.0f)
			{
				text[count++] = L'_';
			}
			const Vector2D at = frame.At(right, 10 * s, 0);
			vgui::surface()->DrawSetTextFont(s_font);
			vgui::surface()->DrawSetTextColor(color.r(), color.g(), color.b(), RoundFloatToInt(alpha * 0.7f));
			vgui::surface()->DrawSetTextPos(RoundFloatToInt(at.x), RoundFloatToInt(at.y) - vgui::surface()->GetFontTall(s_font) / 2);
			vgui::surface()->DrawPrintText(text, count);
		}
	}
}
