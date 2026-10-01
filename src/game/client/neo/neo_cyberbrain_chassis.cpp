#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_hud_model_team.h"
#include "neo_enums.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The etched chassis, after the racer band's instrument detail: round each group and the ring, on the deep layer,
// registration crosses (plain small crosses), an etched ruler under it with fine graduations, and its channel codes.
// The deep layer trails its group on a softer spring and drifts a few pixels as you turn (neo_cyberbrain_attention.cpp),
// so the crosses read as sitting behind the readout: the depth. Steady: it doesn't fade with attention.

ConVar cl_neo_hud_team_mark("cl_neo_hud_team_mark", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's faction mark: your team's logo (NT's own Jinrai and NSF marks), small and faint in the body"
	" group's corner, as a watermark on its chassis. Not in the neutral grid (cl_neo_hud_grid 2).", true, 0, true, 1);

namespace NeoCyberbrain
{
constexpr float CROSS = 4.0f;			// a cross's half size, 1080p
constexpr float CROSS_ALPHA = 0.5f, RULE_ALPHA = 0.3f, ETCHED = 0.7f;
constexpr float MARK = 20.0f, MARK_ALPHA = 0.25f;	// the faction mark's size, 1080p, and strength

// The faction mark (Kyle, 2026-10-01: "the nsf and jinrai logos in their respective huds like a watermark in a corner
// of their neural perception hud", "very small, lowish transparency"): NT's own 128 px team marks, grey masks tinted
// in the team's colour, in the body group's lower corner on the gun's side, just above its ruler. Issued kit: it sits
// on the chassis, under the readout, and drifts with the deep layer. Only on a team, and not in the neutral grid.
static void PaintTeamMark(const Frame &f, float left, float right, float rail)
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
	const float size = MARK * f.s, inset = 4.0f * f.s;
	const float x0 = f.hand > 0 ? right - inset - size : left + inset, y1 = rail - inset;
	NeoGhostFlush();
	const Color c = NeoHud::TeamColour(team);
	vgui::surface()->DrawSetTexture(s_textures[k]);
	vgui::surface()->DrawSetColor(c.r(), c.g(), c.b(), Alpha(f, MARK_ALPHA));
	vgui::surface()->DrawTexturedRect(RoundFloatToInt(x0), RoundFloatToInt(y1 - size), RoundFloatToInt(x0 + size), RoundFloatToInt(y1));
}

void PaintChassis(const Frame &frame)
{
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

		if (slot == GROUP_BODY)
		{
			PaintTeamMark(f, left, right, rail);
		}

		// The etched ruler: a fine tick every tenth, longer at the quarters, a centre mark.
		Line(f, Vector2D(left, rail), Vector2D(right, rail), NEO_GHOST_LIGHT, f.color, RULE_ALPHA);
		for (int k = 0; k <= 10; ++k)
		{
			const float x = left + (right - left) * k / 10.0f;
			const float down = (k == 5) ? 5.0f : (k % 5 == 0) ? 4.0f : 2.0f, up = (k == 5) ? 4.0f : 0.0f;
			Line(f, Vector2D(x, rail - up * s), Vector2D(x, rail + down * s), NEO_GHOST_LIGHT, f.color, (k == 5) ? 0.45f : RULE_ALPHA);
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
} // namespace NeoCyberbrain
