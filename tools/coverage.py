#!/usr/bin/env python3
"""Grep-based coverage: which exports are exercised by a real call?
usage: coverage.py <corpus_dir> [exports.txt ...]   (default: <corpus_dir>/tools/*_exports.txt)
An export counts as *called* when its name appears in the tests as U(Name), UIN("dll", Name), UN(Name),
t_proc("Name") or Name_pfn-style usage; as *mentioned* when it appears anywhere in tests/ (quoted tables
count as mentions only). Prints uncovered lists; use the numbers to drive gap-fill agents. It is a static
scan: macros that build names are invisible, so spot-check before declaring a name uncovered."""
import re, sys, glob, os
root = sys.argv[1]
lists = sys.argv[2:] or sorted(glob.glob(os.path.join(root, "tools", "*_exports.txt")))
src = ""
for f in glob.glob(os.path.join(root, "tests", "**", "*.[ch]"), recursive=True):
    src += open(f, errors="replace").read() + "\n"
names = sorted({l.strip() for p in lists for l in open(p) if l.strip()})
called_re = lambda n: re.compile(r'\b(?:U|UIN|UN|UA|US|UC|UK|UB|UNT)\s*\(\s*(?:"[^"]*"\s*,\s*)?' + re.escape(n) + r'\s*\)|t_proc\w*\s*\(\s*(?:"[^"]*"\s*,\s*)?"' + re.escape(n) + r'"')
unc, ment = [], []
for n in names:
    if called_re(n).search(src): continue
    unc.append(n)
    if not re.search(r'\b' + re.escape(n) + r'\b', src): ment.append(n)
print(f"{len(names)} exports; {len(names)-len(unc)} called; {len(unc)} not called; {len(ment)} never mentioned")
open(os.path.join(root, "tools", "uncovered.txt"), "w").write("\n".join(unc) + "\n")
print("never mentioned:", ", ".join(ment[:80]), "..." if len(ment) > 80 else "")
print("wrote tools/uncovered.txt")
