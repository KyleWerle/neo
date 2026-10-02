#include "cbase.h"
#include "neo_loc.h"
#include "cdll_client_int.h"
#include "steam/steam_api.h"
#include <vgui/ILocalize.h>
#include <string>
#include <unordered_map>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar neo_loc_pseudo("neo_loc_pseudo", "0", FCVAR_NONE,
	"Pseudo-localization: every localized string comes back stretched by 30%, in Cyrillic look-alikes, in brackets. Plain"
	" English that stays is a string not yet in a language file; a clipped bracket is a layout Russian cannot fit.", true, 0, true, 1);

namespace NeoLoc
{
// The English table: neo_english.txt parsed once (it is a KeyValues file in UTF-16, converted to UTF-8 to read).
static std::unordered_map<std::string, std::wstring> &English()
{
	static std::unordered_map<std::string, std::wstring> s_table;
	static bool s_loaded = false;
	if (s_loaded)
		return s_table;
	s_loaded = true;
	CUtlBuffer raw;
	if (!g_pFullFileSystem->ReadFile("resource/neo_english.txt", "GAME", raw) || raw.TellPut() < 4)
		return s_table;
	const wchar_t *pWide = reinterpret_cast<const wchar_t *>(raw.Base());
	int wideLen = raw.TellPut() / static_cast<int>(sizeof(wchar_t));
	if (wideLen > 0 && pWide[0] == 0xFEFF)
	{
		++pWide;
		--wideLen;
	}
	CUtlVector<char> utf8;
	utf8.SetCount(wideLen * 4 + 1);
	V_UnicodeToUTF8(pWide, utf8.Base(), utf8.Count());
	KeyValues *pKv = new KeyValues("lang");
	pKv->UsesEscapeSequences(true);
	if (pKv->LoadFromBuffer("neo_english", utf8.Base()))
	{
		if (KeyValues *pTokens = pKv->FindKey("Tokens"))
		{
			for (KeyValues *pToken = pTokens->GetFirstValue(); pToken; pToken = pToken->GetNextValue())
			{
				wchar_t wide[1024];
				V_UTF8ToUnicode(pToken->GetString(), wide, sizeof(wide));
				s_table[pToken->GetName()] = wide;
			}
		}
	}
	pKv->deleteThis();
	return s_table;
}

bool IsPseudo()
{
	return neo_loc_pseudo.GetBool();
}

const char *Language()
{
	static char s_language[64] = "";
	if (!s_language[0])
	{
		const char *pLanguage = CommandLine() ? CommandLine()->ParmValue("-language", "") : "";
		if ((!pLanguage || !pLanguage[0]) && steamapicontext && steamapicontext->SteamApps())
			pLanguage = steamapicontext->SteamApps()->GetCurrentGameLanguage();
		V_strncpy(s_language, (pLanguage && pLanguage[0]) ? pLanguage : "english", sizeof(s_language));
		V_strlower(s_language);
	}
	return s_language;
}

// The engine loads resource/neo_<language>.txt for Steam's language and ignores -language, so the pack's tokens could
// disagree with its scheme. Load the file for our language once (from any search path, a pack's included), before the
// first lookup or scheme load; for Steam's own language this only loads the same file again.
static void LoadLanguageFile()
{
	static bool s_done = false;
	if (s_done || !g_pVGuiLocalize || !g_pFullFileSystem)
		return;
	s_done = true;
	if (V_stricmp(Language(), "english") == 0)
		return;
	char file[128];
	V_snprintf(file, sizeof(file), "resource/neo_%s.txt", Language());
	if (g_pFullFileSystem->FileExists(file, "GAME"))
		g_pVGuiLocalize->AddFile(file, "GAME", true);
}

const char *ClientSchemePath()
{
	LoadLanguageFile();
	static char s_path[128] = "";
	if (!s_path[0])
	{
		V_strncpy(s_path, "resource/ClientScheme.res", sizeof(s_path));
		if (V_stricmp(Language(), "english") != 0)
		{
			char localized[128];
			V_snprintf(localized, sizeof(localized), "resource/ClientScheme_%s.res", Language());
			if (g_pFullFileSystem->FileExists(localized, "GAME"))
				V_strncpy(s_path, localized, sizeof(s_path));
		}
	}
	return s_path;
}

// Cyrillic look-alikes for the Latin letters that have one, and the stand-ins for the rest.
static wchar_t Lookalike(wchar_t c)
{
	switch (c)
	{
	case 'A': return 0x0410; case 'B': return 0x0412; case 'C': return 0x0421; case 'D': return 0x0414; case 'E': return 0x0415;
	case 'G': return 0x0413; case 'H': return 0x041D; case 'I': return 0x0406; case 'K': return 0x041A; case 'M': return 0x041C;
	case 'N': return 0x041F; case 'O': return 0x041E; case 'P': return 0x0420; case 'R': return 0x042F; case 'T': return 0x0422;
	case 'U': return 0x0426; case 'X': return 0x0425; case 'Y': return 0x0423;
	case 'a': return 0x0430; case 'b': return 0x0432; case 'c': return 0x0441; case 'e': return 0x0435; case 'h': return 0x043D;
	case 'k': return 0x043A; case 'm': return 0x043C; case 'n': return 0x043F; case 'o': return 0x043E; case 'p': return 0x0440;
	case 'r': return 0x0433; case 't': return 0x0442; case 'u': return 0x0438; case 'x': return 0x0445; case 'y': return 0x0443;
	default: return c;
	}
}

static const wchar_t *Pseudo(const wchar_t *pText)
{
	constexpr int SLOTS = 16, SIZE = 512;
	static wchar_t s_ring[SLOTS][SIZE];
	static int s_next = 0;
	wchar_t *pOut = s_ring[s_next++ % SLOTS];
	const int len = static_cast<int>(wcslen(pText));
	int at = 0;
	pOut[at++] = L'[';
	for (int i = 0; i < len && at < SIZE - 8; ++i)
	{
		if (pText[i] == L'%')
		{
			// A format sequence (%s1, %d, %%) stays as it is: the game fills it in.
			pOut[at++] = pText[i++];
			while (i < len && at < SIZE - 8 && ((pText[i] >= L'a' && pText[i] <= L'z') || (pText[i] >= L'0' && pText[i] <= L'9') || pText[i] == L'%'))
				pOut[at++] = pText[i++];
			--i;
			continue;
		}
		pOut[at++] = Lookalike(pText[i]);
	}
	for (int pad = (len * 3 + 9) / 10; pad > 0 && at < SIZE - 2; --pad)
		pOut[at++] = 0x00B7;	// the 30% of stretch
	pOut[at++] = L']';
	pOut[at] = 0;
	return pOut;
}

const wchar_t *FindLine(const char *pToken, const wchar_t *pEnglish)
{
	if (neo_loc_pseudo.GetBool())
		return pEnglish ? Pseudo(pEnglish) : nullptr;
	LoadLanguageFile();
	if (!pToken || !g_pVGuiLocalize || V_stricmp(Language(), "english") == 0)
		return nullptr;
	if (pToken[0] == '#')
		++pToken;
	// The engine's table holds neo_english.txt too: a line the pack lacks comes back as the English, which is the plate.
	const wchar_t *pFound = g_pVGuiLocalize->Find(pToken);
	const auto &english = English();
	const auto it = english.find(pToken);
	return pFound && (it == english.end() || it->second != pFound) ? pFound : nullptr;
}

const wchar_t *Find(const char *pToken, const wchar_t *pEnglish)
{
	if (pToken && pToken[0] == '#')
		++pToken;
	if (!pToken || !pToken[0])
		return pEnglish ? pEnglish : L"";
	LoadLanguageFile();
	if (!neo_loc_pseudo.GetBool() && g_pVGuiLocalize)
	{
		if (const wchar_t *pFound = g_pVGuiLocalize->Find(pToken))
			return pFound;
	}
	const auto &english = English();
	const auto it = english.find(pToken);
	const wchar_t *pSource = it != english.end() ? it->second.c_str() : pEnglish;
	if (!pSource)
	{
		// Loud on purpose: a token nobody wrote shows by name.
		static wchar_t s_missing[8][128];
		static int s_nextMissing = 0;
		wchar_t *pOut = s_missing[s_nextMissing++ % 8];
		V_snwprintf(pOut, 128, L"{%S}", pToken);
		return pOut;
	}
	return neo_loc_pseudo.GetBool() ? Pseudo(pSource) : pSource;
}
} // namespace NeoLoc

