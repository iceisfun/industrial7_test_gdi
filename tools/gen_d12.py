#!/usr/bin/env python3
"""gen_d12.py - derive the decision tables of a Windows-10-like text/font stack from the committed recordings.

    python3 tools/gen_d12.py            # writes docs/D12_DECISION_DATA.md
    python3 tools/gen_d12.py --check    # exit 1 when the committed file differs from the generated one
    python3 tools/gen_d12.py -o FILE    # write elsewhere ("-" = stdout)

Input: data/win10/<family>/*.rec[.gz] only (never re-recorded here). Output is a pure function of those files:
no timestamps, no host data, rows in recording order, so two runs give identical bytes.

Every table row ends with a `src` cell naming the recording file (relative to data/win10) and the row it comes from,
as `<file> <table>:<row key>` with the key exactly as written in the recording (percent-escaped tokens).
A `@` in a key stands for the field(s) listed in the row's own set/column cell: the row then stands for one
recording row per member (grouped rows and pivot tables; the header of the table says which).

Other tools (tools/gen_golden.py) import this module: `build()` returns the tables with typed row data
(`Row.k` key fields, `Row.v` value fields, `Row.src`), so the golden tests assert exactly the rows printed here.
"""
import argparse
import collections
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rec  # noqa: E402

DATA = os.path.join(ROOT, "data", "win10")
OUT = os.path.join(ROOT, "docs", "D12_DECISION_DATA.md")

_cache = {}


def load(rel):
    """Recording `rel` (relative to data/win10; the .gz suffix is found automatically)."""
    if rel in _cache:
        return _cache[rel]
    p = os.path.join(DATA, rel)
    if not os.path.exists(p):
        alt = p[:-3] if p.endswith(".gz") else p + ".gz"
        assert os.path.exists(alt), rel
        p = alt
    rs = rec.load(p)
    assert len(rs) == 1 and rs[0].ok and rs[0].status == "ok", rel
    _cache[rel] = (os.path.relpath(p, DATA), rs[0])
    return _cache[rel]


class Row:
    """One decision row: k = key fields (unescaped str), v = value fields (named, decoded str or None), src."""

    def __init__(self, k, v, src, cells=None, extra=None):
        self.k, self.v, self.src, self.cells, self.extra = k, v, src, cells, extra or {}


class Table:
    def __init__(self, tid, title, use, source, cols, undefined, how=""):
        self.tid, self.title, self.use, self.source = tid, title, use, source
        self.cols, self.undefined, self.how = cols, undefined, how
        self.rows = []


def src_of(path, table, rawkey):
    return "%s %s:%s" % (path, table, rawkey)


def rows_of(rel, table, where=None):
    """Yield Row (without cells) for every row of `table` of recording `rel`, in recording order."""
    path, r = load(rel)
    names = r.colnames(table)
    for rawkey, vals in r.rows.get(table, {}).items():
        k = rec.parse_key(rawkey)
        if where and not where(k):
            continue
        nm = names if names and len(names) == len(vals) else ["v%d" % i for i in range(len(vals))]
        yield Row(k, dict(zip(nm, vals)), src_of(path, table, rawkey))


# ------------------------------------------------------------------ rendering helpers
def na(x):
    return "n/a" if x is None else str(x)


def q(s):
    """A string value as a quoted, pipe-safe markdown cell."""
    if s is None:
        return "n/a"
    out = []
    for ch in s:
        o = ord(ch)
        if ch == "|":
            out.append("\\|")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif o < 0x20 or o == 0x7F:
            out.append("\\x%02X" % o)
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def code(s):
    return "`" + s.replace("`", "'").replace("|", "\\|") + "`"


def replace_key_field(rawkey, field, repl="@"):
    return ";".join(("%s=%s" % (field, repl) if p.partition("=")[0] == field else p) for p in rawkey.split(";"))


def src_cell(row):
    return code(row.src)


def ints(vs):
    return [int(x) for x in vs]


# ------------------------------------------------------------------ A. face substitution and fallback
FS = "face_substitution/dpi96.rec.gz"


def tbl_face_base():
    t = Table(
        "A1", "Requested face -> realised face (the mapper decision)",
        "What GDI realises when a LOGFONT names a face: the name GetTextFace reports, the family and file that really back the "
        "font, the type (tt/raster/vector) and the metrics of the realised font. Covers installed faces, aliases "
        "(MS Shell Dlg, Helv, Tms Rmn, Times, Helvetica), vertical `@` faces, case variants, non-ASCII names and ~45 absent names. "
        "A face that does not exist is realised as Arial.",
        "%s `map`, rows `src=base;api=W;req_h=-11;req_cs=1;req_pf=0x0;dc=screen;gm=1` (lfHeight -11, DEFAULT_CHARSET, weight 400, "
        "no other fields)" % FS,
        ["requested face", "GetTextFace", "realised family (OTM)", "type", "realised file", "tmCharSet", "tmPitchAndFamily",
         "tmHeight", "ascent", "descent"],
        "A raster/vector face has no OTM family: the cell shows n/a and the family is the GetTextFace name. `realised file` is the "
        "GetFontRealizationInfo file (`.fon` for raster faces). Rows with `req_h=-12` and the api variants (A, ExW, CreateFontW) "
        "exist in the recording; this table lists the W api at -11 (see the agreement note below).")
    for r in rows_of(FS, "map", lambda k: k["src"] == "base" and k["api"] == "W" and k["req_h"] == "-11"):
        v = r.v
        r.cells = [q(r.k["face"]), q(v["got"]), q(v["otm_family"]), v["type"], na(v["rz_file"]), v["cs"], v["pf"],
                   v["h"], v["a"], v["d"], src_cell(r)]
        t.rows.append(r)
    # api agreement: A / ExW / CreateFontW realise the same font as W for every (face, req_h) of src=base
    g = collections.defaultdict(dict)
    for r in rows_of(FS, "map", lambda k: k["src"] == "base"):
        v = r.v
        g[(r.k["face"], r.k["req_h"])][r.k["api"]] = (v["got"], v["otm_family"], v["rz_file"], v["type"], v["h"], v["a"], v["d"],
                                                      v["il"], v["ave"], v["max"], v["pf"], v["cs"])
    multi = {k: v for k, v in g.items() if len(v) > 1}
    bad = sorted(k for k, v in multi.items() if len(set(v.values())) > 1)
    t.how = ("Api agreement (computed from the same file, `src=base` rows): %d (face, req_h) groups carry a W row and at least one "
             "of A / ExW / CreateFontW; the realised face, family, file, type and TEXTMETRIC (h a d il ave max pf cs) are identical "
             "within %d of them%s." % (len(multi), len(multi) - len(bad),
                                        "" if not bad else "; differing groups: " + ", ".join(q("%s@%s" % x) for x in bad)))
    return t


