#!/usr/bin/env python3
"""Split a vm_test.sh serial.log into per-area logs and a verdict per area.
usage: vm_test_split.py SERIAL OUTDIR ELAPSED_S AREA...
Verdict per area: PASS only when the area's vmrunner step ended (I7P END) with rc 0, no timeout, a SUMMARY line with
tests>0 failed=0 and no FAIL line; anything else (including a missing result) is FAIL. Exit 0 all PASS, 1 otherwise."""
import re, sys, os
serial, outdir, elapsed, areas = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
lines = [l.rstrip("\r") for l in open(serial, "rb").read().decode("latin-1").split("\n")]
BEGIN = re.compile(r"^I7P BEGIN name=(\S+) ")
END = re.compile(r"^I7P END name=(\S+) seq=\d+ rc=(-?\d+) ms=(\d+) timeout=(\d)")
ERR = re.compile(r"^I7P ERR name=(\S+) \| (.*)$")
cur, out, err, end = None, {}, {}, {}
for l in lines:
    m = BEGIN.match(l)
    if m: cur = m.group(1); out.setdefault(cur, []); continue
    m = END.match(l)
    if m: end[m.group(1)] = (int(m.group(2)), int(m.group(3)), m.group(4) == "1"); cur = None; continue
    m = ERR.match(l)
    if m: err.setdefault(m.group(1), []).append(m.group(2)); continue
    if l.startswith(("I7P ", "I7VM")): continue
    if cur is not None and l != "": out[cur].append(l)
guest_end = any(l.startswith("I7VMRUN END") for l in lines)
rows, bad = [], 0
tot = dict(tests=0, failed=0, skipped=0, cp=0, cf=0)
for a in areas:
    body = out.get(a)
    why = []
    s = None
    if body is None: why.append("no result (step never started)")
    else:
        open(os.path.join(outdir, a + ".log"), "w").write("\n".join(body) + ("\n" if body else ""))
        if a in err: open(os.path.join(outdir, a + ".err"), "w").write("\n".join(err[a]) + "\n")
        e = end.get(a)
        if e is None: why.append("no END marker (guest died or run cut short)")
        else:
            if e[2]: why.append("timeout")
            if e[0] != 0: why.append("exit code %d (0x%08X)" % (e[0], e[0] & 0xFFFFFFFF))
        for l in body:
            m = re.match(r"^SUMMARY tests=(\d+) failed=(\d+) skipped=(\d+) checks_pass=(\d+) checks_fail=(\d+)", l)
            if m: s = tuple(map(int, m.groups()))
        if s is None: why.append("no SUMMARY line")
        else:
            if s[0] == 0: why.append("0 tests ran")
            if s[1]: why.append("%d test(s) failed" % s[1])
            for i, k in enumerate(("tests", "failed", "skipped", "cp", "cf")): tot[k] += s[i]
        if any(l.startswith("  FAIL") or l.startswith("FAIL") for l in body) and not any("failed" in w for w in why):
            why.append("FAIL line present")
        if any(l.startswith("FATAL") for l in body): why.append("FATAL: " + [l for l in body if l.startswith("FATAL")][0])
    v = "FAIL" if why else "PASS"
    bad += bool(why)
    rc, ms = (end[a][0], end[a][1]) if a in end else ("-", "-")
    rows.append((a, v, s, rc, ms, why, [l for l in (body or []) if l.startswith("  SKIP")], [l for l in (body or []) if l.startswith("  NOTE")]))
if not guest_end: print("warning: no 'I7VMRUN END' in serial.log (run cut short)", file=sys.stderr)
res = []
for a, v, s, rc, ms, why, sk, nt in rows:
    st = "tests=%d failed=%d skipped=%d checks=%d/%d" % (s[0], s[1], s[2], s[3], s[3] + s[4]) if s else "no summary"
    res.append("%-4s %-20s rc=%s ms=%s %s%s" % (v, a, rc, ms, st, ("  [" + "; ".join(why) + "]") if why else ""))
res.append("TOTAL areas=%d failed_areas=%d tests=%d failed=%d skipped=%d elapsed_s=%s" % (len(areas), bad, tot["tests"], tot["failed"], tot["skipped"], elapsed))
txt = "\n".join(res) + "\n"
open(os.path.join(outdir, "summary.txt"), "w").write(txt)
open(os.path.join(outdir, "results.tsv"), "w").write("".join("%s\t%s\t%s\t%s\n" % (a, v, rc, ms) for a, v, s, rc, ms, *_ in rows))
print(txt, end="")
for a, v, s, rc, ms, why, sk, nt in rows:
    for l in sk: print("%s: %s" % (a, l.strip()))
    if nt: print("%s: %d NOTE line(s) in %s.log" % (a, len(nt), a))
sys.exit(1 if bad else 0)
