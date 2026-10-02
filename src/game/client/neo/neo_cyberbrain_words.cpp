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

	static ConVar cl_neo_hud_words_debug("cl_neo_hud_words_debug", "0", FCVAR_NONE,
		"Cyberbrain HUD: print the words budget (listening level, slots, each group's reveal) once a second.", true, 0, true, 1);
	if (cl_neo_hud_words_debug.GetBool() && static_cast<int>(f.now) != static_cast<int>(f.now - gpGlobals->frametime))
		Msg("words: listening %.2f action %.2f slots %d critical %d | reveal body %.2f optics %.2f weapon %.2f motion %.2f | att %.2f %.2f %.2f %.2f\n",
			Listening(), Action(*f.pSenses, f.now), slots, critical, s_reveal[GROUP_BODY], s_reveal[GROUP_OPTICS], s_reveal[GROUP_WEAPON],
			s_reveal[GROUP_MOTION], f.pPlaces[GROUP_BODY].att, f.pPlaces[GROUP_OPTICS].att, f.pPlaces[GROUP_WEAPON].att, f.pPlaces[GROUP_MOTION].att);
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
	if (!pWord || !WordFormOf().bGlyphs)
		return pWord;
	const int len = static_cast<int>(wcslen(pWord));
	const int shown = Min(len, Min(size - 1, static_cast<int>(ceilf(WordReveal(f, group) * len))));
	V_wcsncpy(pBuf, pWord, (shown + 1) * sizeof(wchar_t));
	pBuf[shown] = 0;
	return pBuf;
}

// The roll call (HUD-LAYOUT.md's "etched on event", HUD-FONTS.md's loud voice): after the spawn reveal, channel by
// channel, each code types in (NT's boot type-on), holds, and decays glyph by glyph from its end; then the small etched
// code fades back. Comfort forms: Full types in 0.35 s and out 0.3 s; Calm 0.5 s and 0.45 s, fading as it goes; Still
// whole words, in over 0.5 s and out over 0.8 s.
constexpr float ROLL_AFTER = 0.5f;		// seconds after the spawn (the reveal takes 0.45)
constexpr float ROLL_STAGGER = 0.2f;	// seconds between channels
constexpr float ROLL_HOLD = 1.2f;
constexpr float ROLL_ETCHED_FOR = 0.3f;	// the etched code coming back

RollCall RollCallOf(const Frame &f, int channel, int len)
{
	const float spawn = f.pSenses->spawnTime;
	if (spawn < 0.0f || len <= 0)
		return { 0, 0.0f, 1.0f };
	static ConVarRef cl_neo_hud_motion("cl_neo_hud_motion");
	const int motion = cl_neo_hud_motion.IsValid() ? cl_neo_hud_motion.GetInt() : 1;
	const bool bGlyphs = motion != 0;
	const float inFor = motion == 2 ? 0.35f : 0.5f, outFor = motion == 2 ? 0.3f : motion == 1 ? 0.45f : 0.8f;
	const float t = f.now - spawn - ROLL_AFTER - channel * ROLL_STAGGER;
	const float end = inFor + ROLL_HOLD + outFor;
	if (t >= end)
		return { 0, 0.0f, NeoSmoothStep((t - end) / ROLL_ETCHED_FOR) };
	if (t < 0.0f)
		return { 0, 0.0f, 0.0f };	// the place is kept empty from the spawn until this channel's turn
	if (t < inFor)
	{
		const float p = t / inFor;
		return bGlyphs ? RollCall{ Clamp(static_cast<int>(ceilf(p * len)), 1, len), 1.0f, 0.0f } : RollCall{ len, NeoSmoothStep(p), 0.0f };
	}
	if (t < inFor + ROLL_HOLD)
		return { len, 1.0f, 0.0f };
	const float q = (t - inFor - ROLL_HOLD) / outFor;
	if (!bGlyphs)
		return { len, 1.0f - NeoSmoothStep(q), 0.0f };
	return { Max(0, len - static_cast<int>(floorf(q * len))), motion == 1 ? 1.0f - 0.5f * q : 1.0f, 0.0f };
}

// The words specimen (LOCALIZATION.md): a language pack's faces and lengths checked in one screenshot, without waiting
// for each word's moment in play. Left, the machine's calls on their plates (small, then normal; OUT critical); right,
// the optics foot's state words; below, the roll call. Every word goes through Word(), so it shows the pack or pseudo text.
static ConVar cl_neo_hud_words_specimen("cl_neo_hud_words_specimen", "0", FCVAR_NONE,
	"Draw every word the cyberbrain HUD says, as it draws in play, in a column at the screen's centre: for checking a"
	" language pack's faces and lengths.", true, 0, true, 1);