def tbl_face_subst():
    t = Table(
        "A2", "Charset-specific registry substitutes",
        "Faces named in the FontSubstitutes registry key (and a few related names) requested with different charsets: which "
        "face/file is realised and which tmCharSet comes back.",
        "%s `map`, rows `src=subst` (api W, lfHeight -11)" % FS,
        ["requested face", "req charset", "GetTextFace", "realised family (OTM)", "realised file", "tmCharSet", "tmPitchAndFamily"],
        "Rows are literal (one per recording row).")
    for r in rows_of(FS, "map", lambda k: k["src"] == "subst"):
        v = r.v
        r.cells = [q(r.k["face"]), r.k["req_cs"], q(v["got"]), q(v["otm_family"]), na(v["rz_file"]), v["cs"], v["pf"], src_cell(r)]
        t.rows.append(r)
    return t


def tbl_face_family():
    t = Table(
        "A3", "Every enumerated family requested by its own name",
        "Check that a family name that EnumFontFamilies lists is realised as itself (name, file, charset): the identity part of "
        "the mapper, including the vertical `@` families.",
        "%s `map`, rows `src=family` (api W, lfHeight -11, DEFAULT_CHARSET)" % FS,
        ["requested face", "GetTextFace", "realised family (OTM)", "type", "realised file", "tmCharSet", "tmPitchAndFamily"],
        "Rows are literal.")
    for r in rows_of(FS, "map", lambda k: k["src"] == "family"):
        v = r.v
        r.cells = [q(r.k["face"]), q(v["got"]), q(v["otm_family"]), v["type"], na(v["rz_file"]), v["cs"], v["pf"], src_cell(r)]
        t.rows.append(r)
    return t


def grouped(rel, table, where, keyfield, outcome):
    """Group rows (same face, same outcome) -> list of (rows, outcome). Order: first appearance."""
    groups = collections.OrderedDict()
    for r in rows_of(rel, table, where):
        groups.setdefault((r.k["face"], outcome(r)), []).append(r)
    return groups


def tbl_face_charset():
    t = Table(
        "A4", "Requested charset -> realised face",
        "How lfCharSet changes the realised face: a face that lacks the charset is replaced by a face that has it (font linking by "
        "charset) and the tmCharSet that comes back. Each row groups the charsets of one face that realise the same family and file.",
        "%s `map`, rows `src=charset` (api W, lfHeight -11); one row stands for all `req_cs` members listed (key field `req_cs=@`)" % FS,
        ["requested face", "realised family (OTM or GetTextFace for raster)", "realised file", "tmPitchAndFamily",
         "req charset -> tmCharSet"],
        "Charsets not listed in the recording (only 22 values were requested) are undefined.")
    def outcome(r):
        v = r.v
        return (v["otm_family"] if v["type"] == "tt" else v["got"], v["rz_file"], v["pf"])
    for (face, oc), rs in grouped(FS, "map", lambda k: k["src"] == "charset", "req_cs", outcome).items():
        pairs = ", ".join("%s->%s" % (x.k["req_cs"], x.v["cs"]) for x in rs)
        raw = rs[0].src.partition(":")[2]
        row = Row({"face": face}, {}, "%s map:%s" % (rs[0].src.split(" ")[0], replace_key_field(raw, "req_cs")), extra={"rows": rs})
        row.cells = [q(face), q(oc[0]), na(oc[1]), oc[2], pairs, src_cell(row)]
        t.rows.append(row)
    return t


def tbl_face_pitchfamily():
    t = Table(
        "A5", "Requested pitch and family -> realised face",
        "Whether lfPitchAndFamily (pitch 0/1/2, family 0x00..0x50, plus the 0x08 bit) changes the realised font of an installed or "
        "absent face. Each row groups the values of one face that realise the same font.",
        "%s `map`, rows `src=pitchfamily` (api W, lfHeight -11); one row stands for all `req_pf` members listed (key field `req_pf=@`)" % FS,
        ["requested face", "realised family (OTM or GetTextFace for raster)", "realised file", "tmPitchAndFamily", "tmCharSet",
         "req pitch/family values"],
        "Only the 24 values listed in the recording were requested.")
    def outcome(r):
        v = r.v
        return (v["otm_family"] if v["type"] == "tt" else v["got"], v["rz_file"], v["pf"], v["cs"])
    for (face, oc), rs in grouped(FS, "map", lambda k: k["src"] == "pitchfamily", "req_pf", outcome).items():
        raw = rs[0].src.partition(":")[2]
        row = Row({"face": face}, {}, "%s map:%s" % (rs[0].src.split(" ")[0], replace_key_field(raw, "req_pf")), extra={"rows": rs})
        vals = " ".join(x.k["req_pf"] for x in rs)
        row.cells = [q(face), q(oc[0]), na(oc[1]), oc[2], oc[3], vals, src_cell(row)]
        t.rows.append(row)
    return t


