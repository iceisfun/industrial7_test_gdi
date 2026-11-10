#!/usr/bin/env python3
"""Regenerate tests/compare_fixtures/*.rec by mutating the REAL Win10 recording win10_tm_hello.rec
(recorded on Win10 22H2 19045.2965 by tools/probes/tm_hello.c). Deterministic. Also writes cases.json."""
import os, re, json
here = os.path.dirname(os.path.abspath(__file__))
fx = os.path.join(here, "..", "tests", "compare_fixtures")
ref = open(os.path.join(fx, "win10_tm_hello.rec")).read().splitlines()


def mutate(fn, name):
    out = []
    for ln in ref:
        if ln.startswith("TM "):
            d = dict(re.findall(r"(\w+)=((?:(?!\s\w+=).)*)", ln))
            r = fn(d)
            if r is None:
                continue
            ln = "TM " + " ".join("%s=%s" % kv for kv in r.items())
        out.append(ln)
    open(os.path.join(fx, name), "w").write("\n".join(out) + "\n")


def num(d, k, delta):
    d[k] = str(int(d[k]) + delta)


def ascent_m11(d):            # Win10 + 1 ascent (and so height) for every -11 row
    if d["req_h"] == "-11":
        num(d, "a", 1); num(d, "h", 1)
    return d


def segoe12(d):               # the Wine-style Segoe UI -12 result: h 15->14, il 3->1, ext cy follows
    if d["face"] == "Segoe UI" and d["req_h"] == "-12":
        d["h"] = "14"; d["a"] = "11"; d["il"] = "1"; d["ext19"] = re.sub(r"x\d+$", "x14", d["ext19"])
    return d


def face_subst(d):
    if d["face"] == "Segoe UI":
        d["got"] = "DejaVu Sans"
    return d


def maxw_half(d):             # tmMaxCharWidth reported as ~half (FreeType-style glyph max)
    d["max"] = str(max(1, int(d["max"]) // 2))
    return d


def tiny(d):                  # +-1 jitter on width of W only
    num(d, "w_W", 1)
    return d


def drop_courier(d):
    return None if d["face"] == "Courier New" else d


def scale_ext(d):             # all text extents 25% wider
    w, h = d["ext19"].split("x")
    d["ext19"] = "%dx%s" % (round(int(w) * 1.25), h)
    return d


def flags(d):
    if d["face"] == "Tahoma":
        d["pf"] = "0x22"
    return d


for fn, name in [(ascent_m11, "ascent_m11.rec"), (segoe12, "segoe12.rec"), (face_subst, "face_subst.rec"), (maxw_half, "maxw_half.rec"),
                 (tiny, "jitter_w.rec"), (drop_courier, "no_courier.rec"), (scale_ext, "wide_ext.rec"), (flags, "pitchfamily.rec")]:
    mutate(fn, name)

cases = [
    {"name": "identical", "file": "win10_tm_hello.rec", "expect": {"fail": False, "bad": 0, "missing": 0, "extra": 0}},
    {"name": "ascent +1 at -11 (h follows)", "file": "ascent_m11.rec",
     "expect": {"fail": True, "bad": 18, "hints": ["req_h=-11", "TM.a:", "height diff equals ascent diff"]}},
    {"name": "ascent +1 within tolerance 1", "file": "ascent_m11.rec", "args": ["--tol", "1"], "expect": {"fail": False, "bad": 0}},
    {"name": "segoe -12 wine-like", "file": "segoe12.rec", "expect": {"fail": True, "hints": ["TM.il: isolated", "Segoe UI|req_h=-12"]}},
    {"name": "face substitution", "file": "face_subst.rec", "expect": {"fail": True, "bad": 3, "hints": ["appears as 'DejaVu Sans' in 3 row"]}},
    {"name": "max width halved", "file": "maxw_half.rec", "expect": {"fail": True, "hints": ["TM.max"]}},
    {"name": "max width halved, ignored", "file": "maxw_half.rec", "args": ["--ignore", "max"], "expect": {"fail": False, "bad": 0}},
    {"name": "w_W jitter all rows", "file": "jitter_w.rec", "expect": {"fail": True, "hints": ["systematic offset +1 in all 27 rows"]}},
    {"name": "jitter ok at tol w_W=1", "file": "jitter_w.rec", "args": ["--tol", "w_W=1"], "expect": {"fail": False}},
    {"name": "courier rows missing", "file": "no_courier.rec", "expect": {"fail": True, "missing": 3, "hints": ["face=Courier New is missing"]}},
    {"name": "courier rows missing allowed", "file": "no_courier.rec", "args": ["--allow-missing"], "expect": {"fail": False, "missing": 3}},
    {"name": "extents 25 percent wider", "file": "wide_ext.rec", "expect": {"fail": True, "hints": ["proportional scale"]}},
    {"name": "extents wider within rel-tol 0.3", "file": "wide_ext.rec", "args": ["--rel-tol", "0.3"], "expect": {"fail": False}},
    {"name": "pitch/family flag bits", "file": "pitchfamily.rec", "expect": {"fail": True, "hints": ["TM.pf: flag bits differ"]}},
]
json.dump({"reference": "win10_tm_hello.rec", "cases": cases}, open(os.path.join(fx, "cases.json"), "w"), indent=1)
print("wrote", len(cases), "cases")
