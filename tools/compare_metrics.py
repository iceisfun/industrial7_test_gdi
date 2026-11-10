#!/usr/bin/env python3
"""compare_metrics.py - ranked diff of two GDI metric recordings (Win10 reference vs another environment).

usage: compare_metrics.py WIN10 OTHER [--md OUT.md] [--csv OUT.csv] [options]
       compare_metrics.py --selftest
Inputs (WIN10, OTHER): a rec file, a serial.log, or a directory (searched recursively for *.rec *.log *.txt).
Record format (see docs/COMPARE.md): lines "TAG [bare ids] k=v k=v ...", ASCII, stable. Anything else is ignored.
Exit status: 0 no out-of-tolerance difference, 1 differences, 2 usage/input error.  Stdlib only.
"""
import sys, os, re, csv, json, math, argparse, fnmatch, itertools, collections

DEFAULT_ID = {  # field names (lowercase) that identify a row instead of measuring something
    "face", "font", "facename", "dpi", "weight", "italic", "underline", "strike", "charset", "quality", "ch", "char",
    "code", "cp", "text", "str", "string", "name", "key", "id", "idx", "index", "case", "test", "dc", "mode", "stock",
    "pitch", "family", "style", "variant", "sample"}
DEFAULT_ID_PREFIX = ("req_", "in_", "lf")   # req_h=-11, in_face=..., lfHeight=...
DEFAULT_GROUP = ["face", "req_h", "dpi"]
HEIGHTISH = {"h", "a", "d", "il", "el", "height", "ascent", "descent", "tmheight", "tmascent", "tmdescent",
             "tminternalleading", "tmexternalleading", "cy", "ext19.cy"}
H_NAMES, A_NAMES, D_NAMES = {"h", "height", "tmheight"}, {"a", "ascent", "tmascent"}, {"d", "descent", "tmdescent"}
SKIP_TAGS = {"RUN", "RUNALL", "ENV", "DONE", "BLOB", "B64", "RC", "I7VM", "HELLO_BEGIN", "HELLO_END"}
KV_SPLIT = re.compile(r"\s+(?=[A-Za-z_][\w.\-]*=)")      # values may contain spaces: split only before "ident="
KV = re.compile(r"^([A-Za-z_][\w.\-]*)=(.*)$", re.S)
TAG = re.compile(r"^[A-Z][A-Z0-9_]*$")
COMPOSITE = re.compile(r"^-?\d+(x-?\d+)+$")


def parse_value(s):
    """-> (kind, value): kind in int/hex/float/str"""
    s = s.strip()
    if len(s) >= 2 and s[0] == s[-1] == '"':
        s = s[1:-1]
    if re.fullmatch(r"-?\d+", s):
        return "int", int(s)
    if re.fullmatch(r"-?0[xX][0-9a-fA-F]+", s):
        return "hex", int(s, 16)
    if re.fullmatch(r"-?\d+\.\d*|-?\.\d+|-?\d+(\.\d*)?[eE][-+]?\d+", s):
        return "float", float(s)
    return "str", s


class Row:
    __slots__ = ("tag", "ids", "fields", "key", "src")

    def __init__(self, tag, ids, fields, src):
        self.tag, self.ids, self.fields, self.src, self.key = tag, ids, fields, src, None


def is_id(name, id_set, force_value):
    n = name.lower()
    if n in force_value:
        return False
    return n in id_set or n.startswith(DEFAULT_ID_PREFIX)