def tbl_face_registry():
    t = Table(
        "A6", "Registry substitution and mapper defaults",
        "The tables the mapper reads: FontSubstitutes (name -> name[,charset]) and FontMapper\\FamilyDefaults (family class -> face "
        "used when the requested face is absent).",
        "%s `reg` (rows for HKLM FontSubstitutes and FontMapper\\FamilyDefaults)" % FS,
        ["key", "value name", "type", "value"],
        "The recording carries the 64-bit view and a `(wow32)` copy of FontSubstitutes; only the 64-bit view is listed (the "
        "copies are equal, see the `(wow32)` rows).")
    base = "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\"
    for r in rows_of(FS, "reg", lambda k: k["k"] in (base + "FontSubstitutes", base + "FontMapper\\FamilyDefaults")):
        v = r.v
        r.cells = [code(r.k["k"].replace(base, "...\\")), q(r.k["n"]), v["type"], q(v["value"]), src_cell(r)]
        t.rows.append(r)
    return t


def tbl_face_fontlink():
    t = Table(
        "A7", "Font linking chain (SystemLink)",
        "The per-face fallback chain Windows consults for glyphs missing from a face: ordered list of `file,face[,scaling]` entries.",
        "%s `reg`, rows under `FontLink\\SystemLink`; one row stands for all ordinals `i` of one base face (key field `i=@`)" % FS,
        ["base face", "chain (in order)"],
        "Faces without an entry have no chain recorded.")
    base = "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink"
    groups = collections.OrderedDict()
    for r in rows_of(FS, "reg", lambda k: k["k"] == base):
        groups.setdefault(r.k["n"], []).append(r)
    for face, rs in groups.items():
        raw = rs[0].src.partition(":")[2]
        row = Row({"face": face}, {}, "%s reg:%s" % (rs[0].src.split(" ")[0], replace_key_field(raw, "i")), extra={"rows": rs})
        row.cells = [q(face), "; ".join(q(x.v["value"]) for x in rs), src_cell(row)]
        t.rows.append(row)
    return t


SCRIPTS = ["latin", "latinext", "cyr", "greek", "armenian", "georgian", "hebrew", "arabic", "thai", "devanagari", "cjk", "kana",
           "hangul", "sym", "box", "emoji", "pua", "nbsp_shy", "ctrl", "nonchar"]


def tbl_fallback():
    t = Table(
        "A8", "Glyph coverage per face and script (what falls back)",
        "For a sample string per script, how many characters have no glyph in the requested face (`GetGlyphIndicesW` with "
        "GGI_MARK_NONEXISTING_GLYPHS): 0 = fully covered, otherwise those characters need fallback. Cell = `missing/length`.",
        "%s `fallback`, rows `q=nonaa;req_h=-12` (one row per face; column = key field `text=@`)" % FS,
        ["face"] + SCRIPTS,
        "The sample strings are in the recording (`str`/`fallback` tables of the probe); coverage of other characters is undefined. "
        "Which face is used for the missing characters is not in this table (see the A7 chain).")
    byface = collections.OrderedDict()
    for r in rows_of(FS, "fallback", lambda k: k["req_h"] == "-12"):
        byface.setdefault(r.k["face"], {})[r.k["text"]] = r
    for face, d in byface.items():
        r0 = next(iter(d.values()))
        row = Row({"face": face}, {}, "%s fallback:%s" % (r0.src.split(" ")[0], replace_key_field(r0.src.partition(":")[2], "text")),
                  extra={"cells": d})
        row.cells = [q(face)] + ["%s/%s" % (d[s].v["missing"], d[s].v["n"]) if s in d else "n/a" for s in SCRIPTS] + [src_cell(row)]
        t.rows.append(row)
    return t


# ------------------------------------------------------------------ B. font metrics
FM = "font_metrics/dpi96.rec.gz"
UI_HEIGHTS = [-9, -11, -12, -14, -16, -24]


def tm_cells(r):
    v = r.v
    return [q(v["got"]), v["h"], v["a"], v["d"], v["il"], v["el"], v["ave"], v["max"], v["wt"], v["oh"], v["pf"], v["cs"]]


def tbl_metrics_regular():
    t = Table(
        "B1", "TEXTMETRIC per face and size, regular weight",
        "Ascent, descent, internal leading, average and maximum character width and cell height of every face at every requested "
        "height (character heights -6..-40, default 0, cell heights 6..40), as GetTextMetrics reports them on a 96-DPI screen DC "
        "(MM_TEXT, GM_COMPATIBLE). Negative `req_h` = em height in pixels, positive = cell height.",
        "%s `tm`, rows `wt=400;it=0;q=default;cs=1;gm=compat` (DEFAULT_CHARSET, the main grid)" % FM,
        ["face", "req_h", "GetTextFace", "h", "ascent", "descent", "int. leading", "ext. leading", "ave width", "max width", "weight",
         "overhang", "pitch/family", "charset"],
        "96 DPI only (no DPI sweep of this family; the pixel heights of an unaware process do not depend on the system DPI, "
        "README of font_metrics). Other DPIs: see B4 (UI fonts) and the sweeps in section D. Values for the empty face and the "
        "absent face are the realised fallback (Arial), not a separate font.")
    for r in rows_of(FM, "tm", lambda k: k["wt"] == "400" and k["it"] == "0" and k["q"] == "default" and k["cs"] == "1" and k["gm"] == "compat"):
        r.cells = [q(r.k["face"]), r.k["req_h"]] + tm_cells(r) + [src_cell(r)]
        t.rows.append(r)
    return t


