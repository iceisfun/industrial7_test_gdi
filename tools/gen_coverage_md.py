#!/usr/bin/env python3
"""Regenerate COVERAGE.md.

usage: gen_coverage_md.py [--root DIR] [--out FILE] [--date YYYY-MM-DD] [--guest-build TEXT]

Inputs (all in the repository):
  tools/gdi32_exports.txt, tools/gdi32full_exports.txt   export lists (one name per line)
  tests/**/*.[ch]                                        scanned with tools/coverage.py (static, name based)
  probes/**/*.c(pp), common/rec.c                        scanned for calls by the recorders
  tools/coverage_observe_only.txt                        hand-maintained: called but only observed (name<TAB>where/why)
  tools/coverage_extra_behavioural.txt                   optional, hand-maintained: behavioural names the scan cannot see
                                                          (name<TAB>test location)
  tools/uncovered_justified.txt                          name<TAB>reason for names no test calls

Classification, first match wins, every export lands in exactly one class:
  1. observe-only            listed in coverage_observe_only.txt and called by a test
  2. behavioural             called by a test (static scan: U(Name), UN(Name), UIN("dll", Name), t_proc("Name"),
                             t_proc_in(<module>, "Name")) and not resolve-only, or listed in coverage_extra_behavioural.txt
  3. resolve-only            every scan hit is an existence check (REQUIRE(U(Name)), if(!U(Name)), U(Name) == NULL)
  4. probe/recorder only     no test calls it, a recorder under probes/ calls it
  5. delegated               no test or probe calls it; the reason in uncovered_justified.txt starts with "delegated"
  6. uncovered with reason   no test or probe calls it; it has a reason in uncovered_justified.txt
  7. uncovered, no reason    none of the above
The scan cannot tell whether a test checks the result it gets: "behavioural" means "a test in this repository calls it";
the per-area EVIDENCE.md files say what is asserted. Macros that build names are invisible to the scan."""
import argparse, datetime, glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coverage as cov


