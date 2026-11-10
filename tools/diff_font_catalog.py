#!/usr/bin/env python3
"""diff_font_catalog.py - compare the recorded Win10 font inventory (probe font_inventory) with the Industrial7
font catalog manifests (the Win7 font set and the font-link set).

  tools/diff_font_catalog.py [--rec data/win10/font_inventory/dpi96.rec]
        [--win7 /home/iceisfun/work/industrial7/docs/design/dwrite/win7-fonts.manifest.tsv]
        [--fontlink /home/iceisfun/work/industrial7/docs/design/gdi32/fontlink-fonts.sha256]
        [--md docs/FONT_CATALOG_DIFF.md] [--tsv data/derived/font_catalog_diff.tsv]

Inputs are only read; with neither --md nor --tsv the Markdown goes to stdout. Exit 0 (the diff is data, not a verdict).
Matching is by lowercase file name and by full SHA-256 (renamed-but-identical files are reported too).
Only facts from the two inputs are printed; the 'note' column is derived mechanically (equal / differs / missing).
"""
import argparse, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rec  # noqa: E402

I7 = "/home/iceisfun/work/industrial7/docs/design"
DEF_WIN7 = os.path.join(I7, "dwrite/win7-fonts.manifest.tsv")
DEF_LINK = os.path.join(I7, "gdi32/fontlink-fonts.sha256")
KEY = ["micross.ttf", "tahoma.ttf", "tahomabd.ttf", "segoeui.ttf", "segoeuib.ttf", "segoeuii.ttf", "segoeuiz.ttf",
       "arial.ttf", "arialbd.ttf", "cour.ttf", "courbd.ttf", "times.ttf", "marlett.ttf", "lucon.ttf", "seguisym.ttf",
       "vgasys.fon", "vgaoem.fon", "vgafix.fon", "8514sys.fon", "sserife.fon", "sseriff.fon", "serife.fon", "coure.fon",
       "smalle.fon", "modern.fon", "roman.fon", "script.fon"]
UI_FACES = ["Segoe UI", "MS Shell Dlg", "MS Shell Dlg 2", "Microsoft Sans Serif", "Tahoma", "Arial", "Courier New", "Courier",
            "MS Sans Serif", "System", "Terminal", "Fixedsys", "Small Fonts", "Marlett"]