def tbl_metrics_bold_italic():
    t = Table(
        "B2", "TEXTMETRIC, bold and italic at UI heights",
        "The same measurement for weight 700 (bold) and italic (lfItalic 1) requests at the heights UI code actually uses "
        "(-9, -11, -12, -14, -16, -24).",
        "%s `tm`, rows `q=default;cs=1;gm=compat` with (wt=700, it=0) or (wt=400, it=1), req_h in %s" % (FM, UI_HEIGHTS),
        ["face", "weight req", "italic req", "req_h", "GetTextFace", "h", "ascent", "descent", "int. leading", "ext. leading",
         "ave width", "max width", "weight", "overhang", "pitch/family", "charset"],
        "Only the (weight, italic) combinations present in the recording: bold regular, and italic for the faces that were "
        "recorded with italic. Synthesised bold/italic is not distinguished here (see `ri_sim` in the recording).")
    heights = set(str(h) for h in UI_HEIGHTS)
    for r in rows_of(FM, "tm", lambda k: k["q"] == "default" and k["cs"] == "1" and k["gm"] == "compat" and k["req_h"] in heights
                     and ((k["wt"] == "700" and k["it"] == "0") or (k["wt"] == "400" and k["it"] == "1"))):
        r.cells = [q(r.k["face"]), r.k["wt"], r.k["it"], r.k["req_h"]] + tm_cells(r) + [src_cell(r)]
        t.rows.append(r)
    return t


def tbl_metrics_stock():
    t = Table(
        "B3", "Stock fonts",
        "The seven stock fonts (GetStockObject): the face, height and metrics each one realises, read from the stock handle and "
        "from a re-created LOGFONT.",
        "%s `stock_tm` (via=stock and via=recreate)" % FM,
        ["stock object", "via", "GetTextFace", "h", "ascent", "descent", "int. leading", "ave width", "max width", "weight", "pitch/family",
         "charset"],
        "")
    for r in rows_of(FM, "stock_tm"):
        v = r.v
        r.cells = [r.k["obj"], r.k["via"], q(v["got"]), v["h"], v["a"], v["d"], v["il"], v["ave"], v["max"], v["wt"], v["pf"], v["cs"],
                   src_cell(r)]
        t.rows.append(r)
    return t


def tbl_metrics_ui_fonts():
    t = Table(
        "B4", "System UI fonts per DPI",
        "The LOGFONT of each system UI font (SPI_GETNONCLIENTMETRICS / SPI_GETICONMETRICS / icon title) for each DPI, "
        "with the metrics of the font realised from it. `dpi=sys` is the process view (no ForDpi call), the others are "
        "SystemParametersInfoForDpi(dpi).",
        "%s `ui_lf` + `ui_tm` (same keys `src;dpi`)" % FM,
        ["source", "dpi", "lfHeight", "lfWeight", "lfFaceName", "GetTextFace", "h", "ascent", "descent", "int. leading", "ave width",
         "max width"],
        "`dpi=72` is not recorded for these tables (SystemParametersInfoForDpi at 72 DPI returns TRUE with garbage, "
        "README of dpi_scaling: undefined cell).")
    path, r = load(FM)
    for lf in rows_of(FM, "ui_lf"):
        tmk = [x for x in rows_of(FM, "ui_tm", lambda k, kk=lf.k: k == kk)][0]
        row = Row(lf.k, dict(lf.v, **{"tm_" + a: b for a, b in tmk.v.items()}), lf.src + " + " + tmk.src.split(" ", 1)[1])
        v = row.v
        row.cells = [lf.k["src"], lf.k["dpi"], v["h"], v["wt"], q(v["face"]), q(v["tm_got"]), v["tm_h"], v["tm_a"], v["tm_d"], v["tm_il"],
                     v["tm_ave"], v["tm_max"], code(row.src)]
        t.rows.append(row)
    return t


def tbl_metrics_ncm():
    t = Table(
        "B5", "Non-client metrics per DPI",
        "NONCLIENTMETRICS widths/heights and ICONMETRICS spacing for each DPI (SystemParametersInfoForDpi), in pixels.",
        "%s `ncm` and `icm` (rows keyed `dpi`)" % FM,
        ["dpi", "border", "scroll w", "scroll h", "caption w", "caption h", "small caption w", "small caption h", "menu w", "menu h",
         "padded border", "icon h spacing", "icon v spacing", "title wrap"],
        "")
    icm = {r.k["dpi"]: r for r in rows_of(FM, "icm")}
    for r in rows_of(FM, "ncm"):
        v = r.v
        i = icm[r.k["dpi"]]
        row = Row(r.k, dict(v, **{"icm_" + a: b for a, b in i.v.items()}), r.src + " + " + i.src.split(" ", 1)[1])
        row.cells = [r.k["dpi"], v["border"], v["scrollw"], v["scrollh"], v["capw"], v["caph"], v["smcapw"], v["smcaph"], v["menuw"],
                     v["menuh"], v["padborder"], i.v["hspace"], i.v["vspace"], i.v["wrap"], code(row.src)]
        t.rows.append(row)
    return t


# ------------------------------------------------------------------ C. character widths
CW = "char_widths/dpi96.rec.gz"
CW_FACES = None
CW_HEIGHTS = [-8, -9, -10, -11, -12, -13, -14, -16, -20, -24]
ASCII_BLKS = ["asc2", "asc3", "asc4", "asc5", "asc6", "asc7"]


def classes(widths_by_char):
    """{char: width} -> ordered [(width, 'chars')] by width."""
    d = collections.defaultdict(list)
    for c, w in widths_by_char:
        d[w].append(c)
    return sorted(d.items())


def blk_chars(blk):
    _, r = load(CW)
    return [int(x, 16) for x in r.rows["chars"]["blk=" + blk]]


def width_rows(face, h, wt, blks):
    """[(codepoint, width)] over the blocks, or None when a block is missing / not available."""
    _, r = load(CW)
    out = []
    for b in blks:
        key = "face=%s;req_h=%s;wt=%d;it=0;blk=%s" % (rec.esc(face), h, wt, b)
        vals = r.rows["cw32"].get(key)
        if vals is None or any(x is None for x in vals):
            return None
        out += list(zip(blk_chars(b), (int(x) for x in vals)))
    return out


