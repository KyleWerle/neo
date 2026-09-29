#include "cbase.h"
#include "neo_gunplay_tunnel.h"
#include "neo_ghost_stroke.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "iviewrender.h"
#include "ivrenderview.h"
#include "view_shared.h"
#include "mathlib/vmatrix.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_tunnel("cl_neo_gunplay_tunnel", "1", FCVAR_ARCHIVE,
	"With the crosshair layer, aimed: outlines sliding out from the muzzle along the aim, each the spread cone's"
	" size where it is. Its opacity; 0 = off.", true, 0, true, 1);
ConVar cl_neo_gunplay_tunnel_time("cl_neo_gunplay_tunnel_time", "0.9", FCVAR_ARCHIVE,
	"Seconds each outline takes to travel out.", true, 0.1f, true, 5);

static constexpr int OUTLINES = 4;				// travelling at once, evenly spaced
static constexpr float RANGE = 1200.0f;			// where they end, in units along the aim (about 30 m)
static constexpr float MUZZLE_SIZE = 0.6f;		// their half-size leaving the muzzle, in units
static constexpr float MIN_SPREAD = 0.004f;		// a tangent: a perfectly accurate gun still shows a small box
static constexpr float FADE_IN = 0.12f;			// of the trip
static constexpr float FADE_OUT = 0.55f;		// from here to the end

void NeoGunplayPaintTunnel(C_NEO_Player *pPlayer, float spreadTangent, const Color &color, float opacity)
{
	opacity *= cl_neo_gunplay_tunnel.GetFloat();
	const CViewSetup *pView = view ? view->GetViewSetup() : nullptr;
	C_BaseAnimating *pViewModel = pPlayer ? pPlayer->GetViewModel() : nullptr;
	if (opacity <= 0.0f || !pView || !pViewModel)
	{
		return;
	}
	Vector muzzle;
	QAngle muzzleAngles;
	if (!pViewModel->GetAttachment(1, muzzle, muzzleAngles))
	{
		return;
	}
	const Vector &eye = pView->origin;
	Vector forward, right, up;
	AngleVectors(pView->angles, &forward, &right, &up);
	const float muzzleDepth = DotProduct(muzzle - eye, forward);
	if (muzzleDepth < 1.0f)
	{
		return;
	}

	// The muzzle is drawn with the viewmodel's field of view: where it shows on screen, put back into the world's
	// view at its depth, so the tunnel leaves the gun where it is seen.
	CViewSetup viewModelView = *pView;
	viewModelView.fov = pView->fovViewmodel;
	VMatrix worldToView, viewToProjection, viewModelProjection, worldProjection, worldToPixels;
	render->GetMatricesForView(viewModelView, &worldToView, &viewToProjection, &viewModelProjection, &worldToPixels);
	render->GetMatricesForView(*pView, &worldToView, &viewToProjection, &worldProjection, &worldToPixels);
	Vector ndc;
	Vector3DMultiplyPositionProjective(viewModelProjection, muzzle, ndc);
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	const float tanX = tanf(DEG2RAD(pView->fov * 0.5f));
	const float tanY = tanX * tall / Max(wide, 1);
	const Vector start = eye + (forward + right * (ndc.x * tanX) + up * (ndc.y * tanY)) * muzzleDepth;
	const Vector end = eye + forward * RANGE;

	const auto project = [&](const Vector &point) {
		Vector at;
		Vector3DMultiplyPositionProjective(worldProjection, point, at);
		return Vector2D((at.x * 0.5f + 0.5f) * wide, (0.5f - at.y * 0.5f) * tall);
	};
	const float tangent = Max(spreadTangent, MIN_SPREAD);
	NeoGhostPen pen;
	pen.scale = tall / 1080.0f;
	const float phase = gpGlobals->realtime / cl_neo_gunplay_tunnel_time.GetFloat();
	for (int i = 0; i < OUTLINES; ++i)
	{
		const float trip = phase + static_cast<float>(i) / OUTLINES;
		const float t = trip - floorf(trip);
		// Lingering near the gun, then sweeping out: the path's middle flies by on screen anyway.
		const float along = t * t;
		const Vector centre = start + (end - start) * along;
		const float depth = DotProduct(centre - eye, forward);
		const float half = Lerp(along, MUZZLE_SIZE, tangent * depth);
		const float fade = NeoSmoothStep(t / FADE_IN) * (1.0f - NeoSmoothStep((t - FADE_OUT) / (1.0f - FADE_OUT)));
		const int alpha = RoundFloatToInt(255.0f * opacity * fade);
		if (alpha <= 0)
		{
			continue;
		}
		const Vector2D corners[4] = {
			project(centre - right * half + up * half), project(centre + right * half + up * half),
			project(centre + right * half - up * half), project(centre - right * half - up * half) };
		NeoGhostBegin(color, alpha);
		for (int c = 0; c < 4; ++c)
		{
			NeoGhostStroke(pen, corners[c], corners[(c + 1) % 4], NEO_GHOST_LIGHT);
		}
	}
}