def load_win7(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or line.startswith("file\t") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        while len(f) < 7:
            f.append("")
        out[f[0].lower()] = {"bytes": int(f[1]), "sha256": f[2].lower(), "famsub": f[4], "weight": f[5], "version": f[6]}
    return out


def load_sha_list(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        h, _, name = line.strip().partition("  ")
        out[name.strip().lower()] = h.lower()
    return out


def pick_rec(path):
    rs = [r for r in rec.load(path) if r.probe == "font_inventory"]
    if not rs:
        sys.exit("no font_inventory recording in %s" % path)
    good = [r for r in rs if r.ok]
    if not good:
        sys.exit("font_inventory recording in %s does not validate: %s" % (path, rs[0].errors))
    return good[0]


def win10_tables(r):
    d = {}
    for k, v in r.rows.get("dir", {}).items():
        f = rec.parse_key(k)["file"]
        d[f] = {"bytes": int(v[0]) if v[0] else None, "sha256": v[1], "kind": v[2], "faces": v[3], "filever": v[4]}
    names = {}
    for k, v in r.rows.get("name", {}).items():
        kk = rec.parse_key(k)
        names[(kk["file"], int(kk["face"]), int(kk["id"]))] = v[0]
    os2 = {}
    for k, v in r.rows.get("os2", {}).items():
        kk = rec.parse_key(k)
        os2[(kk["file"], int(kk["face"]))] = v
    return d, names, os2


def fmt_sha(h):
    return (h[:16] + "...") if h else "-"


def build(args):
    r = pick_rec(args.rec)
    w7 = load_win7(args.win7)
    link = load_sha_list(args.fontlink)
    d10, names, os2 = win10_tables(r)
    files10 = {f: v for f, v in d10.items() if v["kind"] != "dir"}
    by_sha10 = {}
    for f, v in files10.items():
        if v["sha256"]:
            by_sha10.setdefault(v["sha256"], []).append(f)
    by_sha7 = {}
    for f, v in w7.items():
        by_sha7.setdefault(v["sha256"], []).append(f)

    def w10_id(f):
        fam, sub, ver = names.get((f, 0, 1)), names.get((f, 0, 2)), names.get((f, 0, 5))
        o = os2.get((f, 0))
        return ("%s/%s" % (fam, sub) if fam else ""), (str(o[2]) if o else ""), (ver or "")

    rows = []  # file, status, w7 bytes, w7 sha, w10 bytes, w10 sha, w7 famsub, w10 famsub, w7 ver, w10 ver
    for f in sorted(set(w7) | set(files10)):
        a, b = w7.get(f), files10.get(f)
        if a and b:
            st = "same" if a["sha256"] == b["sha256"] else ("differs" if b["sha256"] else "unhashed")
        elif a:
            st = "only_win7"
            if a["sha256"] in by_sha10:
                st = "renamed_in_win10:" + ",".join(by_sha10[a["sha256"]])
        else:
            st = "only_win10"
            if b["sha256"] and b["sha256"] in by_sha7:
                st = "renamed_from_win7:" + ",".join(by_sha7[b["sha256"]])
        fs10, wt10, v10 = w10_id(f) if b else ("", "", "")
        rows.append([f, st, a["bytes"] if a else "", a["sha256"] if a else "", b["bytes"] if b else "", (b["sha256"] or "") if b else "",
                     a["famsub"] if a else "", fs10, a["weight"] if a else "", wt10, a["version"] if a else "", v10])
    return r, w7, link, files10, rows, w10_id


def regval(r, key, name):
    for k, v in r.rows.get("reg", {}).items():
        kk = rec.parse_key(k)
        if kk.get("key") == key and kk.get("name", "").lower() == name.lower() and kk.get("i") == "0":
            return v[1]
    return None


def markdown(args, r, w7, link, files10, rows, w10_id):
    cnt = {}
    for x in rows:
        s = x[1].split(":")[0]
        cnt[s] = cnt.get(s, 0) + 1
    env = r.env
    L = []
    A = L.append
    A("# Font catalog diff: recorded Win10 22H2 font inventory vs the Industrial7 Win7 catalog")
    A("")
    A("Generated by `tools/diff_font_catalog.py` (do not edit by hand; rerun it). Inputs:")
    A("")
    A("- Win10: `%s` (probe `font_inventory` v=%s, variant %s, tag %s; guest `os.version` %s UBR %s, `%s`)." % (
        os.path.relpath(args.rec, os.path.dirname(HERE)), r.meta.get("v"), r.variant, r.tag, env.get("os.version"), env.get("os.ubr"), env.get("os.product")))
    A("- Win7 catalog: `%s` (%d files)." % (args.win7, len(w7)))
    A("- Font-link set: `%s` (%d files)." % (args.fontlink, len(link)))
    A("")
    A("Every number below is copied from those files. FACT = the recording / manifest says so; nothing here is inferred")
    A("except where a line says *inference*.")
    A("")
    A("## 1. Totals")
    A("")
    summ = next(iter(r.rows.get("summary", {}).values()), None)
    if summ:
        A("Win10 `%%WINDIR%%\\Fonts`: %s entries, %s bytes; ttf %s, ttc %s, otf %s, NE .fon %s, PE .fon %s, dirs %s, other %s, unread %s; "
          "%s sfnt faces, %s FNT resources, %s files hashed." % tuple(summ[:13]))
        A("")
    A("| status | files |")
    A("|---|---:|")
    for s in ["same", "differs", "only_win7", "renamed_in_win10", "only_win10", "renamed_from_win7", "unhashed"]:
        if s in cnt:
            A("| %s | %d |" % (s, cnt[s]))
    A("")
    A("`same` = identical name and SHA-256 on both; `differs` = same name, different bytes (a newer Win10 build of the font);")
    A("`renamed_*` = identical bytes under another name.")
    A("")
    A("## 2. The UI files the D12 question names")
    A("")
    A("| file | Win7 bytes | Win7 version | Win10 bytes | Win10 version (name ID 5) | Win10 file version | status |")
    A("|---|---:|---|---:|---|---|---|")
    idx = {x[0]: x for x in rows}
    for f in KEY:
        x = idx.get(f)
        if not x:
            A("| %s | - | - | - | - | - | absent on both |" % f)
            continue
        fv = files10.get(f, {}).get("filever") or "-"
        A("| %s | %s | %s | %s | %s | %s | %s |" % (f, x[2] or "-", x[10] or "-", x[4] or "-", x[11] or "-", fv, x[1]))
    A("")
    A("## 3. Font-link set (`fontlink-fonts.sha256`)")
    A("")
    A("| file | Win7 sha256 | on Win10 | Win10 sha256 | Win10 file with the same bytes |")
    A("|---|---|---|---|---|")
    by_sha10 = {}
    for f, v in files10.items():
        if v["sha256"]:
            by_sha10.setdefault(v["sha256"], []).append(f)
    for f in sorted(link):
        b = files10.get(f)
        same = by_sha10.get(link[f], [])
        A("| %s | %s | %s | %s | %s |" % (f, fmt_sha(link[f]), "yes" if b else "no", fmt_sha(b["sha256"]) if b else "-", ",".join(same) or "-"))
    A("")
    A("## 4. Registry: substitutes and GRE_Initialize (Win10, FACT)")
    A("")
    A("| key | value name | data |")
    A("|---|---|---|")
    for key, name in [("FontSubstitutes", "MS Shell Dlg"), ("FontSubstitutes", "MS Shell Dlg 2"), ("FontSubstitutes", "Helv"),
                      ("FontSubstitutes", "Tms Rmn"), ("FontSubstitutes", "Helvetica"), ("FontSubstitutes", "Times"), ("FontSubstitutes", "Courier"),
                      ("GRE_Initialize", "FONTS.FON"), ("GRE_Initialize", "FIXEDFON.FON"), ("GRE_Initialize", "OEMFONT.FON")]:
        A("| %s | %s | %s |" % (key, name, regval(r, key, name) if regval(r, key, name) is not None else "(absent)"))
    A("")
    A("## 5. Which file GDI maps each UI face to (Win10 `gdi_file`, lfHeight -11, FW_NORMAL, DEFAULT_CHARSET)")
    A("")
    A("`GetFontData` of the selected font hashed and matched against the directory hashes; `!` = GetFontData failed")
    A("(bitmap/vector font).")
    A("")
    A("| requested face | GetTextFace | tmPitchAndFamily | file(s) with identical bytes |")
    A("|---|---|---|---|")
    gf = {}
    for k, v in r.rows.get("gdi_file", {}).items():
        kk = rec.parse_key(k)
        gf[(kk["face"], kk["weight"], kk["cs"])] = v
    for face in UI_FACES:
        v = gf.get((face, "400", "1"))
        if v:
            A("| %s | %s | %s | %s |" % (face, v[0], v[1], v[7] or "-") if v[7] is not None else "| %s | %s | %s | ! |" % (face, v[0], v[1]))
    A("")
    A("## 6. Same name, different bytes")
    A("")
    A("| file | Win7 bytes | Win10 bytes | Win7 family/sub, weight, version | Win10 family/sub, weight, version |")
    A("|---|---:|---:|---|---|")
    for x in rows:
        if x[1] == "differs":
            A("| %s | %s | %s | %s, %s, %s | %s, %s, %s |" % (x[0], x[2], x[4], x[6] or "-", x[8] or "-", x[10] or "-", x[7] or "-", x[9] or "-", x[11] or "-"))
    A("")
    A("## 7. Files only in one catalog")
    A("")
    o7 = [x[0] + (" (" + x[1].split(":", 1)[1] + ")" if ":" in x[1] else "") for x in rows if x[1].startswith(("only_win7", "renamed_in_win10"))]
    o10 = [x[0] + (" (" + x[1].split(":", 1)[1] + ")" if ":" in x[1] else "") for x in rows if x[1].startswith(("only_win10", "renamed_from_win7"))]
    A("Only Win7 (%d): %s" % (len(o7), ", ".join(o7) or "-"))
    A("")
    A("Only Win10 (%d): %s" % (len(o10), ", ".join(o10) or "-"))
    A("")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rec", default=os.path.join(os.path.dirname(HERE), "data/win10/font_inventory/dpi96.rec"))
    ap.add_argument("--win7", default=DEF_WIN7)
    ap.add_argument("--fontlink", default=DEF_LINK)
    ap.add_argument("--md")
    ap.add_argument("--tsv")
    a = ap.parse_args()
    if not os.path.exists(a.rec) and os.path.exists(a.rec + ".gz"):
        a.rec += ".gz"
    r, w7, link, files10, rows, w10_id = build(a)
    md = markdown(a, r, w7, link, files10, rows, w10_id)
    if a.tsv:
        os.makedirs(os.path.dirname(os.path.abspath(a.tsv)), exist_ok=True)
        with open(a.tsv, "w", encoding="utf-8") as f:
            f.write("# tools/diff_font_catalog.py: Win10 font_inventory vs Industrial7 win7-fonts.manifest.tsv\n")
            f.write("file\tstatus\twin7_bytes\twin7_sha256\twin10_bytes\twin10_sha256\twin7_famsub\twin10_famsub\twin7_weight\twin10_weight\twin7_version\twin10_version\n")
            for x in rows:
                f.write("\t".join(str(c) for c in x) + "\n")
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(md)
    if not a.md and not a.tsv:
        sys.stdout.write(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
