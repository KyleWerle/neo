#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_hud_profile.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The language budget (HUD-REWORK.md, R7; gate 5: loose when quiet, readable always). The plates that say whose group
// this is (BIOMECH, OPTICS, MOTION, WPN) and the layer codes are words the HUD can afford or not: when it's quiet the
// budget is full, every group names itself; in a fight it is down to the groups in the critical layer. Words rank by
// the group's attention, so a pulled-in group is named before a resting one. The parity floor (integrity, rounds,
// RELOAD / LOW / OUT / OVERHEAT, HOLSTERED, the vision plate) is outside the budget and never goes through here.
// A word that isn't allowed fades back out; one that is allowed crystallises in (the comfort forms, HUD-REWORK.md's
// table): Full glyph by glyph over 0.2 s, Calm glyph by glyph with a fade over 0.3 s, Still whole, in over 0.5 s and out
// over 0.8 s.

namespace NeoCyberbrain
{
constexpr int MOST_WORDS = 4;	// the budget when all quiet: one plate for each of the four groups

struct WordForm { float inFor, outFor; bool bGlyphs; };
static WordForm WordFormOf()
{
	static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
	switch (cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1)
	{
	case 0:		return { 0.5f, 0.8f, false };
	case 2:		return { 0.2f, 0.2f, true };
	default:	return { 0.3f, 0.3f, true };
	}
}

static float s_reveal[GROUP__COUNT];	// each group's word, 0 to 1, eased

// Which groups the budget names now, and each one's reveal; once a frame.
static void UpdateWords(const Frame &f)
{
	static int s_frame = -1;
	if (s_frame == gpGlobals->framecount)
		return;
	CNeoHudProfileScope profile(NEO_HUD_PROFILE_VITALS_WORDS);	// nested in whichever part called first: v.words is a slice of it
	const bool bFirst = s_frame < 0 || s_frame != gpGlobals->framecount - 1;
	s_frame = gpGlobals->framecount;

	// The groups that can say anything, the critical ones first, then by attention.
	int order[GROUP__COUNT], n = 0, critical = 0;
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		if (g == GROUP_LINK || (g == GROUP_WEAPON && !f.pSenses->ammo.bShown && !f.pSenses->bGhost))
			continue;
		order[n++] = g;
	}
	const auto rank = [&f](int g)
	{
		return f.pPlaces[g].att + (f.pPlaces[g].layer >= LAYER_CRITICAL ? 10.0f : 0.0f);
	};
	for (int i = 1; i < n; ++i)	// a handful of groups: insertion sort, most important first
	{
		for (int j = i; j > 0 && rank(order[j]) > rank(order[j - 1]); --j)
			{ const int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
	}
	for (int i = 0; i < n; ++i)
		critical += f.pPlaces[order[i]].layer >= LAYER_CRITICAL ? 1 : 0;
	const int slots = Max(critical, 1 + RoundFloatToInt((MOST_WORDS - 1) * Listening()));

	const WordForm form = WordFormOf();
	float s_shown[GROUP__COUNT] = {};
	for (int i = 0; i < n && i < slots; ++i)
		s_shown[order[i]] = 1.0f;
	for (int g = 0; g < GROUP__COUNT; ++g)
	{
		const float dur = s_shown[g] > s_reveal[g] ? form.inFor : form.outFor;
		s_reveal[g] = bFirst ? 0.0f : Approach(s_shown[g], s_reveal[g], gpGlobals->frametime / dur);
	}
}

float WordReveal(const Frame &f, Group group)
{
	UpdateWords(f);
	return s_reveal[group];
}

float WordAlpha(const Frame &f, Group group)
{
	const float r = WordReveal(f, group);
	return WordFormOf().bGlyphs && r > 0.0f ? Min(1.0f, r * 4.0f) : NeoSmoothStep(r);	// glyphs arrive solid, whole words ease
}

const wchar_t *Crystallise(const Frame &f, Group group, const wchar_t *pWord, wchar_t *pBuf, int size)
{
	if (!WordFormOf().bGlyphs)
		return pWord;
	const int len = static_cast<int>(wcslen(pWord));
	const int shown = Min(len, Min(size - 1, static_cast<int>(ceilf(WordReveal(f, group) * len))));
	V_wcsncpy(pBuf, pWord, (shown + 1) * sizeof(wchar_t));
	pBuf[shown] = 0;
	return pBuf;
}
} // namespace NeoCyberbrain
