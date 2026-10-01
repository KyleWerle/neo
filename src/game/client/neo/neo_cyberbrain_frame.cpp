#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_hud_model_team.h"
#include "neo_enums.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The frame (HUD-REWORK.md, phase 3; it was the chassis and the layer marks): each module's etched edge, after the racer band's instrument detail: round each group and the ring, on the deep layer,
// registration crosses (plain small crosses), an etched ruler under it with fine graduations, and its channel codes.
// The deep layer trails its group on a softer spring and drifts a few pixels as you turn (neo_cyberbrain_attention.cpp),
// so the crosses read as sitting behind the readout: the depth. Steady: it doesn't fade with attention.

ConVar cl_neo_hud_team_mark("cl_neo_hud_team_mark", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's faction mark: your team's logo (NT's own Jinrai and NSF marks), small and faint in the body"
	" group's corner, as a watermark on its chassis. Not in the neutral grid (cl_neo_hud_grid 2).", true, 0, true, 1);


// The layer marks, folded into the frame: the perception layers made visible (Kyle, 2026-09-30: "there should also be graphical element shifts as perceptual
// layer changes"): marks round each group's extent that build up layer by layer. Ambient: none. Notable: short
// corner ticks. Urgent: longer brackets, a rail along the top and the layer's code. Critical: heavy brackets doubled
// inside, the code in the critical colour, pulsing its first moments. Rising a layer, the brackets snap in from
// further out and a scan runs down the group; falling one, the old layer's marks drift out and fade. The comfort forms
// (HUD-REWORK.md's table): Full as that; Calm snaps in from 8 px over 0.3 s, no scan or pulse, the old marks fading in
// place over 0.4 s; Still nothing travels, the marks ease in over 0.4 s and out over 0.6 s.

namespace NeoCyberbrain
{
constexpr float CROSS = 4.0f;			// a cross's half size, 1080p
constexpr float CROSS_ALPHA = 0.5f, RULE_ALPHA = 0.3f, ETCHED = 0.7f;
constexpr float MARK = 20.0f, MARK_ALPHA = 0.25f;	// the faction mark's size, 1080p, and strength

// The faction mark (Kyle, 2026-10-01: "the nsf and jinrai logos in their respective huds like a watermark in a corner",
// "very small, lowish transparency"; then "screen corner"): NT's own 128 px team marks, grey masks tinted in the team's
// colour, in the screen's bottom corner on the gun's side (the chat and the squad list hold the left). Issued kit,
// on the deep layer, under the readout. Only on a team, and not in the neutral grid.
static void PaintTeamMark(const Frame &f)
{
	static ConVarRef cl_neo_hud_grid("cl_neo_hud_grid");
	const int team = GetLocalPlayerTeam();
	if (!cl_neo_hud_team_mark.GetBool() || (team != TEAM_JINRAI && team != TEAM_NSF) || (cl_neo_hud_grid.IsValid() && cl_neo_hud_grid.GetInt() == 2))
		return;
	static int s_textures[2] = { -1, -1 };
	const int k = team == TEAM_NSF ? 1 : 0;
	if (s_textures[k] < 0)
	{
		s_textures[k] = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile(s_textures[k], k ? "vgui/nsf_128tm" : "vgui/jinrai_128tm", true, false);
	}
	const float size = MARK * f.s, inset = 24.0f * f.s;
	const float x0 = f.hand > 0 ? f.wide - inset - size : inset, y1 = f.tall - inset;
	NeoGhostFlush();
	const Color c = NeoHud::TeamColour(team);
	vgui::surface()->DrawSetTexture(s_textures[k]);
	vgui::surface()->DrawSetColor(c.r(), c.g(), c.b(), Alpha(f, MARK_ALPHA));
	vgui::surface()->DrawTexturedRect(RoundFloatToInt(x0), RoundFloatToInt(y1 - size), RoundFloatToInt(x0 + size), RoundFloatToInt(y1));
}

// The etched ruler in the faction's hand (every module wears it): Jinrai's issued and heavy, quarters only with long
// cuts; NSF's fine and many, a tick every twentieth. Neutral and the plain crosses: a tick every tenth.
static int RulerDivisions(int team)
{
	return team == TEAM_NSF ? 20 : team == TEAM_JINRAI ? 4 : 10;
}

// Where a module's registration cross nearest `toward` sits (screen pixels, on the deep layer, as the frame draws them):
// the corners of its box, the ring's two rail ends and its front. For the couplings (neo_cyberbrain_couple.cpp).
bool FrameCross(const Frame &frame, int slot, const Vector2D &toward, Vector2D &out)
{
	if ((slot == GROUP_WEAPON && !frame.pSenses->ammo.bShown && !frame.pSenses->bGhost) || slot == GROUP_LINK
		|| (slot == BRIGHT_RING && (frame.ringRadii.x <= 0.0f || frame.style == NEO_HUD_STYLE_BODY)))
		return false;
	const Frame f = ForGroup(frame, slot);
	Vector2D centre, half;
	GroupExtent(f, slot, centre, half);
	centre.y -= CHASSIS_BELOW * f.s * 0.5f;
	half.y -= CHASSIS_BELOW * f.s * 0.5f;
	centre += (slot == BRIGHT_RING) ? RingDeepOffset() : f.pPlaces[slot].deep - f.pPlaces[slot].pos;
	const float s = f.s;
	const float left = centre.x - half.x - 10.0f * s, right = centre.x + half.x + 10.0f * s, top = centre.y - half.y - 8.0f * s,
		rail = centre.y + half.y + 6.0f * s;
	Vector2D points[4] = { Vector2D(left, top), Vector2D(right, top), Vector2D(left, rail), Vector2D(right, rail) };
	int n = 4;
	if (slot == BRIGHT_RING)
	{
		points[0].Init(left, rail);
		points[1].Init(right, rail);
		points[2].Init(centre.x, top - 4.0f * s);
		n = 3;
	}
	float best = FLT_MAX;
	for (int i = 0; i < n; ++i)
	{
		const float d = (points[i] - toward).LengthSqr();
		if (d < best)
		{
			best = d;
			out = points[i];
		}
	}
	return true;
}

void PaintFrame(const Frame &frame)
{
	PaintTeamMark(frame);
	static ConVarRef cl_neo_hud_grid("cl_neo_hud_grid");
	const int team = (cl_neo_hud_grid.IsValid() && cl_neo_hud_grid.GetInt() == 1) ? GetLocalPlayerTeam() : 0;
	const int divisions = RulerDivisions(team);
	for (int slot = 0; slot <= BRIGHT_RING; ++slot)
	{
		if ((slot == GROUP_WEAPON && !frame.pSenses->ammo.bShown) || slot == GROUP_LINK || (slot == BRIGHT_RING && frame.style == NEO_HUD_STYLE_BODY))
		{
			continue;
		}
		const Frame f = ForGroup(frame, slot);
		ProbeOwner(static_cast<ProbeOwnerId>(slot));	// a group owns its frame (R1); the ring is PROBE_RING
		Vector2D centre, half;
		GroupExtent(f, slot, centre, half);
		centre.y -= CHASSIS_BELOW * f.s * 0.5f;	// the chassis is what the extent's reserve is for: it hangs from the bare box
		half.y -= CHASSIS_BELOW * f.s * 0.5f;
		centre += (slot == BRIGHT_RING) ? RingDeepOffset() : f.pPlaces[slot].deep - f.pPlaces[slot].pos;
		const float s = f.s, m = static_cast<float>(f.hand);
		const float left = centre.x - half.x, right = centre.x + half.x, top = centre.y - half.y, rail = centre.y + half.y + 6.0f * s;
		const float cross = CROSS * s;

		// Registration crosses: a receptor group's are the grid's (its four corners, notching onto the screen's grid);
		// otherwise both rail ends, and the top corner away from the gun (the ring: its ends and its front).
		if (slot != BRIGHT_RING && GridOn())
		{
			PaintGrid(f, slot, left - 10.0f * s, right + 10.0f * s, top - 8.0f * s, rail);
		}
		else if (slot == BRIGHT_RING)
		{
			Cross(f, Vector2D(left - 10.0f * s, rail), cross, CROSS_ALPHA);
			Cross(f, Vector2D(right + 10.0f * s, rail), cross, CROSS_ALPHA);
			Cross(f, Vector2D(centre.x, top - 12.0f * s), cross, CROSS_ALPHA);
		}
		else
		{
			Cross(f, Vector2D(left - 10.0f * s, rail), cross, CROSS_ALPHA);
			Cross(f, Vector2D(right + 10.0f * s, rail), cross, CROSS_ALPHA);
			Cross(f, Vector2D(m > 0.0f ? left - 10.0f * s : right + 10.0f * s, top - 8.0f * s), cross, CROSS_ALPHA);
		}

		// The etched ruler: a fine tick every tenth, longer at the quarters, a centre mark.
		Line(f, Vector2D(left, rail), Vector2D(right, rail), NEO_GHOST_LIGHT, f.color, RULE_ALPHA);
		for (int k = 0; k <= divisions; ++k)
		{
			const float x = left + (right - left) * k / divisions;
			const bool bMid = k * 2 == divisions, bQuarter = (k * 4) % divisions == 0;
			const float down = bMid ? 5.0f : bQuarter ? (team == TEAM_JINRAI ? 6.0f : 4.0f) : 2.0f, up = bMid ? 4.0f : 0.0f;
			Line(f, Vector2D(x, rail - up * s), Vector2D(x, rail + down * s), NEO_GHOST_LIGHT, f.color, bMid ? 0.45f : RULE_ALPHA);
		}

		// Channel codes under the ruler: the group's at its outer end (away from the gun), the class's channel at the other.
		const Senses &sense = *f.pSenses;
		const wchar_t *pMotion = sense.bHasJumps ? L"JMP.CH1" : sense.bHasSprint ? L"AUX.CH1" : L"MOV.CH1";
		const wchar_t *s_codes[] = { L"INT.CH0", L"TOC.CH2", L"WPN.CH3", L"LNK.CH4", pMotion, L"AUD.CH5" };
		const float codeY = rail + 13.0f * s;
		const bool bOuterLeft = m > 0.0f;
		Text(f, s_codes[slot], bOuterLeft ? left : right, codeY, bOuterLeft ? 1 : -1, FONT_VALUE, f.color, ETCHED);
		if (slot == GROUP_BODY && sense.bArmour)
		{
			Text(f, L"ARM.CH0", bOuterLeft ? right : left, codeY, bOuterLeft ? -1 : 1, FONT_VALUE, f.color, ETCHED);
		}
	}
}

constexpr float SNAP_FOR = 0.18f;		// seconds the brackets take to snap in on a rise
constexpr float SCAN_FOR = 0.25f;		// seconds the scan takes down the group
constexpr float FALL_FOR = 0.35f;		// seconds the old marks take to drift out and go
constexpr float PULSE_FOR = 1.5f;		// seconds a new critical pulses
constexpr float PAD = 5.0f;				// pixels at 1080p between the extent and its marks

// One layer's marks round the box (a top left, b bottom right), `out` pixels further out than their place.
static void Marks(const Frame &f, Group group, int layer, const Vector2D &a, const Vector2D &b, float out, float alpha)
{
	if (layer <= LAYER_AMBIENT || alpha <= 0.01f)
		return;
	const float s = f.s;
	const Vector2D p0 = a - Vector2D(out, out), p1 = b + Vector2D(out, out);
	const float arm = (layer == LAYER_NOTABLE ? 6.0f : layer == LAYER_URGENT ? 12.0f : 16.0f) * s;
	const NeoGhostWeight w = layer == LAYER_NOTABLE ? NEO_GHOST_LIGHT : layer == LAYER_URGENT ? NEO_GHOST_MEDIUM : NEO_GHOST_HEAVY;
	const float strength = layer == LAYER_NOTABLE ? 0.35f : layer == LAYER_URGENT ? 0.6f : 0.85f;
	const Vector2D corners[4] = { p0, Vector2D(p1.x, p0.y), p1, Vector2D(p0.x, p1.y) };
	for (int i = 0; i < 4; ++i)
	{
		const Vector2D c = corners[i];
		const float dx = (i == 0 || i == 3) ? 1.0f : -1.0f, dy = (i < 2) ? 1.0f : -1.0f;
		Line(f, c, c + Vector2D(dx * arm, 0.0f), w, f.color, strength * alpha);
		Line(f, c, c + Vector2D(0.0f, dy * arm), w, f.color, strength * alpha);
		if (layer >= LAYER_CRITICAL)
		{
			// Doubled inside.
			const Vector2D in = c + Vector2D(dx, dy) * (3.0f * s);
			Line(f, in, in + Vector2D(dx * arm * 0.5f, 0.0f), NEO_GHOST_LIGHT, f.color, 0.6f * alpha);
			Line(f, in, in + Vector2D(0.0f, dy * arm * 0.5f), NEO_GHOST_LIGHT, f.color, 0.6f * alpha);
		}
	}
	if (layer >= LAYER_URGENT)
	{
		// The rail along the top, between the brackets, and the layer's code over its outer end.
		for (float x = p0.x + arm + 4.0f * s; x < p1.x - arm - 4.0f * s; x += 5.0f * s)
			Line(f, Vector2D(x, p0.y), Vector2D(Min(x + 2.0f * s, p1.x - arm - 4.0f * s), p0.y), NEO_GHOST_LIGHT, f.color, 0.3f * alpha);
		const bool bRight = f.hand < 0;	// the side away from the gun
		// A budget word (gate 2): it takes a slot like a plate, crystallising in and out with it.
		wchar_t word[4];
		Text(f, Crystallise(f, group, layer >= LAYER_CRITICAL ? L"P3" : L"P2", word, ARRAYSIZE(word)), bRight ? p1.x : p0.x,
			p0.y - 7.0f * s, bRight ? -1 : 1, FONT_LABEL, layer >= LAYER_CRITICAL ? CRIT : f.color, 0.85f * alpha * WordAlpha(f, group));
	}
}

void PaintLayer(const Frame &f, Group group)
{
	const Place &p = f.pPlaces[group];
	if (group == GROUP_WEAPON && !f.pSenses->ammo.bShown && !f.pSenses->bGhost)
		return;
	Vector2D centre, half;
	GroupExtent(f, group, centre, half);
	const Vector2D pad(PAD * f.s, PAD * f.s);
	const Vector2D a = centre - half - pad, b = centre + half + pad;
	const float age = f.now - p.layerChanged, alpha = f.alpha;
	const bool bRose = p.layer > p.lastLayer;
	static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
	const int motion = cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1;
	const bool bFull = motion >= 2;
	if (bRose)
	{
		if (motion == 0)
		{
			Marks(f, group, p.layer, a, b, 0.0f, NeoSmoothStep(age / 0.4f) * alpha);
			return;
		}
		const float snap = NeoSmoothStep(age / (bFull ? SNAP_FOR : 0.3f));
		float strength = 1.0f;
		if (bFull && p.layer >= LAYER_CRITICAL && age < PULSE_FOR)
			strength = 0.75f + 0.25f * cosf(age * 2.0f * M_PI_F * 2.0f);
		Marks(f, group, p.layer, a, b, (1.0f - snap) * (bFull ? 18.0f : 8.0f) * f.s, (0.4f + 0.6f * snap) * strength * alpha);
		if (bFull && age < SCAN_FOR)
		{
			const float y = a.y + (b.y - a.y) * (age / SCAN_FOR);
			Line(f, Vector2D(a.x, y), Vector2D(b.x, y), NEO_GHOST_LIGHT, f.color, 0.6f * (1.0f - age / SCAN_FOR) * alpha);
		}
		return;
	}
	Marks(f, group, p.layer, a, b, 0.0f, alpha);
	const float fallFor = bFull ? FALL_FOR : motion == 0 ? 0.6f : 0.4f;
	if (age < fallFor)
	{
		const float t = age / fallFor;
		Marks(f, group, p.lastLayer, a, b, bFull ? t * 12.0f * f.s : 0.0f, (1.0f - t) * alpha);
	}
}
} // namespace NeoCyberbrain