def charstr(cps):
    """Characters of a width class; anything that is not a plain graphic ASCII/Latin character is written <HEX>."""
    out = []
    for c in cps:
        if c <= 0x20 or c in (0x5C, 0x7C, 0x60, 0x3C, 0x3E) or 0x7F <= c <= 0xA0 or c == 0xAD:
            out.append("<%02X>" % c)
        else:
            out.append(chr(c))
    return "`" + "".join(out) + "`"


def tbl_width_classes(tid, title, blks, heights, use, extra_undef):
    t = Table(tid, title, use,
              "%s `cw32` (GetCharWidth32W per character), rows `wt=400;it=0`, blocks %s; one row stands for those blocks (key field `blk=@`)"
              % (CW, ", ".join(blks)),
              ["face", "req_h", "classes (width px: characters)"], extra_undef)
    _, r = load(CW)
    faces = list(dict.fromkeys(rec.parse_key(k)["face"] for k in r.rows["font"]))
    for face in faces:
        if face == "NoSuchFace I7":
            continue
        for h in heights:
            w = width_rows(face, str(h), 400, blks)
            if w is None:
                continue
            cl = classes(w)
            key = "face=%s;req_h=%d;wt=400;it=0;blk=@" % (rec.esc(face), h)
            row = Row({"face": face, "req_h": str(h)}, {}, "%s cw32:%s" % (CW, key), extra={"classes": cl, "blks": blks})
            row.cells = [q(face), str(h), "; ".join("%d: %s" % (wd, charstr(cs)) for wd, cs in cl), code(row.src)]
            t.rows.append(row)
    return t


def tbl_width_ascii():
    return tbl_width_classes(
        "C1", "Character-width classes, ASCII 0x20..0x7F",
        ASCII_BLKS, CW_HEIGHTS,
        "The advance width in device pixels of every printable ASCII character (plus DEL, 0x7F), grouped into width classes per "
        "face and height: a layout engine that has these classes knows every ASCII advance. Widths are GetCharWidth32W values "
        "(integer pixels, hinted, 96 DPI screen DC).",
        "Only regular weight, italic off, heights as listed; the recording also has bold, italic and other heights (cw32 table). "
        "Kerning is not applied (see the `kern` table).")


def tbl_width_latin1():
    return tbl_width_classes(
        "C2", "Character-width classes, Latin-1 0xA0..0xFF",
        ["lat1a", "lat1b", "lat1c", "lat1d", "lat1e", "lat1f"], [-11, -12, -16],
        "As C1 for the Latin-1 supplement, at the three heights that are the core UI sizes.",
        "A character absent from a face is measured as the default glyph width (the recording does not mark it here; see the `gi` "
        "table for glyph indices).")


STRINGS = ["fox", "digits", "W10", "i10", "alpha", "hello", "ok", "cancel", "fevh"]


def tbl_string_extents():
    t = Table(
        "C3", "String extents",
        "GetTextExtentPoint32W width (cx) of reference strings per face and height: pangram, digits, ten W, ten i, the alphabet, "
        "common button/menu captions. These are the sums of the C1 advances; they check an implementation's string measurement "
        "end to end. The strings are in the recording's `str` table.",
        "%s `ext`, rows `wt=400;it=0` (one row per face and height; column = key field `s=@`)" % CW,
        ["face", "req_h"] + STRINGS,
        "Only the listed strings, regular weight. The recording also holds cy, fit counts and per-character extents (`ext`, `exdx`).")
    _, r = load(CW)
    for face in dict.fromkeys(rec.parse_key(k)["face"] for k in r.rows["font"]):
        if face == "NoSuchFace I7":
            continue
        for h in [-11, -12, -16, -24]:
            cells = {}
            for s in STRINGS:
                key = "face=%s;req_h=%d;wt=400;it=0;s=%s" % (rec.esc(face), h, s)
                if key in r.rows["ext"]:
                    cells[s] = (key, r.rows["ext"][key])
            if not cells:
                continue
            row = Row({"face": face, "req_h": str(h)}, {}, "%s ext:face=%s;req_h=%d;wt=400;it=0;s=@" % (CW, rec.esc(face), h),
                      extra={"cells": cells})
            row.cells = [q(face), str(h)] + [na(cells[s][1][0]) if s in cells else "n/a" for s in STRINGS] + [code(row.src)]
            t.rows.append(row)
    return t


# ------------------------------------------------------------------ D. UI metrics
UI = "ui_metrics/dpi96.rec.gz"
UI_DPI_FILES = ["dpi96", "dpi120", "dpi144", "dpi168", "dpi192"]
SM_DPIS = [72, 96, 120, 144, 168, 192, 240, 288]
SM_NAMES = ["CXVSCROLL", "CYHSCROLL", "CYCAPTION", "CXBORDER", "CYBORDER", "CXDLGFRAME", "CYDLGFRAME", "CYVTHUMB", "CXHTHUMB",
            "CXICON", "CYICON", "CXCURSOR", "CYCURSOR", "CYMENU", "CXMIN", "CYMIN", "CXSIZE", "CYSIZE", "CXFRAME", "CYFRAME",
            "CXMINTRACK", "CYMINTRACK", "CXDOUBLECLK", "CYDOUBLECLK", "CXICONSPACING", "CYICONSPACING", "CXEDGE", "CYEDGE",
            "CXMINSPACING", "CYMINSPACING", "CXSMICON", "CYSMICON", "CYSMCAPTION", "CXSMSIZE", "CYSMSIZE", "CXMENUSIZE",
            "CYMENUSIZE", "CXDRAG", "CYDRAG", "CXMENUCHECK", "CYMENUCHECK", "CXFOCUSBORDER", "CYFOCUSBORDER", "CXPADDEDBORDER"]


