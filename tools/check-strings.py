#!/usr/bin/env python3
"""Lists localization keys used in the sources but missing from the table.

Keys in the generated table are C++ literals (non-ASCII escaped as \\xNNNN),
so both sides are compared in that escaped form.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "NulConnect"
KEY = r'L"((?:[^"\\]|\\.)*)"'


def escaped(text: str) -> str:
    out = []
    for ch in text:
        code = ord(ch)
        out.append(ch if code < 0x80 else "\\x%04X" % code)
    return "".join(out)


table = (ROOT / "src" / "core" / "Localization.generated.inc").read_text(encoding="utf-8")
known = set()
for line in table.splitlines():
    match = re.match(r'^\{L"(.*?)", \{L"', line)
    if match:
        known.add(match.group(1).replace('" L"', ""))

used = set()
for path in (ROOT / "src").rglob("*.cpp"):
    for match in re.finditer(r"Tr(?:Format)?\(" + KEY, path.read_text(encoding="utf-8")):
        used.add(escaped(match.group(1)))

missing = sorted(used - known)
for key in missing:
    print(key)
sys.exit(1 if missing else 0)