// neo_loc_find <token>: what the engine's table has, and what NeoLoc::Find returns. For checking a language pack loads, and
// that the English file's comments parse.
CON_COMMAND(neo_loc_find, "Prints a localization token as the engine sees it and as NeoLoc::Find returns it.")
{
	if (args.ArgC() < 2)
	{
		Msg("Usage: neo_loc_find <token>\n");
		return;
	}
	const char *pToken = args.Arg(1);
	const wchar_t *pEngine = g_pVGuiLocalize ? g_pVGuiLocalize->Find(pToken) : nullptr;
	Msg("engine: %ls\n", pEngine ? pEngine : L"(none)");
	// The console cannot print Cyrillic (the C runtime drops the whole line): non-ASCII goes out as U+ code points.
	const wchar_t *pFound = NeoLoc::Find(pToken);
	char line[512];
	int at = 0;
	for (int i = 0; pFound[i] && at < static_cast<int>(sizeof(line)) - 12; ++i)
		at += pFound[i] < 0x80 ? V_snprintf(line + at, sizeof(line) - at, "%c", static_cast<char>(pFound[i]))
			: V_snprintf(line + at, sizeof(line) - at, "<U+%04X>", static_cast<unsigned>(pFound[i]));
	line[at] = 0;
	Msg("find:   %s (%d characters)\n", line, static_cast<int>(wcslen(pFound)));
}