def tbl_sm_dpi():
    t = Table(
        "D1", "GetSystemMetricsForDpi",
        "System metrics (frame, caption, scrollbar, icon, menu, edge sizes) for every DPI; the per-DPI scaling rule of the "
        "non-client area in pixels.",
        "%s `sm_dpi` (one row per metric; column = key field `dpi=@`)" % UI,
        ["metric", "index"] + ["dpi %d" % d for d in SM_DPIS],
        "Index 93 (undocumented) is volatile between boots and is excluded (README of ui_metrics). The recording has the other "
        "indices 0..99 and 0x1000..0x2004 too (screen size, work area and similar are session state, not DPI rules). The dpi 72 column is unverified: a later guest run returned a different CXMIN there (106 vs 128), so below 96 DPI treat cells as undefined. At dpi 72 the "
        "values are as returned (e.g. CYCAPTION 24 > 96-DPI 23); the 72-DPI SystemParametersInfoForDpi values are undefined, see B4.")
    _, r = load(UI)
    byname = collections.OrderedDict()
    for k, v in r.rows["sm_dpi"].items():
        kk = rec.parse_key(k)
        byname.setdefault(kk["name"], {"idx": kk["idx"], "cells": {}})["cells"][int(kk["dpi"])] = (k, int(v[0]))
    for name in SM_NAMES:
        e = byname[name]
        row = Row({"name": name, "idx": e["idx"]}, {}, "%s sm_dpi:dpi=@;idx=%s;name=%s" % (UI, e["idx"], name), extra={"cells": e["cells"]})
        row.cells = [name, e["idx"]] + [str(e["cells"][d][1]) if d in e["cells"] else "n/a" for d in SM_DPIS] + [code(row.src)]
        t.rows.append(row)
    return t


AWR_STYLES = ["overlappedwindow", "dialog", "caption", "popup_border"]
AWR_EX = ["0", "clientedge", "windowedge", "toolwindow"]
AWR_DPIS = [96, 120, 144, 168, 192]


def tbl_awr_dpi():
    t = Table(
        "D2", "AdjustWindowRectExForDpi",
        "The non-client margins (left, top, right, bottom, in pixels) that a window style adds around a client area, for each DPI, "
        "with and without a menu bar: the recording adjusts a 100x100 client rectangle and stores -left, -top, right-100, bottom-100.",
        "%s `awr_dpi` (one row per style/ex/menu; column = key field `dpi=@`)" % UI,
        ["style", "ex style", "menu"] + ["dpi %d (l,t,r,b)" % d for d in AWR_DPIS],
        "Only the styles and extended styles listed; the recording has 15 styles x 9 extended styles x 2 menu values.")
    _, r = load(UI)
    for st in AWR_STYLES:
        for ex in AWR_EX:
            for m in ("0", "1"):
                cells = {}
                for d in AWR_DPIS:
                    key = "dpi=%d;style=%s;ex=%s;menu=%s" % (d, st, ex, m)
                    if key in r.rows["awr_dpi"]:
                        cells[d] = (key, r.rows["awr_dpi"][key])
                if len(cells) != len(AWR_DPIS):
                    continue
                row = Row({"style": st, "ex": ex, "menu": m}, {}, "%s awr_dpi:dpi=@;style=%s;ex=%s;menu=%s" % (UI, st, ex, m),
                          extra={"cells": cells})
                row.cells = [st, ex, m] + [",".join(na(x) for x in cells[d][1][1:5]) for d in AWR_DPIS] + [code(row.src)]
                t.rows.append(row)
    return t


DLG_COLS = [("dpi96", "proc"), ("dpi120", "proc"), ("dpi120", "system"), ("dpi144", "system"), ("dpi192", "system"),
            ("dpi120", "unaware")]
DLG_FACE = "MS Shell Dlg"


def tbl_dialog_controls():
    t = Table(
        "D3", "Dialog base units and DLU control sizes",
        "How dialog units turn into pixels (`dbu` = GetDialogBaseUnits) and the pixel size of 23 common controls created from a "
        "dialog template (MS Shell Dlg 8 pt, modal frame, DS_SETFONT): the DPI-dependent layout table of a dialog engine.",
        "`ui_metrics/<column file>` tables `dbu` and `ctl` (template `tmpl=std;style=setfont;face=MS Shell Dlg;pt=8;wt=0;it=0;frame=modal`); "
        "the column header names file and thread DPI context (key field `ctx=`); key field `cid=` is the row",
        ["control (cid)", "DLU w x h"] + ["%s %s" % c for c in DLG_COLS],
        "The probe exe has no DPI manifest. FACT (README of ui_metrics): at 144/168/192 the `proc` context sees virtualised 96 DPI "
        "(not listed: it equals dpi96), the `system` context shows the real-DPI sizes; the unaware context reports the 96 layout "
        "in every file. The `dbu` row is GetDialogBaseUnits as recorded (8x16 in every column except `proc` at "
        "120); it is not a scaling law. Per-monitor contexts (`pm`, `pm2`) equal `system` in the recording and are not repeated. Only "
        "the face MS Shell Dlg 8 pt is tabulated; other dialog fonts (Tahoma, Segoe UI, MS Sans Serif) are in the `ctl` table of the "
        "recording. The dpi144+ sweep files carry the desktop-DPI state of the 120 stage (ui_metrics README): use cold144 as the "
        "reference for unaware rows there.")
    pivots = collections.OrderedDict()
    for fl, ctx in DLG_COLS:
        path, r = load("ui_metrics/%s.rec.gz" % fl)
        names = r.colnames("ctl")
        for k, vals in r.rows["ctl"].items():
            kk = rec.parse_key(k)
            if kk["ctx"] != ctx or kk["face"] != DLG_FACE or kk["tmpl"] != "std" or kk["pt"] != "8" or kk["frame"] != "modal" or kk["style"] != "setfont":
                continue
            d = dict(zip(names, vals))
            pivots.setdefault(kk["cid"], collections.OrderedDict())[(fl, ctx)] = (path, k, d)
    dbu = collections.OrderedDict()
    for fl, ctx in DLG_COLS:
        path, r = load("ui_metrics/%s.rec.gz" % fl)
        dbu[(fl, ctx)] = (path, "ctx=%s" % ctx, r.rows["dbu"]["ctx=%s" % ctx])
    row = Row({"cid": "dbu"}, {}, "ui_metrics/<column file> dbu:ctx=<column ctx>", extra={"cells": dbu})
    row.cells = ["dialog base units (x, y)", "-"] + ["%sx%s" % (dbu[c][2][0], dbu[c][2][1]) for c in DLG_COLS] + [code(row.src)]
    t.rows.append(row)
    for cid, cells in pivots.items():
        if len(cells) != len(DLG_COLS):
            continue
        dd = cells[DLG_COLS[0]][2]
        row = Row({"cid": cid}, {}, "ui_metrics/<column file> ctl:ctx=<column ctx>;tmpl=std;style=setfont;face=MS%%20Shell%%20Dlg;pt=8;wt=0;it=0;frame=modal;cid=%s" % cid,
                  extra={"cells": cells})
        row.cells = [cid, "%sx%s" % (dd["du_w"], dd["du_h"])] + ["%sx%s" % (cells[c][2]["w"], cells[c][2]["h"]) for c in DLG_COLS] + [code(row.src)]
        t.rows.append(row)
    return t


