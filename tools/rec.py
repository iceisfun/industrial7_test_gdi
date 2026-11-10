#!/usr/bin/env python3
"""rec.py - parse, validate, split, diff and export probe recordings (format: docs/PROBE_FORMAT.md, v1).

Stdlib only. Inputs may be .rec, .rec.gz, a raw serial.log (several recordings mixed with I7VM/I7P markers),
or a directory (all *.rec / *.rec.gz below it).

  rec.py validate FILE...                      check every recording (rows/crc32/status); exit 1 if any is bad
  rec.py split SERIAL --outdir DIR [--tag T]   write each valid recording to DIR/<probe>/<tag>[.<variant>].rec
  rec.py show FILE [--table GLOB] [--env]      human-readable dump (schema-labelled values)
  rec.py csv FILE [--table GLOB] [-o OUT]      CSV of R rows (one header per table) / H rows
  rec.py diff A B [--tol N] [--tol COL=N] [--tol TABLE.COL=N] [--rel-tol R] [--ignore GLOB] [--table GLOB]
                  [--bitmap-tol N] [--env] [--allow-missing] [--allow-extra] [--csv OUT]
  rec.py legacy FILE                           'TAG key=val ...' lines for tools/compare_metrics.py
  rec.py blobs FILE --outdir DIR               extract X blobs (sha256-verified)
  rec.py bitmaps FILE [--outdir DIR] [--ascii] write B rows as PGM files and/or print them as ASCII art
  rec.py selftest                              unit tests of the format + writer/parser round trip

Python API: load(path) -> [Recording]; Recording.rows[table][key] -> list of decoded values;
Writer(...) writes the same format from Python (for host-derived data, e.g. font-table numbers).
"""
import argparse, base64, binascii, csv, fnmatch, gzip, hashlib, io, os, re, sys, zlib

FORMAT = 1
TAGS = {"ENV", "S", "R", "H", "B", "N", "X", "XC"}
_ESC_SET = set(b"%;|~!")


# ------------------------------------------------------------------ escaping
def esc(s):
    """Escape a str/bytes into one token (same rules as rec_esc in common/rec.c)."""
    if s is None:
        return "!"
    b = s.encode("utf-8") if isinstance(s, str) else bytes(s)
    if not b:
        return "~"
    out = []
    for c in b:
        if c <= 0x20 or c >= 0x7F or c in _ESC_SET:
            out.append("%%%02X" % c)
        else:
            out.append(chr(c))
    return "".join(out)


def unesc(tok):
    """Token -> str. '~' -> '', '!' -> None."""
    if tok == "~":
        return ""
    if tok == "!":
        return None
    if "%" not in tok:
        return tok
    out = bytearray()
    i = 0
    while i < len(tok):
        if tok[i] == "%" and i + 2 < len(tok) and re.match(r"[0-9A-Fa-f]{2}", tok[i + 1:i + 3]):
            out.append(int(tok[i + 1:i + 3], 16))
            i += 3
        else:
            out += tok[i].encode("latin-1", "replace")
            i += 1
    return out.decode("utf-8", "replace")


def typed(v):
    """Decoded value -> int / float / str / None (NA)."""
    if v is None or not isinstance(v, str):
        return v
    if re.fullmatch(r"-?\d+", v):
        return int(v)
    if re.fullmatch(r"0x[0-9a-fA-F]+", v):
        return int(v, 16)
    if re.fullmatch(r"-?(\d+\.\d*|\.\d+|\d+)([eE][-+]?\d+)?", v) or v in ("inf", "-inf", "nan"):
        try:
            return float(v)
        except ValueError:
            pass
    return v


def parse_key(k):
    """'face=Segoe%20UI;req_h=-12' -> {'face': 'Segoe UI', 'req_h': '-12'} (ordered)."""
    if k == "~":
        return {}
    d = {}
    for part in k.split(";"):
        n, _, v = part.partition("=")
        d[n] = unesc(v)
    return d


def make_key(d):
    return ";".join("%s=%s" % (n, esc(str(v))) for n, v in d.items()) or "~"


def crc32_lines(lines):
    c = 0
    for l in lines:
        c = zlib.crc32(l.encode("latin-1") + b"\n", c)
    return c & 0xFFFFFFFF


