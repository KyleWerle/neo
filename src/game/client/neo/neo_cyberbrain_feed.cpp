#include "cbase.h"
#include "neo_cyberbrain_team.h"
#include "c_neo_player.h"
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The kill feed, top right: the HUD's copy of the death notice's entries (neo_hud_model_feed.h), in NT's killfeed icon
// glyphs between team-coloured names, the oldest on top. Entries you're in are bracketed.

namespace NeoCyberbrain
{
constexpr float FEED_Y = 16.0f, FEED_ROW = 30.0f, FEED_RIGHT = 16.0f;

using NeoHud::FeedEntry;
using Entry = FeedEntry;

// The cyberbrain's faces for the feed's kinds of segment.
static Font FontOf(NeoHud::FeedKind kind)
{
	switch (kind)
	{
	case NeoHud::FEED_NAME:		return FONT_NAME;
	case NeoHud::FEED_WORDS:	return FONT_LABEL;
	case NeoHud::FEED_ICON:		return FONT_ICONS;
	default:					return FONT_VALUE;
	}
}

void PaintFeed(const Frame &f)
{
	const FeedEntry *pFeed;
	const int count = NeoHud::FeedEntries(&pFeed);
	const float s = f.s, right = f.wide - FEED_RIGHT * s;
	for (int i = 0; i < count; ++i)
	{
		const Entry &e = pFeed[i];
		float width = 0.0f;
		for (int k = 0; k < e.count; ++k)
			width += TextWidth(e.seg[k].text, FontOf(e.seg[k].kind));
		const float y = (FEED_Y + FEED_ROW * 0.5f + i * FEED_ROW) * s, x0 = right - width;
		// A faint dark strip behind (low: it shouldn't read as a panel), and your own entries bracketed.
		Rect(f, Vector2D(x0 - 10.0f * s, y - 12.0f * s), Vector2D(right + 6.0f * s, y + 12.0f * s), Color(0, 0, 0, 255), 0.35f);
		if (e.bInvolved)
		{
			const float bx0 = x0 - 12.0f * s, bx1 = right + 8.0f * s, by0 = y - 13.0f * s, by1 = y + 13.0f * s, arm = 6.0f * s;
			Line(f, Vector2D(bx0, by0), Vector2D(bx0 + arm, by0), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx0, by0), Vector2D(bx0, by1), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx0, by1), Vector2D(bx0 + arm, by1), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx1, by0), Vector2D(bx1 - arm, by0), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx1, by0), Vector2D(bx1, by1), NEO_GHOST_MEDIUM, f.color, 0.8f);
			Line(f, Vector2D(bx1, by1), Vector2D(bx1 - arm, by1), NEO_GHOST_MEDIUM, f.color, 0.8f);
		}
		float x = x0;
		for (int k = 0; k < e.count; ++k)
			x += Text(f, e.seg[k].text, x, y, 1, FontOf(e.seg[k].kind), e.seg[k].color, 0.95f);
	}
}
} // namespace NeoCyberbrain
