#!/usr/bin/env python3
"""Help carve a module's export list into test areas.
usage: suggest_areas.py <exports.txt> [min_group=6]
Groups names by stripped A/W suffix and common CamelCase prefix so you can see natural families
(e.g. Reg*, Crypt*, Event*). Output is a starting point only: areas should follow *behaviour*
(what a developer would test together), not just name prefixes. Aim for 15-40 areas of 5-80 exports."""
import re, sys, collections
names = [l.strip() for l in open(sys.argv[1]) if l.strip()]
minn = int(sys.argv[2]) if len(sys.argv) > 2 else 6
base = sorted({re.sub(r'(Ex)?[AW]$', '', n) if re.search(r'[a-z][AW]$', n) else n for n in names})
groups = collections.defaultdict(list)
for n in base:
    m = re.match(r'([A-Z][a-z0-9]+(?:[A-Z][a-z0-9]+)?)', n)
    groups[m.group(1) if m else n].append(n)
big = {k: v for k, v in groups.items() if len(v) >= minn}
print(f"{len(names)} names, {len(base)} base names, {len(big)} families >= {minn}")
for k, v in sorted(big.items(), key=lambda kv: -len(kv[1])):
    print(f"{k:28} {len(v):4}  e.g. {', '.join(v[:4])}")
rest = [n for k, v in groups.items() if k not in big for n in v]
print(f"\n{len(rest)} base names in small families (assign by hand / gap-fill):")
print("  " + ", ".join(rest[:60]) + (" ..." if len(rest) > 60 else ""))
