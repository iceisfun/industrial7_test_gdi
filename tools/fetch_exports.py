#!/usr/bin/env python3
"""Fetch named exports of a module from the Windows 10 corpus API.
usage: fetch_exports.py <module> <out.txt> [--json out.json]
Writes one exported name per line (sorted, unique). The JSON dump keeps ordinals and forwarder targets
(useful for forwarder/export-table tests)."""
import json, sys, urllib.request, os
B = os.environ.get("CORPUS_API", "http://docker1.dev.moonlightcompanies.com:8765")
mod, out = sys.argv[1], sys.argv[2]
items, off = [], 0
while True:
    with urllib.request.urlopen(f"{B}/api/modules/{mod}/exports?limit=1000&offset={off}", timeout=60) as r:
        d = json.load(r)
    items += d.get("items", [])
    off += 1000
    if off >= d.get("total", 0) or not d.get("items"):
        break
names = sorted({i["name"] for i in items if i.get("name")})
open(out, "w").write("\n".join(names) + "\n")
if "--json" in sys.argv:
    json.dump(items, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=0)
fw = sum(1 for i in items if i.get("forwarder"))
print(f"{mod}: {len(items)} exports, {len(names)} named, {fw} forwarders, {len(items)-len(names)} ordinal-only", file=sys.stderr)