CH_FILES = [("dpi96", "control_heights/dpi96.rec.gz"), ("dpi120", "control_heights/dpi120.rec.gz"),
            ("dpi144", "control_heights/dpi144.rec.gz"), ("dpi168", "control_heights/dpi168.rec.gz"),
            ("dpi192", "control_heights/dpi192.rec.gz"), ("v6 dpi96", "control_heights/dpi96.v6.rec.gz")]
CH_FONTS = ["default", "segoe-12", "segoe9pt", "tahoma-11", "shelldlg-11", "shelldlg2-11", "mssans-11", "arial-12", "guifont", "system",
            "ncm-message"]
# (table, cfg, value column, label)
CH_SPEC = [("cb", "dropdown", "win_h", "combo closed height"), ("cb", "dropdown", "ih_sel", "combo selection-field item height"),
           ("cb", "dropdown", "ih_item", "combo list item height"), ("cb", "dropdown", "drop_h", "combo dropped height"),
           ("lb", "std", "ih", "listbox item height"), ("lb", "std", "win_h", "listbox height (integral)"),
           ("tv", "plain", "ih", "treeview item height"), ("tv", "plain", "indent", "treeview indent"),
           ("lv", "plain", "row_h", "listview report row height"), ("lv", "plain", "hdr_h", "listview header height"),
           ("tb", "std_il0_notext", "win_h", "toolbar height (no text)"), ("tb", "std_il0_text", "win_h", "toolbar height (text)"),
           ("sb", "simple", "win_h", "status bar height"), ("rb", "text_only", "bar_h", "rebar height"),
           ("edit", "single_ce", "cli_h", "single-line edit client height (20 px window)"),
           ("edit", "multi_ce", "marg_l", "multiline edit left margin")]


def ch_row(rec_, table, font, theme, cfg, col, view=None):
    names = rec_.colnames(table)
    for k, vals in rec_.rows.get(table, {}).items():
        kk = rec.parse_key(k)
        if kk.get("font") == font and kk.get("theme") == theme and kk.get("cfg") == cfg and (view is None or kk.get("view") == view):
            return k, dict(zip(names, vals))[col]
    return None, None


def tbl_control_heights():
    t = Table(
        "D4", "Control heights per font, DPI and manifest",
        "Pixel sizes of common controls (combo box, list box, tree view, list view, toolbar, status bar, rebar, edit) for 11 "
        "dialog fonts, per DPI and for the comctl32 v6 manifest: the control-height table of a UI toolkit.",
        "`control_heights/<column file>` (columns: unmanifested exe at dpi96/120/144/168/192, and the v6 exe at 96); row = table + key fields "
        "`font=<font>;theme=app;cfg=<cfg>`, value column named in the row",
        ["control / value", "font", "cfg"] + [c[0] for c in CH_FILES],
        "FACT (README of control_heights): the exe is DPI-unaware, so at dpi144/168/192 it sees the virtualised 96 DPI view and the "
        "cells equal dpi96; only dpi120 is a real scaled column. Per-monitor/system-aware variants were not recorded (STATE: not done). "
        "A font that could not be created or a control that failed shows n/a.")
    for tab, cfg, col, label in CH_SPEC:
        for font in CH_FONTS:
            cells = collections.OrderedDict()
            for lab, rel in CH_FILES:
                path, r = load(rel)
                key, val = ch_row(r, tab, font, "app", cfg, col, view="report" if tab == "lv" else None)
                cells[lab] = (path, tab, key, val)
            if all(c[3] is None for c in cells.values()):
                continue
            first = next(c for c in cells.values() if c[2])
            row = Row({"table": tab, "font": font, "cfg": cfg, "col": col}, {}, "control_heights/<column file> %s:%s [%s]" % (tab, first[2], col),
                      extra={"cells": cells})
            row.cells = ["%s: %s (%s)" % (tab, label, col), font, cfg] + [na(c[3]) for c in cells.values()] + [code(row.src)]
            t.rows.append(row)
    return t


# ------------------------------------------------------------------ assemble
def build():
    tabs = [tbl_face_base(), tbl_face_subst(), tbl_face_family(), tbl_face_charset(), tbl_face_pitchfamily(), tbl_face_registry(),
            tbl_face_fontlink(), tbl_fallback(),
            tbl_metrics_regular(), tbl_metrics_bold_italic(), tbl_metrics_stock(), tbl_metrics_ui_fonts(), tbl_metrics_ncm(),
            tbl_width_ascii(), tbl_width_latin1(), tbl_string_extents(),
            tbl_sm_dpi(), tbl_awr_dpi(), tbl_dialog_controls(), tbl_control_heights()]
    return collections.OrderedDict((t.tid, t) for t in tabs)