# ------------------------------------------------------------------ model
class Recording:
    def __init__(self, src, lineno, rec_line):
        self.src, self.lineno = src, lineno
        t = rec_line.split(" ")
        self.fmt = int(t[1]) if len(t) > 1 and t[1].isdigit() else -1
        self.probe_tok = t[2] if len(t) > 2 else "?"
        self.probe = unesc(self.probe_tok)
        self.meta = {}
        for kv in t[3:]:
            n, _, v = kv.partition("=")
            self.meta[n] = unesc(v)
        self.variant = self.meta.get("variant", "base") or "base"
        self.tag = self.meta.get("tag", "")
        self.lines = [rec_line]          # record lines REC..(pre END), for crc
        self.end = None                  # dict rows/crc32/status
        self.env = {}
        self.schemas = {}                # table -> (idcols, cols)   (cols may carry ':unit')
        self.rows = {}                   # table -> {key: [decoded values]}
        self.hashes = {}                 # table -> {key: (sha16, len)}
        self.bitmaps = {}                # table -> {key: (w, h, bpp, bytes)}
        self.blobs = {}                  # name -> bytes (verified) ; blob_meta name -> dict
        self.blob_meta, self._chunks = {}, {}
        self.notes = []
        self.errors, self.warnings = [], []

    # ---- status
    @property
    def ok(self):
        return not self.errors

    @property
    def status(self):
        return self.end["status"] if self.end else "truncated"

    def ident(self):
        return (self.probe, self.variant)

    def colnames(self, table):
        sc = self.schemas.get(table)
        if not sc:
            return None
        return [c.split(":")[0] for c in sc[1]]

    def units(self, table):
        sc = self.schemas.get(table)
        return [c.partition(":")[2] for c in sc[1]] if sc else None

    def row_dicts(self, table):
        """yield (keydict, valuedict) with schema names (v0, v1... when no schema)."""
        names = self.colnames(table)
        for k, vals in self.rows.get(table, {}).items():
            nm = names if names and len(names) == len(vals) else ["v%d" % i for i in range(len(vals))]
            yield parse_key(k), dict(zip(nm, vals))

    # ---- line ingestion
    def add(self, tag, rest, lineno):
        if tag == "ENV":
            k, _, v = rest.partition(" ")
            self.env[unesc(k)] = unesc(v)
        elif tag == "S":
            t = rest.split(" ")
            if len(t) < 3 or t[2] != "|":
                self.errors.append("line %d: bad S" % lineno); return
            ids = [] if t[1] == "~" else t[1].split(";")
            self.schemas[unesc(t[0])] = (ids, t[3:])
        elif tag == "R":
            left, bar, right = rest.partition(" |")
            if not bar:
                self.errors.append("line %d: R without |" % lineno); return
            table, _, key = left.partition(" ")
            table = unesc(table)
            vals = [unesc(x) for x in right.split(" ") if x != ""]
            d = self.rows.setdefault(table, {})
            if key in d:
                self.warnings.append("line %d: duplicate key %s/%s" % (lineno, table, key))
                n = 2
                while "%s#%d" % (key, n) in d:
                    n += 1
                key = "%s#%d" % (key, n)
            d[key] = vals
            sc = self.schemas.get(table)
            if sc and len(sc[1]) != len(vals):
                self.warnings.append("line %d: %s has %d values, schema %d" % (lineno, table, len(vals), len(sc[1])))
        elif tag == "H":
            t = rest.split(" ")
            if len(t) < 4:
                self.errors.append("line %d: bad H" % lineno); return
            kv = dict(x.partition("=")[::2] for x in t[2:])
            self.hashes.setdefault(unesc(t[0]), {})[t[1]] = (kv.get("sha256"), int(kv.get("len", "-1")))
        elif tag == "B":
            t = rest.split(" ")
            if len(t) != 4:
                self.errors.append("line %d: bad B" % lineno); return
            w, h, bpp = (int(x) for x in t[2].split("x"))
            enc, _, data = t[3].partition(":")
            raw = b"" if data == "~" else (bytes.fromhex(data) if enc == "hex" else base64.b64decode(data))
            stride = (w * bpp + 7) // 8
            if len(raw) != stride * h:
                self.warnings.append("line %d: bitmap len %d != %d" % (lineno, len(raw), stride * h))
            self.bitmaps.setdefault(unesc(t[0]), {})[t[1]] = (w, h, bpp, raw)
        elif tag == "N":
            self.notes.append(unesc(rest))
        elif tag == "X":
            t = rest.split(" ")
            kv = dict(x.partition("=")[::2] for x in t[1:])
            self.blob_meta[unesc(t[0])] = {"len": int(kv["len"]), "sha256": kv["sha256"], "chunks": int(kv["chunks"])}
            self._chunks[unesc(t[0])] = {}
        elif tag == "XC":
            t = rest.split(" ")
            self._chunks.setdefault(unesc(t[0]), {})[int(t[1])] = t[2] if len(t) > 2 else ""

    def finish(self, end_line, lineno):
        t = end_line.split(" ")
        kv = dict(x.partition("=")[::2] for x in t[2:])
        self.end = {"rows": int(kv.get("rows", "-1")), "crc32": kv.get("crc32", ""), "status": kv.get("status", "?")}
        crc = "%08X" % crc32_lines(self.lines)
        if self.end["rows"] != len(self.lines):
            self.errors.append("END rows=%d but %d lines received" % (self.end["rows"], len(self.lines)))
        if self.end["crc32"].upper() != crc:
            self.errors.append("END crc32=%s but computed %s" % (self.end["crc32"], crc))
        if self.end["status"] != "ok":
            self.errors.append("status=%s" % self.end["status"])
        self._assemble_blobs()

    def _assemble_blobs(self):
        for name, m in self.blob_meta.items():
            ch = self._chunks.get(name, {})
            if len(ch) != m["chunks"]:
                self.errors.append("blob %s: %d/%d chunks" % (name, len(ch), m["chunks"])); continue
            data = b"".join(base64.b64decode(ch[i]) for i in range(m["chunks"]))
            if len(data) != m["len"] or hashlib.sha256(data).hexdigest() != m["sha256"]:
                self.errors.append("blob %s: length/sha256 mismatch" % name); continue
            self.blobs[name] = data

    def text(self):
        out = list(self.lines)
        if self.end:
            out.append("END %s rows=%d crc32=%s status=%s" % (self.probe_tok, self.end["rows"], self.end["crc32"], self.end["status"]))
        return "\n".join(out) + "\n"

    def summary(self):
        nrows = sum(len(v) for v in self.rows.values())
        nh = sum(len(v) for v in self.hashes.values())
        nb = sum(len(v) for v in self.bitmaps.values())
        s = "%s variant=%s tag=%s lines=%d R=%d H=%d B=%d X=%d status=%s" % (
            self.probe, self.variant, self.tag or "-", len(self.lines), nrows, nh, nb, len(self.blobs), self.status)
        return s + ("  OK" if self.ok else "  BAD: " + "; ".join(self.errors))


