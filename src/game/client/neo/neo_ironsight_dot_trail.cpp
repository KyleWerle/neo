#include "cbase.h"
#include "neo_ironsight_dot_trail.h"
#include "neo_ironsights.h"
#include "neo_ironsight_optic.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "iviewrender.h"
#include "ivrenderview.h"
#include "view_shared.h"
#include "mathlib/vmatrix.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "materialsystem/MaterialSystemUtil.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_ironsight_dot_trail("cl_neo_ironsight_dot_trail", "0.2", FCVAR_ARCHIVE,
	"Collimated dots leave a fading red streak where they pointed, in the dark, when the aim moves quickly."
	" Its brightness; 0 = off.", true, 0, true, 2);
ConVar cl_neo_ironsight_dot_trail_time("cl_neo_ironsight_dot_trail_time", "0.25", FCVAR_ARCHIVE,
	"Seconds the dot's trail takes to fade.", true, 0.05f, true, 1);
ConVar cl_neo_ironsight_dot_trail_width("cl_neo_ironsight_dot_trail_width", "1.5", FCVAR_ARCHIVE,
	"The trail's width, in dot radii (its glow reaches this far; its bright core a third of it).", true, 0.2f, true, 6);
ConVar cl_neo_ironsight_dot_trail_color("cl_neo_ironsight_dot_trail_color", "255 40 24", FCVAR_ARCHIVE,
	"The trail's colour, \"R G B\".");
ConVar cl_neo_ironsight_dot_trail_speed("cl_neo_ironsight_dot_trail_speed", "40", FCVAR_ARCHIVE,
	"Degrees a second the aim must turn before the trail starts showing (full at twice this): no smear while"
	" holding or tracking slowly.", true, 0, true, 720);
ConVar cl_neo_ironsight_dot_trail_dark("cl_neo_ironsight_dot_trail_dark", "0.08", FCVAR_ARCHIVE,
	"Light level where the player stands at or below which the trail shows fully.", true, 0, true, 1);
ConVar cl_neo_ironsight_dot_trail_bright("cl_neo_ironsight_dot_trail_bright", "0.3", FCVAR_ARCHIVE,
	"Light level at or above which the trail is gone.", true, 0, true, 1);
ConVar cl_neo_ironsight_dot_trail_shot_hold("cl_neo_ironsight_dot_trail_shot_hold", "0.3", FCVAR_ARCHIVE,
	"Seconds after the last shot before the trail comes back (each shot cuts it).", true, 0, true, 2);
ConVar cl_neo_ironsight_dot_trail_head_fade("cl_neo_ironsight_dot_trail_head_fade", "4", FCVAR_ARCHIVE,
	"How far behind the dot the trail fades in, in dot radii: no solid line running into the dot itself.",
	true, 0, true, 20);
ConVar cl_neo_ironsight_dot_trail_debug("cl_neo_ironsight_dot_trail_debug", "0", FCVAR_NONE,
	"Debug: print the light level where the player stands (what _dark and _bright compare with), once a second.");

static constexpr int TRAIL_SAMPLES = 128;
static constexpr int FRAME_GAP = 3;			// samples this many frames apart or more don't join (the dot was gone)
static constexpr float SHOT_FADE_IN = 0.15f;	// after the hold, the trail fades back in over this
static constexpr float MOVING_EASE = 0.05f;	// seconds the "moving quickly" amount eases over
static constexpr float FAR = 4096.0f;			// "infinity", as far as the projection can tell

struct TrailSample
{
	Vector direction;
	float angularRadius;
	float strength;	// the dot's visibility, times the shot's cut
	float moving;		// 0 to 1: how quickly the aim was turning
	float time;
	int frame;
};

static struct
{
	TrailSample samples[TRAIL_SAMPLES];
	int newest = -1;
	int count = 0;
	float lastShot = -100.0f;
	const CNEOWeaponInfo *pWeapon = nullptr;
	int lastClip = -1;
	float lastDebug = 0.0f;
} s_trail;

// Notes a shot when the viewed weapon's clip drops.
static void WatchShots(float now)
{
	C_NEO_Player *pPlayer = NeoIronsightOpticViewPlayer();
	auto *pWeapon = pPlayer ? dynamic_cast<CNEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon()) : nullptr;
	const CNEOWeaponInfo *pData = pWeapon ? &pWeapon->GetNEOWpnData() : nullptr;
	const int clip = pWeapon ? pWeapon->Clip1() : -1;
	if (pData && pData == s_trail.pWeapon && clip >= 0 && clip < s_trail.lastClip)
	{
		s_trail.lastShot = now;
		s_trail.count = 0;	// the flash puts the trail out
	}
	s_trail.pWeapon = pData;
	s_trail.lastClip = clip;
}