def parse_text(text, src, id_set, force_value):
    rows = []
    for ln, raw in enumerate(text.splitlines(), 1):
        line = raw.strip("\r\n ").strip()
        if not line or "\x00" in line:
            continue
        # head = tag + bare tokens (up to first k=v)
        m = re.search(r"(?:^|\s)[A-Za-z_][\w.\-]*=", line)
        head = line[:m.start()].strip() if m else line
        rest = line[m.start():].strip() if m else ""
        toks = head.split()
        if not toks or not TAG.match(toks[0]) or toks[0] in SKIP_TAGS:
            continue
        tag, bare = toks[0], toks[1:]
        ids, fields = collections.OrderedDict(), collections.OrderedDict()
        if len(toks) == 2 and not rest and parse_value(toks[1])[0] != "str":     # "ENUMCOUNT 682"
            fields["value"] = parse_value(toks[1])
            bare = []
        for b in bare:
            ids["_%d" % len(ids)] = b
        for part in (KV_SPLIT.split(rest) if rest else []):
            km = KV.match(part.strip())
            if not km:
                continue
            name, val = km.group(1), km.group(2).strip()
            if val.startswith('"') and val.endswith('"') and len(val) >= 2:
                val = val[1:-1]
            if is_id(name, id_set, force_value):
                ids[name] = val
            elif COMPOSITE.match(val) and not re.fullmatch(r"-?0[xX][0-9a-fA-F]+", val):
                comps = val.split("x")
                labels = ["cx", "cy"] if len(comps) == 2 else [str(i) for i in range(len(comps))]
                for lab, c in zip(labels, comps):
                    fields["%s.%s" % (name, lab)] = ("int", int(c))
            else:
                fields[name] = parse_value(val)
        if not ids and not fields:
            continue
        rows.append(Row(tag, ids, fields, "%s:%d" % (src, ln)))
    return rows


def collect_files(path):
    if os.path.isdir(path):
        out = []
        for d, _, fs in os.walk(path):
            for f in sorted(fs):
                if f.endswith((".rec", ".log", ".txt")):
                    out.append(os.path.join(d, f))
        return sorted(out)
    return [path]


def load(paths, id_set, force_value, families):
    rows, files = [], []
    for p in paths:
        for f in collect_files(p):
            with open(f, "r", errors="replace") as fh:
                files.append(f)
                rows += parse_text(fh.read(), f, id_set, force_value)
    seen, out = collections.Counter(), collections.OrderedDict()
    for r in rows:
        if families and not any(fnmatch.fnmatch(r.tag, g) for g in families):
            continue
        base = r.tag + "".join("|%s=%s" % (k.lstrip("_") if k.startswith("_") else k, v) for k, v in r.ids.items())
        seen[base] += 1
        r.key = base if seen[base] == 1 else "%s#%d" % (base, seen[base])
        out[r.key] = r
    return out, files


class Finding:
    def __init__(self, **kw):
        self.__dict__.update(kw)


def weight_of(field, weights):
    f = field.lower()
    if f in weights:
        return weights[f]
    if f in HEIGHTISH:
        return 3.0
    if f.endswith(".cy") or f.endswith(".cx"):
        return 2.0
    if f in ("max", "tmmaxcharwidth", "maxw"):
        return 0.5
    return 1.0


def lookup(table, fam, field, default):
    for k in (fam + "." + field, field, fam + "." + field.lower(), field.lower()):
        if k in table:
            return table[k]
    return default


def compare(left, right, opt):
    findings, missing_r, missing_l, compared_rows = [], [], [], 0
    for key, a in left.items():
        b = right.get(key)
        if b is None:
            missing_r.append(a)
            continue
        compared_rows += 1
        for fld, (ka, va) in a.fields.items():
            if any(fnmatch.fnmatch(fld, g) or fnmatch.fnmatch(a.tag + "." + fld, g) for g in opt.ignore):
                continue
            w = weight_of(fld, opt.weight)
            ent = b.fields.get(fld)
            base = dict(family=a.tag, key=key, ids=a.ids, field=fld, left=va, weight=w)
            if ent is None:
                findings.append(Finding(kind="missing_field", right=None, diff=None, rel=None, mag=5.0,
                                        score=w * 5.0, ok=False, **base))
                continue
            kb, vb = ent
            if ka in ("int", "float", "hex") and kb in ("int", "float", "hex"):
                if ka == "hex" or kb == "hex":
                    diff = vb ^ va
                    mag = bin(diff).count("1")
                    kind, shown = "flags", diff
                else:
                    diff = vb - va
                    mag = abs(diff)
                    kind, shown = "num", diff
                rel = (abs(diff) / abs(va)) if va not in (0, 0.0) and kind == "num" else None
                atol = lookup(opt.tol, a.tag, fld, opt.tol_default)
                ok = (mag <= atol) or (kind == "num" and opt.rel_tol is not None and rel is not None and rel <= opt.rel_tol)
                if kind == "flags":
                    ok = (diff == 0) or (mag <= atol and opt.tol_default > 0 and False)
                if diff != 0:
                    findings.append(Finding(kind=kind, right=vb, diff=shown, rel=rel, mag=float(mag), score=w * mag,
                                            ok=ok, **base))
            else:
                if str(va) != str(vb):
                    findings.append(Finding(kind="text", right=vb, diff=None, rel=None, mag=10.0, score=w * 10.0,
                                            ok=False, **base))
        for fld in b.fields:
            if fld not in a.fields and not any(fnmatch.fnmatch(fld, g) for g in opt.ignore):
                pass  # extra field on the other side: not a metric regression
    for key, b in right.items():
        if key not in left:
            missing_l.append(b)
    return findings, missing_r, missing_l, compared_rows


