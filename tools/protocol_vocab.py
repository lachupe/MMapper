#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 The MMapper Authors
"""Writes (or checks) src/frontend/protocol_vocab.json, the frontend protocol's vocabulary.

Read from the sources, never typed twice:
  * the package and module names: the X-macros in src/proxy/GmcpMessage.h and GmcpModule.h;
  * each package's replay kind: the three switches in src/frontend/FrontendReplayCache.cpp;
  * each wire enum's strings: the to_string_view() tables in the parser sources.

mume3d keeps a copy under data/protocol/ and tests its Protocol constants against it.

  tools/protocol_vocab.py            rewrite src/frontend/protocol_vocab.json
  tools/protocol_vocab.py --check    exit 1 when the checked-in file is stale
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "frontend" / "protocol_vocab.json"

# Enums whose strings go out on the wire (FrontendMessages.cpp serialises them with to_string_view()).
ENUM_SOURCES = [
    "src/parser/CombatLines.cpp",
    "src/parser/CharFollowers.cpp",
    "src/parser/ContainerLines.cpp",
    "src/parser/RoomDoors.cpp",
    "src/parser/WeatherLines.cpp",
    "src/parser/ExitLooks.cpp",
    "src/parser/ItemLines.cpp",
    "src/parser/XmlElement.cpp",  # the categories; the tags come from XFOREACH_XML_TAG
]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def x_macro(rel, table):
    """[(UPPER, Camel, normalized, friendly)] of one XFOREACH_<table> macro."""
    text = read(rel)
    start = text.index("#define XFOREACH_" + table)
    end = text.index("/* define", start)
    body = re.sub(r"/\*.*?\*/", "", text[start:end], flags=re.S).replace("\\\n", " ")
    return re.findall(r'X\(\s*(\w+),\s*(\w+),\s*"([^"]*)",\s*"([^"]*)"\s*\)', body)


def replay_kinds():
    text = read("src/frontend/FrontendReplayCache.cpp")
    kinds = {}
    for func, kind in (("isWholeState", "whole"),
                       ("carriesChangedFields", "merge-fields"),
                       ("setChangedBy", "set-change")):
        start = text.index("(const GmcpMessageTypeEnum type)", text.index(func))
        body = text[start:text.index("\n}\n", start)]
        for upper in re.findall(r"case GmcpMessageTypeEnum::(\w+):", body):
            kinds.setdefault(upper, kind)
    # The two keyed Sets are stored whole and changed member by member.
    for upper in ("GROUP_SET", "ROOM_CHARS_SET"):
        kinds[upper] = "keyed-set"
    return kinds


def xml_tags():
    text = read("src/parser/XmlElement.h")
    start = text.index("#define XFOREACH_XML_TAG")
    end = text.index("enum class", start)
    body = re.sub(r"/\*.*?\*/", "", text[start:end], flags=re.S).replace("\\\n", " ")
    return [{"name": n, "wire": w, "category": c.lower()}
            for n, w, c in re.findall(r'X\(\s*(\w+),\s*"([^"]*)",\s*(\w+)\s*\)', body)]


def enums():
    found = {}
    for rel in ENUM_SOURCES:
        text = read(rel)
        for m in re.finditer(r"std::string_view to_string_view\(const (\w+) \w+\)\s*\{", text):
            body = text[m.end():text.index("\n}\n", m.end())]
            cases = re.findall(r'case (?:\w+::)?(\w+):\s*return "([^"]*)";', body)
            if cases:
                found[m.group(1)] = [{"name": n, "wire": w} for n, w in cases]
    return dict(sorted(found.items()))


def build():
    kinds = replay_kinds()
    packages = []
    for upper, camel, normalized, friendly in x_macro("src/proxy/GmcpMessage.h", "GMCP_MESSAGE_TYPE"):
        packages.append({"name": friendly, "id": upper, "replay": kinds.get(upper, "event")})
    modules = [{"name": friendly, "id": upper}
               for upper, _camel, _norm, friendly in x_macro("src/proxy/GmcpModule.h", "GMCP_MODULE_TYPE")]
    return {
        "generated_by": "tools/protocol_vocab.py",
        "packages": sorted(packages, key=lambda p: p["name"]),
        "modules": sorted(modules, key=lambda m: m["name"]),
        "enums": enums(),
        "xml_tags": xml_tags(),
    }


def main():
    text = json.dumps(build(), indent=1, sort_keys=False) + "\n"
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            sys.stderr.write("%s is stale: run tools/protocol_vocab.py\n" % OUT.relative_to(ROOT))
            return 1
        return 0
    OUT.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