def _read_text(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    return data.decode("latin-1")


def parse_text(text, src="<text>"):
    """Return (recordings, foreign_lines). Lines are LF/CRLF tolerant; CR stripped."""
    recs, foreign, cur = [], [], None
    for i, line in enumerate(text.split("\n"), 1):
        line = line.rstrip("\r")
        if line.startswith("REC "):
            if cur is not None:
                cur.errors.append("truncated: new REC at line %d before END" % i)
                recs.append(cur)
            cur = Recording(src, i, line)
            continue
        if cur is None:
            if line:
                foreign.append(line)
            continue
        tag, _, rest = line.partition(" ")
        probe_tok, _, rest2 = rest.partition(" ")
        if tag == "END" and probe_tok == cur.probe_tok:
            cur.finish(line, i)
            recs.append(cur)
            cur = None
        elif probe_tok == cur.probe_tok and re.fullmatch(r"[A-Z][A-Z0-9]*", tag) and tag != "REC":
            cur.lines.append(line)            # part of the CRC even if the tag is newer than this reader
            if tag not in TAGS:
                cur.warnings.append("line %d: unknown tag %s (ignored)" % (i, tag))
                continue
            try:
                cur.add(tag, rest2, i)
            except Exception as e:  # malformed line: keep going, mark the recording bad
                cur.errors.append("line %d: %s: %s" % (i, type(e).__name__, e))
        else:
            if line:
                foreign.append(line)
                cur.warnings.append("line %d: foreign line inside recording: %.60s" % (i, line))
    if cur is not None:
        cur.errors.append("truncated: no END")
        recs.append(cur)
    return recs, foreign


def load(path):
    """Recordings from a file or directory."""
    if os.path.isdir(path):
        out = []
        for root, _, files in sorted(os.walk(path)):
            for f in sorted(files):
                if f.endswith((".rec", ".rec.gz")):
                    out += parse_text(_read_text(os.path.join(root, f)), os.path.join(root, f))[0]
        return out
    return parse_text(_read_text(path), path)[0]


# ------------------------------------------------------------------ writer (Python side)
class Writer:
    """Write a recording in the same format from Python (host-derived data)."""

    def __init__(self, fh, probe, version="1", variant="host", tag=""):
        self.fh, self.p = fh, esc(probe)
        self.n, self.crc, self.done = 0, 0, False
        self._line("REC %d %s v=%s variant=%s tag=%s" % (FORMAT, self.p, esc(version), esc(variant), esc(tag)))

    def _line(self, s):
        self.crc = zlib.crc32(s.encode("latin-1") + b"\n", self.crc)
        self.n += 1
        self.fh.write(s + "\n")

    def env(self, k, v):
        self._line("ENV %s %s %s" % (self.p, esc(k), esc(str(v))))

    def schema(self, table, idcols, cols):
        self._line(("S %s %s %s | %s" % (self.p, esc(table), ";".join(idcols) or "~", " ".join(cols))).rstrip())

    @staticmethod
    def _val(v):
        if v is None:
            return "!"
        if isinstance(v, float):
            return "%.6g" % v
        return esc(str(v))

    def row(self, table, key, vals):
        self._line(("R %s %s %s | %s" % (self.p, esc(table), make_key(key), " ".join(self._val(v) for v in vals))).rstrip())

    def hash(self, table, key, data):
        self._line("H %s %s %s sha256=%s len=%d" % (self.p, esc(table), make_key(key), hashlib.sha256(data).hexdigest()[:16], len(data)))

    def bitmap(self, table, key, w, h, bpp, data):
        enc = ("hex:" + (data.hex() or "~")) if len(data) <= 64 else "b64:" + base64.b64encode(data).decode()
        self._line("B %s %s %s %dx%dx%d %s" % (self.p, esc(table), make_key(key), w, h, bpp, enc))

    def note(self, text):
        self._line("N %s %s" % (self.p, esc(text)))

    def blob(self, name, data, chunk=3072):
        n = (len(data) + chunk - 1) // chunk
        self._line("X %s %s len=%d sha256=%s chunks=%d" % (self.p, esc(name), len(data), hashlib.sha256(data).hexdigest(), n))
        for i in range(n):
            self._line("XC %s %s %d %s" % (self.p, esc(name), i, base64.b64encode(data[i * chunk:(i + 1) * chunk]).decode()))

    def end(self, status="ok"):
        if not self.done:
            self.fh.write("END %s rows=%d crc32=%08X status=%s\n" % (self.p, self.n, self.crc & 0xFFFFFFFF, status))
            self.done = True


# ------------------------------------------------------------------ commands
def cmd_validate(a):
    bad = 0
    for p in a.files:
        recs = load(p)
        if not recs:
            print("%s: no recordings" % p); bad += 1
        for r in recs:
            print("%s:%d %s" % (r.src, r.lineno, r.summary()))
            if a.verbose:
                for w in r.warnings:
                    print("    warn:", w)
            bad += not r.ok
    return 1 if bad else 0


def i7p_lines(text):
    return [l.rstrip("\r") for l in text.split("\n") if l.startswith(("I7P ", "I7VM", "I7VMRUN", "I7DPI"))]


def cmd_split(a):
    text = _read_text(a.serial)
    recs, foreign = parse_text(text, a.serial)
    tag = a.tag or "run"
    os.makedirs(a.outdir, exist_ok=True)
    bad, written = 0, []
    for r in recs:
        name = re.sub(r"[^A-Za-z0-9_.+-]", "_", r.probe)
        fn = tag + ("" if r.variant in ("base", "") else "." + re.sub(r"[^A-Za-z0-9_.+-]", "_", r.variant)) + ".rec"
        d = os.path.join(a.outdir, name)
        os.makedirs(d, exist_ok=True)
        body = r.text()
        if not r.ok:
            bad += 1
            fn += ".bad"
            if not a.keep_bad:
                print("BAD  %s: %s (not written)" % (r.probe, "; ".join(r.errors)))
                continue
        path = os.path.join(d, fn)
        if a.gzip_over and len(body) > a.gzip_over and r.ok:
            path += ".gz"
            with open(path, "wb") as f:
                f.write(gzip.compress(body.encode("latin-1"), mtime=0))
        else:
            with open(path, "w", newline="\n", encoding="latin-1") as f:
                f.write(body)
        written.append(path)
        print("%s %s -> %s" % ("OK  " if r.ok else "BAD ", r.summary(), path))
    # runner markers: per-probe rc / timeout
    marks = i7p_lines(text)
    for m in marks:
        if m.startswith("I7P END") and ("timeout=1" in m or " rc=0 " not in m + " "):
            print("WARN runner:", m)
    if a.runlog:
        os.makedirs(os.path.dirname(a.runlog) or ".", exist_ok=True)
        with open(a.runlog, "w", newline="\n") as f:
            f.write("# serial %s sha256=%s bytes=%d\n" % (os.path.basename(a.serial), hashlib.sha256(text.encode("latin-1")).hexdigest(), len(text)))
            for p in written:
                f.write("# wrote %s\n" % p)
            for m in marks:
                f.write(m + "\n")
    if not recs:
        print("no recordings found in", a.serial)
        return 1
    return 1 if bad else 0


def _tables(r, glob):
    return [t for t in sorted(set(r.rows) | set(r.hashes) | set(r.bitmaps)) if fnmatch.fnmatch(t, glob or "*")]


def cmd_show(a):
    for r in load(a.file):
        print("== " + r.summary())
        if a.env:
            for k, v in r.env.items():
                print("  ENV %-36s %s" % (k, v))
        for t in _tables(r, a.table):
            names = r.colnames(t)
            for k, vals in r.rows.get(t, {}).items():
                nm = names if names and len(names) == len(vals) else ["v%d" % i for i in range(len(vals))]
                print("  %s [%s] %s" % (t, unesc_key(k), " ".join("%s=%s" % (n, "!" if v is None else v) for n, v in zip(nm, vals))))
            for k, (h, n) in r.hashes.get(t, {}).items():
                print("  %s [%s] sha256=%s len=%d" % (t, unesc_key(k), h, n))
            for k, (w, h, bpp, raw) in r.bitmaps.get(t, {}).items():
                print("  %s [%s] bitmap %dx%dx%d %d bytes sha=%s" % (t, unesc_key(k), w, h, bpp, len(raw), hashlib.sha256(raw).hexdigest()[:16]))
        for n in r.notes:
            print("  NOTE", n)
    return 0


def unesc_key(k):
    return ", ".join("%s=%s" % kv for kv in parse_key(k.split("#")[0]).items()) + (("  #" + k.split("#")[1]) if "#" in k else "")


def cmd_csv(a):
    out = open(a.o, "w", newline="") if a.o else sys.stdout
    w = csv.writer(out)
    for r in load(a.file):
        for t in _tables(r, a.table):
            if t in r.rows:
                names = r.colnames(t)
                ids = r.schemas.get(t, ([], []))[0]
                rows = list(r.rows[t].items())
                if not ids and rows:
                    ids = list(parse_key(rows[0][0].split("#")[0]).keys())
                width = max((len(v) for _, v in rows), default=0)
                nm = names if names and len(names) == width else ["v%d" % i for i in range(width)]
                w.writerow(["probe", "variant", "tag", "table"] + ids + nm)
                for k, vals in rows:
                    kd = parse_key(k.split("#")[0])
                    w.writerow([r.probe, r.variant, r.tag, t] + [kd.get(i, "") for i in ids] + ["!" if v is None else v for v in vals])
            if t in r.hashes:
                w.writerow(["probe", "variant", "tag", "table", "key", "sha256_16", "len"])
                for k, (h, n) in r.hashes[t].items():
                    w.writerow([r.probe, r.variant, r.tag, t, unesc_key(k), h, n])
    if a.o:
        out.close()
    return 0


def _pixels(w, h, bpp, raw):
    stride = (w * bpp + 7) // 8
    px = []
    for y in range(h):
        for x in range(w):
            if bpp >= 8:
                px.append(raw[y * stride + x * (bpp // 8)] if y * stride + x * (bpp // 8) < len(raw) else 0)
            else:
                bit = x * bpp
                b = raw[y * stride + bit // 8] if y * stride + bit // 8 < len(raw) else 0
                q = (b >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1)
                px.append(q * 255 // ((1 << bpp) - 1))
    return px


def cmd_diff(a):
    A, B = load(a.a), load(a.b)
    tol_default, tol = 0.0, {}
    for t in a.tol or []:
        if "=" in t:
            n, _, v = t.partition("=")
            tol[n] = float(v)
        else:
            tol_default = float(t)
    ign = a.ignore or []

    def ignored(table, col):
        return any(fnmatch.fnmatch(col, g) or fnmatch.fnmatch("%s.%s" % (table, col), g) for g in ign)

    def tolerance(table, col):
        return tol.get("%s.%s" % (table, col), tol.get(col, tol_default))

    bmap = {r.ident(): r for r in B}
    diffs, missing, extra, within = [], [], [], 0
    for ra in A:
        rb = bmap.get(ra.ident())
        if rb is None:
            missing.append((ra.probe, ra.variant, "*", "(whole recording)"))
            continue
        if a.env:
            for k in sorted(set(ra.env) | set(rb.env)):
                if ra.env.get(k) != rb.env.get(k) and not ignored("ENV", k):
                    diffs.append((ra.probe, "ENV", k, "value", ra.env.get(k), rb.env.get(k), "", "env"))
        for t in sorted(set(ra.rows) | set(rb.rows)):
            if not fnmatch.fnmatch(t, a.table or "*"):
                continue
            ta, tb = ra.rows.get(t, {}), rb.rows.get(t, {})
            names = ra.colnames(t) or rb.colnames(t)
            for k in ta:
                if k not in tb:
                    missing.append((ra.probe, ra.variant, t, k)); continue
                va, vb = ta[k], tb[k]
                n = max(len(va), len(vb))
                nm = names if names and len(names) == n else ["v%d" % i for i in range(n)]
                for i in range(n):
                    col = nm[i]
                    if ignored(t, col):
                        continue
                    x = typed(va[i]) if i < len(va) else "<absent>"
                    y = typed(vb[i]) if i < len(vb) else "<absent>"
                    if x == y:
                        continue
                    if isinstance(x, (int, float)) and isinstance(y, (int, float)) and not isinstance(x, bool):
                        d = y - x
                        rel = abs(d) / abs(x) if x else float("inf")
                        if abs(d) <= tolerance(t, col) or (a.rel_tol and rel <= a.rel_tol):
                            within += 1; continue
                        diffs.append((ra.probe, t, k, col, x, y, d, "num"))
                    else:
                        diffs.append((ra.probe, t, k, col, x, y, "", "text"))
            for k in tb:
                if k not in ta:
                    extra.append((ra.probe, ra.variant, t, k))
        for t in sorted(set(ra.hashes) | set(rb.hashes)):
            if not fnmatch.fnmatch(t, a.table or "*"):
                continue
            ta, tb = ra.hashes.get(t, {}), rb.hashes.get(t, {})
            for k in ta:
                if k not in tb:
                    missing.append((ra.probe, ra.variant, t, k))
                elif ta[k] != tb[k] and not ignored(t, "sha256"):
                    diffs.append((ra.probe, t, k, "sha256", "%s/%d" % ta[k], "%s/%d" % tb[k], "", "hash"))
            extra += [(ra.probe, ra.variant, t, k) for k in tb if k not in ta]
        for t in sorted(set(ra.bitmaps) | set(rb.bitmaps)):
            if not fnmatch.fnmatch(t, a.table or "*"):
                continue
            ta, tb = ra.bitmaps.get(t, {}), rb.bitmaps.get(t, {})
            for k in ta:
                if k not in tb:
                    missing.append((ra.probe, ra.variant, t, k)); continue
                (w1, h1, b1, r1), (w2, h2, b2, r2) = ta[k], tb[k]
                if (w1, h1, b1) != (w2, h2, b2):
                    diffs.append((ra.probe, t, k, "shape", "%dx%dx%d" % (w1, h1, b1), "%dx%dx%d" % (w2, h2, b2), "", "bitmap")); continue
                if r1 == r2:
                    continue
                p1, p2 = _pixels(w1, h1, b1, r1), _pixels(w2, h2, b2, r2)
                dd = [abs(x - y) for x, y in zip(p1, p2)]
                mx, cnt = max(dd), sum(1 for x in dd if x > a.bitmap_tol)
                if cnt == 0:
                    within += 1; continue
                diffs.append((ra.probe, t, k, "pixels", "", "", "%d px differ (max |d|=%d)" % (cnt, mx), "bitmap"))
            extra += [(ra.probe, ra.variant, t, k) for k in tb if k not in ta]
    aset = {r.ident() for r in A}
    extra += [(r.probe, r.variant, "*", "(whole recording)") for r in B if r.ident() not in aset]
    for d in diffs:
        print("DIFF %s %s [%s] %s: %s -> %s %s" % (d[0], d[1], unesc_key(d[2]) if d[1] != "ENV" else d[2], d[3], d[4], d[5], d[6]))
    for m in missing:
        print("MISSING %s/%s %s [%s]" % (m[0], m[1], m[2], unesc_key(m[3]) if m[3][0] != "(" else m[3]))
    for m in extra:
        print("EXTRA %s/%s %s [%s]" % (m[0], m[1], m[2], unesc_key(m[3]) if m[3][0] != "(" else m[3]))
    nenv = sum(1 for d in diffs if d[7] == "env")
    real = len(diffs) - nenv
    print("SUMMARY diffs=%d (env %d, informational) within_tol=%d missing=%d extra=%d" % (real, nenv, within, len(missing), len(extra)))
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["probe", "table", "key", "field", "a", "b", "diff", "kind"])
            for d in diffs:
                w.writerow([d[0], d[1], unesc_key(d[2]) if d[1] != "ENV" else d[2], d[3], d[4], d[5], d[6], d[7]])
            for m in missing:
                w.writerow([m[0], m[2], m[3], "", "", "", "", "missing_in_b"])
            for m in extra:
                w.writerow([m[0], m[2], m[3], "", "", "", "", "extra_in_b"])
    fail = real > 0 or (missing and not a.allow_missing) or (extra and not a.allow_extra)
    return 1 if fail else 0


def cmd_legacy(a):
    """TAG key=val lines (tools/compare_metrics.py input). TAG = upper-cased table; id cols come first."""
    for r in load(a.file):
        for t in sorted(r.rows):
            tag = re.sub(r"[^A-Z0-9_]", "_", t.upper())
            if not re.match(r"[A-Z]", tag):
                tag = "T" + tag
            for kd, vd in r.row_dicts(t):
                parts = ["%s=%s" % (k, v) for k, v in kd.items()] + ["%s=%s" % (k, "!" if v is None else v) for k, v in vd.items()]
                print(tag, " ".join(parts))
    return 0


def cmd_blobs(a):
    os.makedirs(a.outdir, exist_ok=True)
    rc = 0
    for r in load(a.file):
        for n, data in r.blobs.items():
            p = os.path.join(a.outdir, re.sub(r"[^A-Za-z0-9_.+-]", "_", n))
            with open(p, "wb") as f:
                f.write(data)
            print("%s %d bytes sha256=%s" % (p, len(data), hashlib.sha256(data).hexdigest()))
        rc |= not r.ok
    return rc


def cmd_bitmaps(a):
    ramp = " .:-=+*#%@"
    for r in load(a.file):
        for t in sorted(r.bitmaps):
            for k, (w, h, bpp, raw) in r.bitmaps[t].items():
                px = _pixels(w, h, bpp, raw) if bpp <= 8 else None
                if a.outdir and px is not None:
                    os.makedirs(a.outdir, exist_ok=True)
                    name = re.sub(r"[^A-Za-z0-9_.+-]", "_", "%s_%s_%s" % (r.probe, t, unesc_key(k)))[:150] + ".pgm"
                    with open(os.path.join(a.outdir, name), "wb") as f:
                        f.write(b"P5\n%d %d\n255\n" % (w, h) + bytes(px))
                if a.ascii and px is not None:
                    print("%s [%s] %dx%dx%d" % (t, unesc_key(k), w, h, bpp))
                    for y in range(h):
                        print("  |" + "".join(ramp[(255 - px[y * w + x]) * (len(ramp) - 1) // 255] for x in range(w)) + "|")
    return 0


def cmd_selftest(a):
    fails = []

    def check(c, msg):
        if not c:
            fails.append(msg)

    check(zlib.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926, "crc32 check value")
    for s in ["", "Segoe UI", "a;b|c~d!e%f", "MS Shell Dlg 2", "微软雅黑", "x=y"]:
        check(unesc(esc(s)) == s, "escape round trip %r" % s)
    check(esc("Segoe UI") == "Segoe%20UI", "esc space")
    check(esc("") == "~" and unesc("!") is None, "empty / NA tokens")
    # Python writer -> parser
    buf = io.StringIO()
    w = Writer(buf, "selftest", "1", tag="t1")
    w.env("os.version", "10.0.19045")
    w.schema("tm", ["face", "req_h"], ["got", "h:px", "a:px"])
    w.row("tm", {"face": "Segoe UI", "req_h": -12}, ["Segoe UI", 15, 12])
    w.row("tm", {"face": "Tahoma", "req_h": -11}, ["Tahoma", 13, None])
    w.hash("cw", {"face": "Segoe UI"}, b"abc")
    w.bitmap("g", {"ch": "A"}, 4, 2, 8, bytes(range(8)))
    w.bitmap("g", {"ch": "B"}, 30, 3, 8, bytes(90))
    w.blob("b1", os.urandom(7000))
    w.note("hello; world")
    w.end()
    txt = buf.getvalue()
    recs, _ = parse_text(txt)
    check(len(recs) == 1 and recs[0].ok, "writer round trip ok: %s" % (recs[0].errors if recs else "none"))
    r = recs[0]
    check(r.rows["tm"]["face=Segoe%20UI;req_h=-12"] == ["Segoe UI", "15", "12"], "row values")
    check(r.rows["tm"]["face=Tahoma;req_h=-11"][2] is None, "NA value")
    check(r.hashes["cw"]["face=Segoe%20UI"][0] == hashlib.sha256(b"abc").hexdigest()[:16], "hash row")
    check(r.bitmaps["g"]["ch=A"][3] == bytes(range(8)), "hex bitmap")
    check(len(r.blobs.get("b1", b"")) == 7000, "blob")
    check(r.notes == ["hello; world"], "note")
    check(r.env["os.version"] == "10.0.19045", "env")
    # corruption / truncation / noise
    lines = txt.split("\n")
    bad = "\n".join(l.replace("15", "16") if l.startswith("R ") else l for l in lines)
    check(not parse_text(bad)[0][0].ok, "crc catches altered value")
    trunc = "\n".join(lines[:5])
    check(parse_text(trunc)[0][0].status == "truncated", "missing END detected")
    noisy = "I7VM: logon\r\n" + txt.replace("\n", "\r\n", 3) + "I7P END name=x rc=0\n"
    rr = parse_text(noisy)[0]
    check(len(rr) == 1 and rr[0].ok, "CRLF + markers tolerated")
    two = txt + txt.replace("selftest", "selftest2")
    check([x.probe for x in parse_text(two)[0]] == ["selftest", "selftest2"], "two recordings")
    # C writer output under wine, if a recording produced by tools/wine_probe.sh is given
    if a.c_output:
        for p in a.c_output:
            for rc in load(p):
                check(rc.ok, "C recording %s: %s" % (p, rc.errors))
                if rc.probe == "hello":
                    check("tm" in rc.rows and len(rc.rows["tm"]) == 16, "hello tm rows")
                    check("glyph" in rc.bitmaps, "hello bitmaps")
    for f in fails:
        print("FAIL", f)
    print("SELFTEST %s (%d failures)" % ("OK" if not fails else "FAILED", len(fails)))
    return 1 if fails else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("validate"); p.add_argument("files", nargs="+"); p.add_argument("-v", "--verbose", action="store_true"); p.set_defaults(f=cmd_validate)
    p = sp.add_parser("split"); p.add_argument("serial"); p.add_argument("--outdir", required=True); p.add_argument("--tag")
    p.add_argument("--keep-bad", action="store_true"); p.add_argument("--gzip-over", type=int, default=4 << 20, help="gzip recordings larger than N bytes (0=never)")
    p.add_argument("--runlog", help="write runner markers + serial sha256 here"); p.set_defaults(f=cmd_split)
    p = sp.add_parser("show"); p.add_argument("file"); p.add_argument("--table"); p.add_argument("--env", action="store_true"); p.set_defaults(f=cmd_show)
    p = sp.add_parser("csv"); p.add_argument("file"); p.add_argument("--table"); p.add_argument("-o"); p.set_defaults(f=cmd_csv)
    p = sp.add_parser("diff"); p.add_argument("a"); p.add_argument("b"); p.add_argument("--tol", action="append")
    p.add_argument("--rel-tol", type=float, default=0.0); p.add_argument("--ignore", action="append"); p.add_argument("--table")
    p.add_argument("--bitmap-tol", type=int, default=0); p.add_argument("--env", action="store_true")
    p.add_argument("--allow-missing", action="store_true"); p.add_argument("--allow-extra", action="store_true"); p.add_argument("--csv")
    p.set_defaults(f=cmd_diff)
    p = sp.add_parser("legacy"); p.add_argument("file"); p.set_defaults(f=cmd_legacy)
    p = sp.add_parser("blobs"); p.add_argument("file"); p.add_argument("--outdir", required=True); p.set_defaults(f=cmd_blobs)
    p = sp.add_parser("bitmaps"); p.add_argument("file"); p.add_argument("--outdir"); p.add_argument("--ascii", action="store_true"); p.set_defaults(f=cmd_bitmaps)
    p = sp.add_parser("selftest"); p.add_argument("c_output", nargs="*", help="recordings made by the C writer to validate too"); p.set_defaults(f=cmd_selftest)
    a = ap.parse_args(argv)
    try:
        return a.f(a)
    except BrokenPipeError:
        return 0


if __name__ == "__main__":
    sys.exit(main())