def fmt(v):
    if isinstance(v, float):
        return "%.4g" % v
    return str(v)


def sgn(d):
    return ("+%s" % fmt(d)) if d > 0 else fmt(d)


# ---------------------------------------------------------------- root-cause hints
def hints(left, right, findings, missing_r, group_dims):
    out = []
    num = [f for f in findings if f.kind == "num"]
    byff = collections.defaultdict(list)
    for f in findings:
        byff[(f.family, f.field)].append(f)
    # universe of compared rows per (family, field)
    universe = collections.defaultdict(list)
    for key, a in left.items():
        b = right.get(key)
        if b is None:
            continue
        for fld, (ka, va) in a.fields.items():
            if fld in b.fields and ka in ("int", "float") and b.fields[fld][0] in ("int", "float"):
                universe[(a.tag, fld)].append((key, a, va, b.fields[fld][1]))
    for (fam, fld), fs in sorted(byff.items(), key=lambda kv: -sum(x.score for x in kv[1])):
        bad = [f for f in fs if not f.ok]
        if not bad:
            continue
        if bad[0].kind == "text":
            maps = collections.Counter((str(f.left), str(f.right)) for f in bad)
            tot = len(universe.get((fam, fld), [])) or None
            for (l, r), n in maps.most_common(4):
                out.append((sum(f.score for f in bad), "%s.%s: value %r on the reference appears as %r in %d row(s)" % (fam, fld, l, r, n)))
            continue
        if bad[0].kind == "missing_field":
            out.append((sum(f.score for f in bad), "%s.%s: field absent in the other recording for %d row(s)" % (fam, fld, len(bad))))
            continue
        if bad[0].kind == "flags":
            masks = collections.Counter(f.diff for f in bad)
            m, n = masks.most_common(1)[0]
            out.append((sum(f.score for f in bad), "%s.%s: flag bits differ, most common xor 0x%x in %d/%d rows" % (fam, fld, m, n, len(bad))))
            continue
        uni = universe.get((fam, fld), [])
        total = len(uni) or len(bad)
        diffs = [f.diff for f in bad]
        score = sum(f.score for f in bad)
        ds = collections.Counter(diffs)
        # 1. systematic constant offset
        if len(bad) / total >= 0.9 and len(ds) == 1 and total >= 2:
            out.append((score, "%s.%s: systematic offset %s in all %d rows (other = win10 %s)" % (fam, fld, sgn(diffs[0]), total, sgn(diffs[0]))))
            continue
        # 2. scale
        ratios = [f.right / f.left for f in bad if isinstance(f.left, (int, float)) and f.left and f.right is not None]
        if len(ratios) >= 5 and len(bad) / total >= 0.5:
            ratios.sort()
            med = ratios[len(ratios) // 2]
            near = sum(1 for r in ratios if abs(r - med) <= 0.03 * abs(med))
            if abs(med - 1) > 0.03 and near / len(ratios) >= 0.8:
                out.append((score, "%s.%s: proportional scale ~x%.3f in %d/%d rows (looks like a different size/dpi/algorithm, not rounding)" % (fam, fld, med, len(bad), total)))
                continue
        # 3. concentration by group dims (single dim, then pairs on the remainder)
        dims = [d for d in group_dims if any(d in left[k].ids for k, *_ in uni)]
        badkeys = {f.key: f for f in bad}
        remaining = set(badkeys)
        found = 0
        for size in (1, 2):
            for combo in itertools.combinations(dims, size):
                cells = collections.defaultdict(lambda: [0, 0])
                for k, a, va, vb in uni:
                    cell = tuple(a.ids.get(d) for d in combo)
                    cells[cell][0] += 1
                    if k in remaining:
                        cells[cell][1] += 1
                for cell, (n, m) in sorted(cells.items(), key=lambda kv: -kv[1][1]):
                    if m >= 2 and m / n >= 0.8 and n < total and m >= 0.3 * len(remaining):
                        sel = [badkeys[k].diff for k in remaining if tuple(left[k].ids.get(d) for d in combo) == cell]
                        sc = collections.Counter(sel)
                        desc = ", ".join("%s x%d" % (sgn(d), c) for d, c in sc.most_common(3))
                        lab = " and ".join("%s=%s" % (d, c) for d, c in zip(combo, cell))
                        out.append((score * m / len(bad), "%s.%s: off in %d/%d rows with %s (diffs %s); other cells match more" % (fam, fld, m, n, lab, desc)))
                        remaining -= {k for k in remaining if tuple(left[k].ids.get(d) for d in combo) == cell}
                        found += 1
                        if found >= 3:
                            break
                if found >= 3 or not remaining:
                    break
            if found >= 3 or not remaining or found:
                break
        if found == 0:
            if len(bad) <= 3:
                out.append((score, "%s.%s: isolated, only %s differ (%s)" % (fam, fld, ", ".join("`%s` %s" % (f.key, sgn(f.diff)) for f in bad), "all other rows match")))
                continue
            lo, hi = min(diffs), max(diffs)
            sc = ", ".join("%s x%d" % (sgn(d), c) for d, c in ds.most_common(3))
            out.append((score, "%s.%s: scattered, %d/%d rows differ (range %s..%s; %s); no single face/height/dpi explains it" % (fam, fld, len(bad), total, sgn(lo), sgn(hi), sc)))
    # 4. co-moving signatures
    sig = collections.defaultdict(lambda: collections.defaultdict(dict))
    for f in num:
        if not f.ok:
            sig[f.family][f.key][f.field] = f.diff
    for fam, keys in sig.items():
        c = collections.Counter(tuple(sorted(d.items())) for d in keys.values() if len(d) > 1)
        for s, n in c.most_common(2):
            if n >= 3:
                out.append((n * 2.0, "%s: %d rows share the diff signature [%s] - probably one cause (the largest-weight field drives the rest)" % (fam, n, ", ".join("%s %s" % (k, sgn(v)) for k, v in s))))
        rel = 0
        for d in keys.values():
            h = [v for k, v in d.items() if k.lower() in H_NAMES]
            a = [v for k, v in d.items() if k.lower() in A_NAMES]
            dd = [v for k, v in d.items() if k.lower() in D_NAMES]
            if h and (a or dd) and h[0] == (a[0] if a else 0) + (dd[0] if dd else 0):
                rel += 1
        if rel >= 3:
            out.append((rel * 2.0, "%s: in %d rows the height diff equals ascent diff + descent diff (cell height follows its parts; fix ascent/descent first)" % (fam, rel)))
    # 5. missing rows
    if missing_r:
        fams = collections.Counter(r.tag for r in missing_r)
        for fam, n in fams.most_common(5):
            tot = sum(1 for r in left.values() if r.tag == fam)
            out.append((10.0 * n, "%s: %d of %d reference rows have no counterpart (probe not run, crashed, or key format differs)" % (fam, n, tot)))
            for d in group_dims:
                vs = collections.Counter(r.ids.get(d) for r in missing_r if r.tag == fam and d in r.ids)
                for v, c in vs.most_common(1):
                    alln = sum(1 for r in left.values() if r.tag == fam and r.ids.get(d) == v)
                    if c == alln and c >= 2 and alln < tot:
                        out.append((10.0 * c, "%s: every row with %s=%s is missing (%d rows)" % (fam, d, v, c)))
    out.sort(key=lambda t: -t[0])
    seen, res = set(), []
    for _, t in out:
        if t not in seen:
            seen.add(t)
            res.append(t)
    return res


# ---------------------------------------------------------------- reporting
def group_stats(findings, dim, left):
    g = collections.OrderedDict()
    for f in findings:
        if dim in f.ids:
            e = g.setdefault(f.ids[dim], [0, 0.0, 0.0, set()])
            e[0] += 1
            e[1] += f.score
            e[2] = max(e[2], f.mag)
            e[3].add(f.key)
    return g


def md_table(head, rows):
    s = "| " + " | ".join(head) + " |\n|" + "|".join("---" for _ in head) + "|\n"
    for r in rows:
        s += "| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |\n"
    return s


def write_md(path, args, left, right, lfiles, rfiles, findings, mr, ml, nrows, hs, opt):
    bad = [f for f in findings if not f.ok]
    ok_diffs = [f for f in findings if f.ok]
    bad.sort(key=lambda f: (-f.score, f.key, f.field))
    keys_bad = {f.key for f in bad}
    L = []
    L.append("# Metric comparison: %s vs %s\n" % (opt.left_name, opt.right_name))
    L.append("Reference (%s): %d rows from %d file(s). Other (%s): %d rows from %d file(s).  " % (opt.left_name, len(left), len(lfiles), opt.right_name, len(right), len(rfiles)))
    L.append("Compared rows (key present on both sides): %d. Diff sign = %s minus %s.\n" % (nrows, opt.right_name, opt.left_name))
    L.append("Tolerance: abs=%s%s%s; ignored: %s.\n" % (opt.tol_default, ", rel=%s" % opt.rel_tol if opt.rel_tol is not None else "",
                                                       ", per-field " + str(opt.tol) if opt.tol else "", ", ".join(opt.ignore) or "none"))
    verdict = "MATCH" if not bad and not mr else "DIFFERENCES"
    L.append("## Summary\n")
    L.append("- Verdict: **%s**" % verdict)
    L.append("- Out-of-tolerance field diffs: %d in %d rows (of %d compared)" % (len(bad), len(keys_bad), nrows))
    L.append("- Within-tolerance field diffs (not scored): %d" % len(ok_diffs))
    L.append("- Reference rows missing from %s: %d; extra rows only in %s: %d\n" % (opt.right_name, len(mr), opt.right_name, len(ml)))
    L.append("## Root-cause hints\n")
    L.append("Heuristic reading of the diff pattern (INFER, not measured): cause guesses ranked by total score.\n")
    if hs:
        for h in hs[:25]:
            L.append("- " + h)
    else:
        L.append("- none (no out-of-tolerance differences)")
    L.append("")
    # family summary
    L.append("## Per probe family\n")
    fam = collections.OrderedDict()
    for r in left.values():
        fam.setdefault(r.tag, [0, 0, 0, 0.0, 0.0])[0] += 1
    for r in mr:
        fam[r.tag][2] += 1
    for f in bad:
        e = fam[f.family]
        e[3] += f.score
        e[4] = max(e[4], f.mag)
    badrows = collections.Counter()
    for k in keys_bad:
        badrows[left[k].tag] += 1
    rows = [(t, e[0], badrows[t], e[2], "%.1f" % e[3], fmt(e[4])) for t, e in sorted(fam.items(), key=lambda kv: -kv[1][3])]
    L.append(md_table(["family", "ref rows", "rows differing", "rows missing", "score", "max abs diff"], rows))
    # per-field stats
    L.append("## Per field (out of tolerance)\n")
    st = collections.OrderedDict()
    for f in bad:
        st.setdefault((f.family, f.field), []).append(f)
    rows = []
    for (fm, fl), fs in sorted(st.items(), key=lambda kv: -sum(x.score for x in kv[1])):
        ds = [x.diff for x in fs if isinstance(x.diff, (int, float)) and x.kind == "num"]
        rows.append((fm, fl, len(fs), sgn(min(ds)) if ds else "-", sgn(max(ds)) if ds else "-",
                     sgn(round(sum(ds) / len(ds), 3)) if ds else "-", "%.1f" % sum(x.score for x in fs)))
    L.append(md_table(["family", "field", "rows", "min diff", "max diff", "mean diff", "score"], rows) if rows else "none\n")
    # grouping
    for dim in opt.group_by:
        g = group_stats(bad, dim, left)
        if not g:
            continue
        totals = collections.Counter(r.ids[dim] for r in left.values() if dim in r.ids)
        L.append("## Grouped by %s\n" % dim)
        rows = [(v, totals[v], len(e[3]), e[0], "%.1f" % e[1], fmt(e[2])) for v, e in sorted(g.items(), key=lambda kv: -kv[1][1])]
        L.append(md_table([dim, "ref rows", "rows differing", "field diffs", "score", "max abs diff"], rows))
    # ranked findings
    L.append("## Top %d ranked differences\n" % min(opt.top, len(bad)))
    rows = []
    for f in bad[:opt.top]:
        rows.append((f.key, f.field, fmt(f.left), fmt(f.right) if f.right is not None else "(absent)",
                     (sgn(f.diff) if f.kind == "num" else (("xor 0x%x" % f.diff) if f.kind == "flags" else f.kind)),
                     ("%.1f%%" % (100 * f.rel)) if f.rel is not None else "", "%.1f" % f.score))
    L.append(md_table(["key", "field", opt.left_name, opt.right_name, "diff", "rel", "score"], rows) if rows else "none\n")
    if len(bad) > opt.top:
        L.append("... %d more in the CSV.\n" % (len(bad) - opt.top))
    L.append("## Missing / extra rows\n")
    if mr:
        L.append("Reference rows not in %s (%d):\n" % (opt.right_name, len(mr)))
        for r in mr[:opt.top]:
            L.append("- `%s`" % r.key)
        if len(mr) > opt.top:
            L.append("- ... %d more" % (len(mr) - opt.top))
    if ml:
        L.append("\nRows only in %s (%d):\n" % (opt.right_name, len(ml)))
        for r in ml[:opt.top]:
            L.append("- `%s`" % r.key)
        if len(ml) > opt.top:
            L.append("- ... %d more" % (len(ml) - opt.top))
    if not mr and not ml:
        L.append("none")
    open(path, "w").write("\n".join(L) + "\n")


def write_csv(path, findings, mr, ml, opt):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["family", "key", "face", "req_h", "dpi", "field", opt.left_name, opt.right_name, "diff", "rel", "kind", "in_tolerance", "score"])
        for f in sorted(findings, key=lambda f: (f.ok, -f.score, f.key, f.field)):
            w.writerow([f.family, f.key, f.ids.get("face", ""), f.ids.get("req_h", ""), f.ids.get("dpi", ""), f.field,
                        f.left, "" if f.right is None else f.right, "" if f.diff is None else f.diff,
                        "" if f.rel is None else "%.4f" % f.rel, f.kind, int(f.ok), "%.2f" % f.score])
        for side, lst in (("missing_in_" + opt.right_name, mr), ("extra_in_" + opt.right_name, ml)):
            for r in lst:
                w.writerow([r.tag, r.key, r.ids.get("face", ""), r.ids.get("req_h", ""), r.ids.get("dpi", ""), "", "", "", "", "", side, 0, "10.00"])


