#pragma once

// Localization lookup with an English fallback (LOCALIZATION.md, "The base"). A token is the key in a language file
// (resource/neo_<language>.txt, UTF-16 LE with a BOM; any subset of neo_english.txt). Find returns the current language's
// string, else the English string (neo_english.txt, loaded into a table of our own so the fallback never depends on how the
// engine treats a missing token), else `pEnglish`, the English written at the call site while its token is still being
// added, else the token's name in brackets (loud, so a missing token shows in testing). With neo_loc_pseudo 1 it returns
// the English stretched by 30%, swapped to Cyrillic look-alikes and bracketed instead: untranslated code stays plain
// English and stands out, and a clipped bracket or a blank glyph shows a layout or a face that cannot take Russian.
namespace NeoLoc
{
// The string for a token (with or without a leading #). The pointer is valid until the next 16 pseudo-localized calls and
// otherwise as long as the localization tables are.
const wchar_t *Find(const char *pToken, const wchar_t *pEnglish = nullptr);
// Whether pseudo-localization is on (neo_loc_pseudo), for code that formats its own text.
bool IsPseudo();
// The game's language (lower case, "english" if nothing says otherwise): the -language launch option, else Steam's.
const char *Language();
// The second line under a layered element (LOCALIZATION.md, "layer"): the pack's string for the token, or null when the
// language is English or the pack lacks it (never the English: the English is already the element above it). With
// neo_loc_pseudo 1, the pseudo form of `pEnglish`, so a layered line can be laid out in an English game.
const wchar_t *FindLine(const char *pToken, const wchar_t *pEnglish);
// The client scheme to load: resource/ClientScheme_<language>.res if the language is not English and that file exists in any
// search path (a language pack's, which #base-includes ClientScheme.res and overrides only the faces it needs), else
// resource/ClientScheme.res. Every load of the "ClientScheme" scheme uses it, so there is only ever one.
const char *ClientSchemePath();
}
