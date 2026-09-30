#pragma once

// The kill feed's entries (HUD-SYSTEM.md, the models layer): the HUD's own copy of the death notice's events (the
// stock one keeps its side effects: the round's killers for the spectator overlay, your kills for the scoreboard, the
// console lines stats plugins read), listened for here in every style. The same kinds, filters and lifetime as the
// stock feed (hud_deathnotice_time, cl_neo_hud_extended_killfeed; eight at most, the oldest first): a kill ("killer
// [+ assist] weapon [explosive] [headshot] victim [ghost]"), a suicide or world death (the shortbus), a rank change,
// the ghost captured, the VIP extracted or dead. No painting: each style picks its faces for the segments.

#include "Color.h"

namespace NeoHud
{
enum FeedKind
{
	FEED_NAME,		// a player's name, in their team's colour
	FEED_WORDS,		// a phrase ("HAS CAPTURED THE")
	FEED_GAP,		// the spaces and joins between the rest (" + ")
	FEED_ICON,		// one of NT's killfeed glyphs
};

constexpr int FEED_MAX = 8, FEED_SEGMENTS = 12;
struct FeedSegment { wchar_t text[64]; FeedKind kind; Color color; };
struct FeedEntry { FeedSegment seg[FEED_SEGMENTS]; int count; float hide; bool bInvolved; };

// Retires the expired entries and returns the rest, oldest first; none while the scoreboard hides the feed.
int FeedEntries(const FeedEntry **ppEntries);
void ResetFeed();
} // namespace NeoHud