SECTIONS = [
    ("A", "Face substitution and fallback", "A1 A2 A3 A4 A5 A6 A7 A8"),
    ("B", "Font metrics", "B1 B2 B3 B4 B5"),
    ("C", "Character widths", "C1 C2 C3"),
    ("D", "UI metrics and control heights", "D1 D2 D3 D4"),
]

HEAD = """# Decision data of a Windows 10 text/font stack

Generated by `tools/gen_d12.py` from the committed Windows 10 22H2 recordings in `data/win10/` (nothing here is typed by hand or
re-recorded; rerun `python3 tools/gen_d12.py` to regenerate, `--check` to verify). Oracle: Windows 10 Home 22H2, 10.0.19045 (UBR 2965),
en-US (ACP 1252), 1024x768x32, std VGA, one monitor, administrator session, 96 DPI system DPI; the facts of each recording family
(boots, determinism, documented exceptions) are in `data/win10/<family>/README.md`. The recordings are deterministic over two fresh boots
except the cells listed under "Undefined cells and volatile data".

## What this is for

An implementer of a Windows-10-like text and UI stack (font mapper, text metrics, string measurement, dialog and control layout)
needs the *decisions* Windows makes, not only the API contract: which face a LOGFONT realises, what metrics that font has at a
size, how wide each character is, and how big the UI is at each DPI. These tables are those decisions, measured. `tools/gen_golden.py`
turns a sample of the rows into conformance tests (`tests/golden_*`) that pass on a real Windows 10 guest by construction.

Section A answers "requested face / charset / pitch / family -> realised face (and which text falls back)", section B "font x size ->
ascent, descent, leadings, widths, height", section C "face x size x character -> advance width", section D "DPI x manifest -> system
metrics, dialog units, control heights".

## Conventions

- `src` (last column of every table) = `<recording file relative to data/win10> <table>:<row key>`, the key as written in the recording
  (tokens percent-escaped: `%20` space, `~` empty string). Looking the row up: `python3 tools/rec.py show data/win10/<file> --table <table>`.
- A `@` in a key stands for the field(s) listed in the row (a set or a column): the row then stands for one recording row per member.
  `<column file>` / `<column ctx>` in a `src` mean the file/context named by the column header.
- `n/a` = the recording has `!` (call failed or value not available). Heights: `req_h` < 0 is the character (em) height in pixels,
  > 0 the cell height, 0 the default. All lengths are device pixels at the stated DPI. `tmPitchAndFamily`/`tmCharSet` are the
  TEXTMETRIC fields (hex pitch/family byte: 0x01 variable-pitch/fixed bit, 0x02 vector, 0x04 TrueType, 0x08 device; high nibble = family).
- Row order is the order of the recording. Row counts are stated per table.

## Undefined cells and volatile data

Documented exceptions in the recording READMEs, which these tables inherit:

- `GetSystemMetrics` index 93 (`i93`) is volatile between boots (564 vs 588); excluded from D1.
- `GetSystemMetricsForDpi` at 72 DPI: a later process on the same image returned CXMIN 106 where the recording has 128, so the dpi 72
  column of D1 is unverified (not asserted by the golden tests).
- `SystemParametersInfoForDpi` at 72 DPI returns TRUE with an (almost) zero struct and a varying menu height: undefined; B4/B5 do not list 72.
- The DPI sweep and the cold-boot value at 144 differ for the unaware desktop DPI state (ui_metrics README): the `dpi144/168/192`
  columns of D3/D4 come from the sweep recordings and are reference for the probe's unaware process only.
- control_heights at 144/168/192 are the virtualised 96 DPI view of an unaware exe: they equal dpi96 apart from a few system rows. Real
  per-monitor-aware recordings do not exist yet (STATE.md, not done).
- Everything is at 96 system DPI for fonts (B1..B3, C1..C3, A1..A8): font recordings were made at 96 DPI only.
- `!` cells: raster faces have no outline/OTM fields; comctl32 5.82 (no manifest) has no BCM_GETIDEALSIZE/BCM_GETTEXTMARGIN/TTM_GETBUBBLESIZE.
- Observed-only facts: the face substitution rules (A4/A5) and the glyph coverage (A8) are observations of this machine's font set
  (the standard Windows 10 inventory, `font_inventory` recording); on a machine with other fonts installed the realised files differ.
"""


def render():
    tabs = build()
    out = [HEAD]
    counts = ", ".join("%s %d" % (t.tid, len(t.rows)) for t in tabs.values())
    out.append("Row counts: %s (total %d).\n" % (counts, sum(len(t.rows) for t in tabs.values())))
    for sid, stitle, ids in SECTIONS:
        out.append("\n## %s. %s\n" % (sid, stitle))
        for tid in ids.split():
            t = tabs[tid]
            out.append("\n### %s. %s\n" % (t.tid, t.title))
            out.append("**For:** %s\n" % t.use)
            out.append("**Source:** %s. %d rows.\n" % (t.source, len(t.rows)))
            if t.undefined:
                out.append("**Undefined cells / limits:** %s\n" % t.undefined)
            if t.how:
                out.append("**Note:** %s\n" % t.how)
            cols = t.cols + ["src"]
            out.append("| " + " | ".join(cols) + " |")
            out.append("|" + "|".join("---" for _ in cols) + "|")
            for r in t.rows:
                assert len(r.cells) == len(cols), (t.tid, len(r.cells), len(cols), r.cells)
                out.append("| " + " | ".join(na(c) for c in r.cells) + " |")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", default=OUT)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    text = render()
    if a.check:
        cur = open(a.out, encoding="utf-8").read() if os.path.exists(a.out) else ""
        print("up to date" if cur == text else "DIFFERS")
        return 0 if cur == text else 1
    if a.out == "-":
        sys.stdout.write(text)
    else:
        with open(a.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote %s (%d bytes)" % (a.out, len(text.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