def read_tsv(path):
    out = {}
    if not os.path.exists(path):
        return out
    for l in open(path, errors="replace"):
        l = l.rstrip("\n")
        if not l.strip() or l.startswith("#"):
            continue
        k, _, v = l.partition("\t")
        out[k.strip()] = v.strip()
    return out


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    return re.sub(r"//[^\n]*", " ", s)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    ap.add_argument("--out", default=None)
    ap.add_argument("--date", default=datetime.date.today().isoformat())
    ap.add_argument("--guest-build", default="Windows 10 22H2 build 19045.2965 (oracle guest of the recordings and tests)")
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    out = a.out or os.path.join(root, "COVERAGE.md")
    lists = [os.path.join(root, "tools", "gdi32_exports.txt"), os.path.join(root, "tools", "gdi32full_exports.txt")]
    names, called, unc, ment = cov.scan(root, lists)
    called_set = set(called)
    src = cov.read_tests(root)
    mods = {}
    for lp, m in zip(lists, ("gdi32", "gdi32full")):
        for l in open(lp):
            if l.strip():
                mods.setdefault(l.strip(), []).append(m)

    observe = read_tsv(os.path.join(root, "tools", "coverage_observe_only.txt"))
    extra = read_tsv(os.path.join(root, "tools", "coverage_extra_behavioural.txt"))
    just = read_tsv(os.path.join(root, "tools", "uncovered_justified.txt"))

    # per-area sources, to say which area calls a name
    area_src = {}
    for f in sorted(glob.glob(os.path.join(root, "tests", "*", "*.[ch]"))):
        area_src.setdefault(os.path.basename(os.path.dirname(f)), "")
        area_src[os.path.basename(os.path.dirname(f))] += open(f, errors="replace").read() + "\n"

    def areas_of(n):
        r = cov.called_re(n)
        return [ar for ar, s in area_src.items() if r.search(s)]

    def resolve_only(n):
        q = re.escape(n)
        form = r'(?:\b(?:U|UIN|UN|UA|US|UC|UK|UB|UNT)\s*\(\s*(?:"[^"]*"\s*,\s*)?' + q + r'\s*\)|t_proc\w*\s*\(\s*(?:[^()]*,\s*)?"' + q + r'")'
        s = re.sub(r'REQUIRE\s*\(\s*' + form + r'\s*\)', " ", src)
        s = re.sub(r'if\s*\(\s*!\s*' + form + r'\s*\)', " ", s)
        s = re.sub(form + r'\s*(?:==|!=)\s*(?:NULL|0)\b', " ", s)
        return not cov.called_re(n).search(s)

    # probes
    psrc = ""
    for f in sorted(glob.glob(os.path.join(root, "probes", "**", "*.c*"), recursive=True)) + [os.path.join(root, "common", "rec.c")]:
        if os.path.exists(f):
            psrc += strip_comments(open(f, errors="replace").read()) + "\n"

    def probe_users(n):
        r = re.compile(r'\b' + re.escape(n) + r'\s*\(|"' + re.escape(n) + r'"')
        if not r.search(psrc):
            return []
        us = []
        for f in sorted(glob.glob(os.path.join(root, "probes", "*", "*.c*"))):
            if r.search(strip_comments(open(f, errors="replace").read())):
                us.append(os.path.basename(os.path.dirname(f)))
        return sorted(set(us)) or ["common/rec.c"]

    cls = {k: [] for k in ("observe", "behav", "resolve", "probe", "deleg", "reason", "noreason")}
    info = {}
    for n in names:
        if n in called_set or n in extra:
            if n in observe and n in called_set:
                cls["observe"].append(n); info[n] = observe[n]
            elif n in called_set and resolve_only(n) and n not in extra:
                cls["resolve"].append(n); info[n] = ", ".join(areas_of(n))
            else:
                cls["behav"].append(n); info[n] = extra.get(n) or ", ".join(areas_of(n))
            continue
        pu = probe_users(n)
        if pu:
            cls["probe"].append(n); info[n] = ", ".join(pu)
        elif n in just and just[n].startswith("delegated"):
            cls["deleg"].append(n); info[n] = just[n]
        elif n in just:
            cls["reason"].append(n); info[n] = just[n]
        else:
            cls["noreason"].append(n)
    stale = sorted(n for n in just if n in called_set)
    unknown = sorted(n for n in just if n not in mods)

    total = len(names)
    L = []
    w = L.append
    cmd = "python3 tools/gen_coverage_md.py"
    w("# Coverage of the gdi32 / gdi32full export surface\n")
    w(f"Generated {a.date} by `{cmd}` from the repository at that date. Do not edit by hand.\n")
    w(f"- Oracle: {a.guest_build}. The scan itself is static and does not run anything on the guest; whether the tests pass there is stated in `STATE.md` and in each area's `EVIDENCE.md`.")
    w("- Export lists: `tools/gdi32_exports.txt`, `tools/gdi32full_exports.txt` (distinct names; a name exported by both counts once).")
    w("- Scan: `python3 tools/coverage.py . tools/gdi32_exports.txt tools/gdi32full_exports.txt` (also writes `tools/uncovered.txt`). "
      "It prints `%d exports; %d called; %d not called; %d never mentioned`; this file was generated from the same scan." % (total, len(called), len(unc), len(ment)))
    w("- The scan is static and name based. It sees `U(Name)`, `UN(Name)`, `UIN(\"dll\", Name)`, `t_proc(\"Name\")` and `t_proc_in(module, \"Name\")`; macros that build names are invisible. "
      "It cannot tell whether a test checks the result it gets: \"behavioural\" below means a test in this repository calls the export, and the area's `EVIDENCE.md` says what is asserted.\n")
    w("## Summary\n")
    w("| Class | Exports |")
    w("|-------|--------:|")
    rows = [("behavioural (called by a test)", "behav"), ("observe-only (called by a test, nothing asserted about the export)", "observe"),
            ("resolve-only (a test only checks that it exists)", "resolve"), ("probe / recorder only", "probe"),
            ("delegated to the sibling corpus", "deleg"), ("uncovered, with a recorded reason", "reason"), ("uncovered, no reason recorded", "noreason")]
    for t, k in rows:
        w(f"| {t} | {len(cls[k])} |")
    w(f"| **total distinct names** | **{total}** |\n")
    w("Probe/recorder-only means a recorder under `probes/` calls the export and no conformance test does; the data lives in `data/win10/`.\n")

    def table(title, key, head, descr=None):
        w(f"## {title} ({len(cls[key])})\n")
        if descr:
            w(descr + "\n")
        if not cls[key]:
            w("None.\n"); return
        w(f"| Export | Module | {head} |")
        w("|--------|--------|" + "-" * len(head) + "|")
        for n in cls[key]:
            w(f"| `{n}` | {'+'.join(mods[n])} | {info.get(n, '')} |")
        w("")

    table("Behavioural", "behav", "Test area(s)", "Called by at least one conformance test. Which results are asserted is in `tests/<area>/EVIDENCE.md` (new areas) or in the test source (older areas).")
    table("Observe-only", "observe", "Where and why", "Called by a test that reports what happens (`NOTE`) but asserts nothing about the contract of the export; hand-maintained in `tools/coverage_observe_only.txt`.")
    table("Resolve-only", "resolve", "Test area(s)", "Only an existence check was found; this does not count as coverage.")
    table("Probe / recorder only", "probe", "Probe(s)", "Called from a recorder, not from a conformance test.")

    w(f"## Delegated to the sibling corpus ({len(cls['deleg'])})\n")
    w("Non-text GDI breadth (DCs, bitmaps, regions, paths, metafiles, palettes). Not duplicated here; names are listed, the reason is the same for all.\n")
    w(", ".join(f"`{n}`" for n in cls["deleg"]) + "\n" if cls["deleg"] else "None.\n")

    w(f"## Uncovered, with a reason ({len(cls['reason'])})\n")
    w("Reasons are recorded in `tools/uncovered_justified.txt`; names grouped by reason.\n")
    groups = {}
    for n in cls["reason"]:
        groups.setdefault(just[n], []).append(n)
    for r in sorted(groups, key=lambda r: (-len(groups[r]), r)):
        w(f"### {r} ({len(groups[r])})\n")
        w(", ".join(f"`{n}`" for n in groups[r]) + "\n")

    w(f"## Uncovered, no reason recorded ({len(cls['noreason'])})\n")
    w(", ".join(f"`{n}`" for n in cls["noreason"]) + "\n" if cls["noreason"] else "None.\n")

    w("## Consistency of the hand-maintained files\n")
    w(f"- Entries of `tools/uncovered_justified.txt` for names that a test now calls (stale, remove them): {', '.join(stale) if stale else 'none'}.")
    w(f"- Entries of `tools/uncovered_justified.txt` that are not in the export lists: {', '.join(unknown) if unknown else 'none'}.")
    bad = [n for n in observe if n not in called_set]
    w(f"- Entries of `tools/coverage_observe_only.txt` that no test calls: {', '.join(bad) if bad else 'none'}.")
    open(out, "w").write("\n".join(L) + "\n")
    print(f"wrote {out}: " + "; ".join(f"{k}={len(v)}" for k, v in cls.items()) + f"; total={total}")


if __name__ == "__main__":
    main()
