#!/usr/bin/env python3
"""Grep-based coverage: which exports are exercised by a real call?
usage: coverage.py <corpus_dir> [exports.txt ...]   (default: <corpus_dir>/tools/*_exports.txt)
An export counts as *called* when its name appears in the tests as U(Name), UIN("dll", Name), UN(Name),
t_proc("Name") or Name_pfn-style usage; as *mentioned* when it appears anywhere in tests/ (quoted tables
count as mentions only). Prints uncovered lists; use the numbers to drive gap-fill agents. It is a static
scan: macros that build names are invisible, so spot-check before declaring a name uncovered."""
import re, sys, glob, os


def read_tests(root):
    """Concatenated source of every tests/**/*.[ch]."""
    src = ""
    for f in sorted(glob.glob(os.path.join(root, "tests", "**", "*.[ch]"), recursive=True)):
        src += open(f, errors="replace").read() + "\n"
    return src


def export_names(lists):
    return sorted({l.strip() for p in lists for l in open(p) if l.strip()})


def called_re(n):
    """U(Name) / UIN("dll", Name) / UN(Name) ... or t_proc("Name") / t_proc_in(<any module expression>, "Name")."""
    q = re.escape(n)
    return re.compile(r'\b(?:U|UIN|UN|UA|US|UC|UK|UB|UNT)\s*\(\s*(?:"[^"]*"\s*,\s*)?' + q + r'\s*\)'
                      r'|t_proc\w*\s*\(\s*(?:(?:"[^"]*"|[^,"()]*(?:\[[^\]]*\])?[^,"()]*)\s*,\s*)?"' + q + r'"')


def direct_call_re(n):
    """The stricter form U(Name)(args): the resolved pointer is invoked on the spot."""
    q = re.escape(n)
    return re.compile(r'\b(?:U|UIN|UN|UA|US|UC|UK|UB|UNT)\s*\(\s*(?:"[^"]*"\s*,\s*)?' + q + r'\s*\)\s*\(')


def scan(root, lists):
    src = read_tests(root)
    names = export_names(lists)
    called, unc, ment = [], [], []
    for n in names:
        if called_re(n).search(src):
            called.append(n)
            continue
        unc.append(n)
        if not re.search(r'\b' + re.escape(n) + r'\b', src):
            ment.append(n)
    return names, called, unc, ment


def main():
    root = sys.argv[1]
    lists = sys.argv[2:] or sorted(glob.glob(os.path.join(root, "tools", "*_exports.txt")))
    names, called, unc, ment = scan(root, lists)
    print(f"{len(names)} exports; {len(called)} called; {len(unc)} not called; {len(ment)} never mentioned")
    open(os.path.join(root, "tools", "uncovered.txt"), "w").write("\n".join(unc) + "\n")
    print("never mentioned:", ", ".join(ment[:80]), "..." if len(ment) > 80 else "")
    print("wrote tools/uncovered.txt")


if __name__ == "__main__":
    main()