# ---------------------------------------------------------------- driver
def kv_list(items, conv=float):
    d = {}
    for it in items or []:
        k, _, v = it.partition("=")
        d[k.lower() if "." not in k else k] = conv(v)
    return d


def build_opts(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("left", nargs="?")
    ap.add_argument("right", nargs="?")
    ap.add_argument("--md")
    ap.add_argument("--csv")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--tol", action="append", default=[], metavar="[FAM.]FIELD=N|N",
                    help="absolute tolerance; bare N sets the default; repeatable")
    ap.add_argument("--rel-tol", type=float, default=None, help="also accept |diff|/|ref| <= R")
    ap.add_argument("--ignore", action="append", default=[], metavar="GLOB", help="field (or FAM.field) glob to skip")
    ap.add_argument("--weight", action="append", default=[], metavar="FIELD=W")
    ap.add_argument("--family", action="append", default=[], metavar="GLOB")
    ap.add_argument("--group-by", default=",".join(DEFAULT_GROUP))
    ap.add_argument("--id", action="append", default=[], help="extra identity field name")
    ap.add_argument("--value", action="append", default=[], help="force this field name to be a measured value")
    ap.add_argument("--left-name", default="win10")
    ap.add_argument("--right-name", default="other")
    ap.add_argument("--allow-extra", action="store_true", help="rows only in OTHER do not fail the exit status")
    ap.add_argument("--allow-missing", action="store_true", help="reference rows absent in OTHER do not fail the exit status")
    ap.add_argument("--selftest", action="store_true")
    o = ap.parse_args(argv)
    o.tol_default = 0.0
    tol = {}
    for t in o.tol:
        if "=" in t:
            k, _, v = t.partition("=")
            tol[k] = float(v)
        else:
            o.tol_default = float(t)
    o.tol = tol
    o.weight = kv_list(o.weight)
    o.group_by = [g for g in o.group_by.split(",") if g]
    o.id_set = DEFAULT_ID | {x.lower() for x in o.id}
    o.force_value = {x.lower() for x in o.value}
    return o