void NeoIronsightRecordDotTrail(const Vector &direction, float angularRadius, float strength)
{
	if (cl_neo_ironsight_dot_trail.GetFloat() <= 0.0f)
	{
		return;
	}
	const float now = gpGlobals->realtime;
	WatchShots(now);
	const float sinceShot = now - s_trail.lastShot - cl_neo_ironsight_dot_trail_shot_hold.GetFloat();
	const float cut = NeoSmoothStep(sinceShot / SHOT_FADE_IN);

	// The collimator can draw more than once a frame (the glass's passes): one sample a frame, the last kept.
	const TrailSample *pPrevious = (s_trail.count > 0) ? &s_trail.samples[s_trail.newest] : nullptr;
	if (pPrevious && pPrevious->frame == gpGlobals->framecount)
	{
		pPrevious = (s_trail.count > 1) ? &s_trail.samples[(s_trail.newest + TRAIL_SAMPLES - 1) % TRAIL_SAMPLES] : nullptr;
		s_trail.newest = (s_trail.newest + TRAIL_SAMPLES - 1) % TRAIL_SAMPLES;
		--s_trail.count;
	}
	Vector dir = direction;
	VectorNormalize(dir);
	float moving = 0.0f;
	if (pPrevious && gpGlobals->framecount - pPrevious->frame < FRAME_GAP && now > pPrevious->time)
	{
		const float turned = RAD2DEG(acosf(clamp(DotProduct(dir, pPrevious->direction), -1.0f, 1.0f)));
		const float speed = turned / (now - pPrevious->time);
		const float threshold = Max(cl_neo_ironsight_dot_trail_speed.GetFloat(), 0.001f);
		const float target = NeoSmoothStep((speed - threshold) / threshold);
		const float ease = Min(1.0f, (now - pPrevious->time) / MOVING_EASE);
		moving = pPrevious->moving + (target - pPrevious->moving) * ease;
	}
	s_trail.newest = (s_trail.newest + 1) % TRAIL_SAMPLES;
	s_trail.count = Min(s_trail.count + 1, TRAIL_SAMPLES);
	TrailSample &sample = s_trail.samples[s_trail.newest];
	sample.direction = dir;
	sample.angularRadius = angularRadius;
	sample.strength = strength * cut;
	sample.moving = moving;
	sample.time = now;
	sample.frame = gpGlobals->framecount;
}

// How dark it is where the player stands: 1 fully dark, 0 too bright for the trail.
static float Darkness(const Vector &eye)
{
	const Vector light = engine->GetLightForPoint(eye, true);
	const float level = 0.3f * light.x + 0.59f * light.y + 0.11f * light.z;
	if (cl_neo_ironsight_dot_trail_debug.GetBool() && gpGlobals->realtime - s_trail.lastDebug >= 1.0f)
	{
		s_trail.lastDebug = gpGlobals->realtime;
		Msg("[dot trail] light level %.3f\n", level);
	}
	const float dark = cl_neo_ironsight_dot_trail_dark.GetFloat();
	const float bright = Max(cl_neo_ironsight_dot_trail_bright.GetFloat(), dark + 0.001f);
	return 1.0f - NeoSmoothStep((level - dark) / (bright - dark));
}