void PaintWordsSpecimen(const Frame &f)
{
	if (!cl_neo_hud_words_specimen.GetBool())
		return;
	struct Token { const char *pToken; const wchar_t *pEnglish; };
	static const Token s_calls[] = { { "neo_hud_cb_reload", L"RELOAD" }, { "neo_hud_cb_low", L"LOW" }, { "neo_hud_cb_out", L"OUT" },
		{ "neo_hud_cb_overheat", L"OVERHEAT" }, { "neo_hud_cb_holstered", L"HOLSTERED" } };
	static const Token s_states[] = { { "neo_hud_cb_cloaked", L"CLOAKED" }, { "neo_hud_cb_exposed", L"EXPOSED" },
		{ "neo_hud_cb_lit", L"LIT" }, { "neo_hud_cb_dim", L"DIM" }, { "neo_hud_cb_dark", L"DARK" } };
	static const Token s_roll[] = { { "neo_hud_cb_rc_int", L"INTEGRITY" }, { "neo_hud_cb_rc_toc", L"THERMOPTIC" },
		{ "neo_hud_cb_rc_vision", L"VISION" }, { "neo_hud_cb_rc_wpn", L"WEAPON" }, { "neo_hud_cb_rc_lnk", L"LINK" },
		{ "neo_hud_cb_rc_jmp", L"JUMP" }, { "neo_hud_cb_rc_aux", L"AUX POWER" }, { "neo_hud_cb_rc_mov", L"MOVEMENT" },
		{ "neo_hud_cb_rc_aud", L"HEARING" } };
	static const wchar_t *s_codes[] = { L"INT.CH0", L"TOC.CH2", L"TOC.CH2", L"WPN.CH3", L"LNK.CH4", L"JMP.CH1", L"AUX.CH1",
		L"MOV.CH1", L"AUD.CH5" };
	const float s = f.s, gap = 12.0f * s, x = f.centre.x;
	float y = f.tall * 0.18f;
	const float callStep = MachinePlateTall(f) + 6.0f * s, stateStep = FontTall(FONT_LABEL) + 4.0f * s;
	for (int i = 0; i < ARRAYSIZE(s_calls); ++i)
	{
		const wchar_t *pWord = Word(s_calls[i].pToken, s_calls[i].pEnglish);
		const float row = y + i * callStep;
		MachinePlate(f, pWord, x - gap, row, -1, 1.0f, i == 2);
		MachinePlate(f, pWord, x - gap * 2.0f - 260.0f * s, row, -1, 1.0f, false, true);
		Text(f, Word(s_states[i].pToken, s_states[i].pEnglish), x + gap, y + i * stateStep, 1, FONT_LABEL, f.color, 1.0f);
	}
	y += ARRAYSIZE(s_calls) * callStep + 16.0f * s;
	const float rollStep = Max(FontTall(FONT_ROLL_CALL), FontTall(FONT_CAPTION)) + 4.0f * s;
	for (int i = 0; i < ARRAYSIZE(s_roll); ++i)
	{
		Text(f, s_codes[i], x - gap, y + i * rollStep, -1, FONT_ROLL_CALL, f.color, 1.0f);
		Text(f, Word(s_roll[i].pToken, s_roll[i].pEnglish), x + gap, y + i * rollStep, 1, FONT_CAPTION, f.color, 1.0f);
	}
	// The layered plates, with their lines, in a column left of the roll call.
	static const Token s_plates[] = { { "neo_hud_cb_plate_biomech", L"BIOMECH" }, { "neo_hud_cb_plate_optics", L"OPTICS" },
		{ "neo_hud_cb_plate_motion", L"MOTION" }, { "neo_hud_cb_plate_wpn", L"WPN" } };
	float py = y;
	for (int i = 0; i < ARRAYSIZE(s_plates); ++i)
	{
		const wchar_t *pLine = PlateLine(s_plates[i].pToken, s_plates[i].pEnglish);
		const float tall = PlateTall(f, s_plates[i].pEnglish, nullptr, pLine);
		Plate(f, s_plates[i].pEnglish, x - gap - 200.0f * s, py + tall * 0.5f, -1, 1.0f, nullptr, pLine);
		py += tall + 8.0f * s;
	}
}
} // namespace NeoCyberbrain
