#include "cbase.h"
#include "neo_cyberbrain_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The etched chassis, after the racer band's instrument detail: round each group and the ring, on the deep layer,
// registration crosses (plain small crosses), an etched ruler under it with fine graduations, and its channel codes.
// The deep layer trails its group on a softer spring and drifts a few pixels as you turn (neo_cyberbrain_attention.cpp),
// so the crosses read as sitting behind the readout: the depth. Steady: it doesn't fade with attention.

namespace NeoCyberbrain
{
constexpr float CROSS = 4.0f;			// a cross's half size, 1080p
constexpr float CROSS_ALPHA = 0.5f, RULE_ALPHA = 0.3f, ETCHED = 0.7f;

void PaintChassis(const Frame &frame)
{
	for (int slot = 0; slot <= BRIGHT_RING; ++slot)
	{
		if ((slot == GROUP_WEAPON && !frame.pSenses->ammo.bShown) || slot == GROUP_LINK || (slot == BRIGHT_RING && frame.style == NEO_HUD_STYLE_BODY))
		{
			continue;
		}
		const Frame f = ForGroup(frame, slot);
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