def run(o, quiet=False):
    left, lf = load([o.left], o.id_set, o.force_value, o.family)
    right, rf = load([o.right], o.id_set, o.force_value, o.family)
    if not left:
        raise SystemExit("error: no records parsed from %s" % o.left)
    findings, mr, ml, nrows = compare(left, right, o)
    hs = [h for h in hints(left, right, findings, mr, o.group_by)]
    if o.md:
        write_md(o.md, o, left, right, lf, rf, findings, mr, ml, nrows, hs, o)
    if o.csv:
        write_csv(o.csv, findings, mr, ml, o)
    bad = [f for f in findings if not f.ok]
    fail = bool(bad) or (mr and not o.allow_missing) or (ml and not o.allow_extra)
    res = dict(rows_left=len(left), rows_right=len(right), compared=nrows, findings=findings, bad=bad, missing=mr, extra=ml, hints=hs, fail=bool(fail))
    if not quiet:
        print("%s: %d ref rows, %d other rows, %d compared; %d out-of-tolerance diffs, %d missing, %d extra; %d hints" %
              ("DIFFERENCES" if fail else "MATCH", len(left), len(right), nrows, len(bad), len(mr), len(ml), len(hs)))
        for h in hs[:8]:
            print("  hint: " + h)
    return res


def selftest():
    here = os.path.dirname(os.path.abspath(__file__))
    fx = os.path.join(here, "..", "tests", "compare_fixtures")
    spec = json.load(open(os.path.join(fx, "cases.json")))
    ref = os.path.join(fx, spec["reference"])
    fails = 0
    for c in spec["cases"]:
        o = build_opts([ref, os.path.join(fx, c["file"])] + c.get("args", []))
        res = run(o, quiet=True)
        errs = []
        e = c["expect"]
        if "fail" in e and res["fail"] != e["fail"]:
            errs.append("fail=%s want %s" % (res["fail"], e["fail"]))
        for k, key in (("bad", "bad"), ("missing", "missing"), ("extra", "extra")):
            if k in e and len(res[key]) != e[k]:
                errs.append("%s=%d want %d" % (k, len(res[key]), e[k]))
        txt = "\n".join(res["hints"])
        for sub in e.get("hints", []):
            if sub not in txt:
                errs.append("hint %r not found in:\n    %s" % (sub, txt.replace("\n", "\n    ")))
        print("%s %s%s" % ("FAIL" if errs else "ok  ", c["name"], "".join("\n    " + x for x in errs)))
        fails += bool(errs)
    print("SELFTEST %s (%d cases)" % ("FAILED" if fails else "PASSED", len(spec["cases"])))
    return 1 if fails else 0


def main(argv):
    o = build_opts(argv)
    if o.selftest:
        return selftest()
    if not (o.left and o.right):
        print("usage: compare_metrics.py WIN10 OTHER [--md F] [--csv F]  (see --help)", file=sys.stderr)
        return 2
    for p in (o.left, o.right):
        if not os.path.exists(p):
            print("error: no such input %s" % p, file=sys.stderr)
            return 2
    return 1 if run(o)["fail"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