void NeoIronsightPaintDotTrail()
{
	const float brightness = cl_neo_ironsight_dot_trail.GetFloat();
	const CViewSetup *pView = view ? view->GetViewSetup() : nullptr;
	if (brightness <= 0.0f || !pView || s_trail.count < 2)
	{
		return;
	}
	const float darkness = Darkness(pView->origin);
	if (darkness <= 0.0f)
	{
		return;
	}

	// Projected as the dot is drawn: the main view with the viewmodel's field of view.
	CViewSetup viewModelView = *pView;
	viewModelView.fov = pView->fovViewmodel;
	VMatrix worldToView, viewToProjection, worldToProjection, worldToPixels;
	render->GetMatricesForView(viewModelView, &worldToView, &viewToProjection, &worldToProjection, &worldToPixels);
	Vector forward, up;
	AngleVectors(pView->angles, &forward, nullptr, &up);
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	const auto project = [&](const Vector &direction) {
		Vector ndc;
		Vector3DMultiplyPositionProjective(worldToProjection, pView->origin + direction * FAR, ndc);
		return Vector2D((ndc.x * 0.5f + 0.5f) * wide, (0.5f - ndc.y * 0.5f) * tall);
	};

	// The live samples, oldest first, on screen, with their width and brightness.
	struct Point
	{
		Vector2D at;
		float width;
		float alpha;
		int frame;
	};
	static Point s_points[TRAIL_SAMPLES];
	int points = 0;
	const float now = gpGlobals->realtime;
	const float life = cl_neo_ironsight_dot_trail_time.GetFloat();
	const float widthScale = cl_neo_ironsight_dot_trail_width.GetFloat();
	for (int i = s_trail.count - 1; i >= 0; --i)
	{
		const TrailSample &sample = s_trail.samples[(s_trail.newest + TRAIL_SAMPLES - i) % TRAIL_SAMPLES];
		const float fresh = 1.0f - (now - sample.time) / life;
		if (fresh <= 0.0f || DotProduct(sample.direction, forward) <= 0.1f)
		{
			continue;
		}
		Point &point = s_points[points++];
		point.at = project(sample.direction);
		const Vector2D edge = project(sample.direction + up * sample.angularRadius);
		// Thinning toward its tail, as it fades.
		point.width = (edge - point.at).Length() * widthScale * (0.3f + 0.7f * fresh);
		point.alpha = clamp(brightness * darkness * sample.strength * sample.moving * fresh * fresh, 0.0f, 1.0f);
		point.frame = sample.frame;
	}
	if (points < 2)
	{
		return;
	}
	// Faded in from nothing at the dot (where the newest sample sits) to full a few dot radii back along it.
	const Vector2D &head = s_points[points - 1].at;
	const float headFade = cl_neo_ironsight_dot_trail_head_fade.GetFloat() * s_points[points - 1].width
		/ Max(cl_neo_ironsight_dot_trail_width.GetFloat(), 0.001f);
	if (headFade > 0.0f)
	{
		for (int i = 0; i < points; ++i)
		{
			s_points[i].alpha *= NeoSmoothStep((s_points[i].at - head).Length() / headFade);
		}
	}

	// A soft ribbon: across it, the glow's edges, the bright core's edges and its middle.
	static const float s_across[] = { -1.0f, -0.33f, 0.0f, 0.33f, 1.0f };
	static const float s_acrossAlpha[] = { 0.0f, 0.7f, 1.0f, 0.7f, 0.0f };
	constexpr int ACROSS = ARRAYSIZE(s_across);
	static CMaterialReference s_material;
	if (!s_material.IsValid())
	{
		KeyValues *pVMT = new KeyValues("UnlitGeneric");
		pVMT->SetString("$basetexture", "vgui/white");
		pVMT->SetInt("$additive", 1);
		pVMT->SetInt("$vertexcolor", 1);
		pVMT->SetInt("$vertexalpha", 1);
		pVMT->SetInt("$ignorez", 1);
		pVMT->SetInt("$nocull", 1);
		s_material.Init("__neo_ironsight_dot_trail", pVMT);
	}
	int red = 255, green = 40, blue = 24;
	sscanf(cl_neo_ironsight_dot_trail_color.GetString(), "%d %d %d", &red, &green, &blue);

	CMatRenderContextPtr pRenderContext(materials);
	IMesh *pMesh = pRenderContext->GetDynamicMesh(true, nullptr, nullptr, s_material);
	CMeshBuilder meshBuilder;
	meshBuilder.Begin(pMesh, MATERIAL_TRIANGLES, points * ACROSS, (points - 1) * (ACROSS - 1) * 6);
	Vector2D lastNormal(0.0f, 1.0f);
	for (int i = 0; i < points; ++i)
	{
		// Across the ribbon: square to its path here (from its neighbours).
		const Vector2D along = s_points[Min(i + 1, points - 1)].at - s_points[Max(i - 1, 0)].at;
		const float length = along.Length();
		const Vector2D normal = (length > 0.01f) ? Vector2D(-along.y / length, along.x / length) : lastNormal;
		lastNormal = normal;
		for (int k = 0; k < ACROSS; ++k)
		{
			const Vector2D at = s_points[i].at + normal * (s_points[i].width * s_across[k]);
			meshBuilder.Color4ub(red, green, blue, static_cast<unsigned char>(255.0f * s_points[i].alpha * s_acrossAlpha[k]));
			meshBuilder.TexCoord2f(0, 0.5f, 0.5f);
			meshBuilder.Position3f(at.x, at.y, 0.0f);
			meshBuilder.AdvanceVertex();
		}
	}
	for (int i = 1; i < points; ++i)
	{
		// Samples from either side of a gap (the dot was gone) don't join.
		const bool bJoined = s_points[i].frame - s_points[i - 1].frame < FRAME_GAP;
		for (int k = 0; k < ACROSS - 1; ++k)
		{
			const int a = (i - 1) * ACROSS + k, b = a + 1, c = i * ACROSS + k, d = c + 1;
			if (!bJoined)
			{
				// Degenerate: the index count is fixed when the mesh begins.
				for (int n = 0; n < 6; ++n)
				{
					meshBuilder.FastIndex(a);
				}
				continue;
			}
			meshBuilder.FastIndex(a); meshBuilder.FastIndex(c); meshBuilder.FastIndex(d);
			meshBuilder.FastIndex(a); meshBuilder.FastIndex(d); meshBuilder.FastIndex(b);
		}
	}
	meshBuilder.End();
	pMesh->Draw();
}
