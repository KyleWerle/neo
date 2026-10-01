#!/usr/bin/env python3
"""Lists the display strings still hard-coded in the client UI (LOCALIZATION.md): wide-string literals with letters in them.

Usage: loc_lint.py [client/neo directory]   (default: src/game/client/neo next to this script's repo)
Prints a count per file, most first, and the total. The goal is zero. A literal is skipped if its line also has a
NeoLoc::Find or Word( call, if it is only digits, format codes or punctuation, or if the line is marked `// loc: keep`
(a name, a code or a unit that is never translated)."""
import re, sys, pathlib

LITERAL = re.compile(r'L"((?:[^"\\]|\\.)*)"')
LETTERS = re.compile(r'[A-Za-z]{2,}')
NOISE = re.compile(r'%[0-9.]*[a-z]+|\\[nrt]')


def scan(root):
    counts = {}
    for path in sorted(root.rglob('*.cpp')):
        n = 0
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            if 'loc: keep' in line or 'NeoLoc::Find' in line or 'Word(' in line:
                continue
            for lit in LITERAL.findall(line):
                if LETTERS.search(NOISE.sub('', lit)):
                    n += 1
        if n:
            counts[str(path.relative_to(root))] = n
    return counts


if __name__ == '__main__':
    default = pathlib.Path(__file__).resolve().parent.parent / 'src' / 'game' / 'client' / 'neo'
    counts = scan(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else default)
    for name, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f'{n:5d}  {name}')
    print(f'{sum(counts.values()):5d}  total')
