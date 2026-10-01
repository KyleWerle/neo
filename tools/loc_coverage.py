#!/usr/bin/env python3
"""How complete a language pack is against neo_english.txt (LOCALIZATION.md).

Usage: loc_coverage.py <pack folder> [english file]
  e.g. loc_coverage.py game/neo/custom/lang_russian
Reads <pack>/resource/neo_<language>.txt (the language is the pack folder's lang_ suffix), prints the coverage percent,
the cyberbrain HUD block's on its own line, the tokens still missing, the tokens that no longer exist in English, and the
%s1 style arguments that don't match. Exit code 1 if any argument mismatches."""
import re, sys, pathlib

PAIR = re.compile(r'^\s*"([^"]+)"\s+"((?:[^"\\]|\\.)*)"', re.M)
ARGS = re.compile(r'%[sd]?\d?')


def tokens(path):
    raw = path.read_bytes()
    text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8-sig')
    return {k: v for k, v in PAIR.findall(text) if k != 'Language'}


def main():
    pack = pathlib.Path(sys.argv[1])
    root = pathlib.Path(__file__).resolve().parent.parent
    english_path = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'game' / 'neo' / 'resource' / 'neo_english.txt'
    language = pack.name.removeprefix('lang_')
    mine = tokens(pack / 'resource' / f'neo_{language}.txt')
    english = tokens(english_path)
    done = [k for k in english if k in mine]
    missing = [k for k in english if k not in mine]
    stale = [k for k in mine if k not in english]
    bad = [k for k in done if sorted(ARGS.findall(english[k])) != sorted(ARGS.findall(mine[k]))]
    hud = [k for k in english if k.startswith('neo_hud_cb_')]
    hud_done = [k for k in hud if k in mine]
    print(f'{language}: {len(done)}/{len(english)} tokens, {100 * len(done) / max(1, len(english)):.0f}%')
    if hud:
        print(f'cyberbrain HUD: {len(hud_done)}/{len(hud)} tokens, {100 * len(hud_done) / len(hud):.0f}%')
    for title, items in (('missing', missing), ('no longer in English', stale), ('argument mismatch', bad)):
        if items:
            print(f'{title} ({len(items)}):')
            for k in items:
                print('   ', k)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
