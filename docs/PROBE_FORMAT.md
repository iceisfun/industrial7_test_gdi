# Probe record format (REC v1)

Probes in `probes/<name>/` are **recorders**: they measure Win10 GDI/USER/comctl32 behaviour and write
line-oriented, versioned, diffable text. Writer: `common/rec.c` / `common/rec.h` (C, no dependencies beyond the
Win32 system DLLs). Reader/tooling: `tools/rec.py` (Python 3 stdlib). Python writer for host-derived data:
`tools/rec.py` `Writer` class (same bytes, same CRC).

## 1. Lines

One record per line, ASCII only, LF line ends (CR is ignored by the reader, because cmd/serial paths can add it).
Fields are separated by exactly one space. Every field that carries data is a **token** (section 2).

| tag | layout | meaning |
|---|---|---|
| `REC` | `REC 1 <probe> v=<probe version> variant=<variant> tag=<tag>` | start of a recording; `1` = format version |
| `ENV` | `ENV <probe> <key> <value>` | environment fact (OS, DPI, locale, fonts, metrics; section 4) |
| `S` | `S <probe> <table> <idcol;idcol...> \| <col[:unit]> <col[:unit]> ...` | schema of table: key column names, then value column names (optional unit after `:`) |
| `R` | `R <probe> <table> <k=v;k=v...> \| <val> <val> ...` | one measured row; the key identifies it, values are positional per `S` |
| `H` | `H <probe> <table> <key> sha256=<16 hex> len=<N>` | a buffer too big to keep (e.g. 95 char widths, a glyph outline): first 64 bits of its SHA-256 and its byte length |
| `B` | `B <probe> <table> <key> <w>x<h>x<bpp> <enc>:<data>` | tiny bitmap; `enc` = `hex` (<= 64 bytes) or `b64`; `~` = empty |
| `N` | `N <probe> <token>` | free note (e.g. `crash code=...`, `skipped: no such font`) |
| `X` | `X <probe> <name> len=<N> sha256=<64 hex> chunks=<K>` | binary blob header (full SHA-256) ... |
| `XC` | `XC <probe> <name> <seq> <b64>` | ... followed by K chunks of <= 3072 raw bytes (4096 base64 chars), seq 0..K-1 |
| `END` | `END <probe> rows=<N> crc32=<8 HEX> status=<ok\|fail\|crash>` | trailer |

Notes:
- `<probe>` is repeated on every line so a recording can be cut out of a mixed stream (serial.log holds
  `I7VM:`/`I7P`/`ver` lines and several probes). The reader only accepts lines whose tag is one of the above
  and whose second field equals the probe of the open `REC`; anything else inside a recording is a *foreign
  line* (a warning; excluded from the CRC).
- **Trailer**: `rows` = number of lines from `REC` through the last line before `END` (REC included).
  `crc32` = CRC-32 (IEEE 802.3, the zlib/`binascii.crc32` one) over exactly those lines, each as its bytes plus
  one `\n`. A missing `END`, a wrong count or a wrong CRC means truncation/corruption over serial: the
  recording is rejected by `rec.py split`/`validate`.
- `status=crash` is written by the unhandled-exception filter in `rec.c` (preceded by an `N ... crash code=`
  line). `fail` = the probe returned non-zero through `rec_finish(rc)`. Killed by timeout = no `END`.
- **Keys**: `name=value` pairs joined by `;`, in the probe's fixed order. Names are identifiers
  (`[A-Za-z0-9_.]`); values are tokens. A key must be unique within (probe, table); the reader renames
  duplicates `key#2` and warns. The empty key is `~`. Rows are always matched by key, never by position.
- **Values** after ` | ` are positional, named by the table's `S` line. Integers are decimal (`-11`),
  flags/bitfields hex with `0x` (`0x27`), floats `%.6g`. `!` = call failed / not available.
- Order of lines is the probe's deterministic order (no timestamps or pointers in R/H/B lines), so two runs of
  the same probe on the same system produce byte-identical R/H/B lines; only some `ENV` values (time, uptime,
  exe hash) change. `rec.py diff` ignores ENV unless `--env`.

## 2. Tokens and escaping

A token is one or more bytes from `0x21..0x7E`, except that `%` `;` `|` `~` `!` and every byte outside that range
(space, control bytes, UTF-8 bytes of non-ASCII text) are written as `%XX` (uppercase hex). Text is UTF-8 before
escaping (wide strings go through `WideCharToMultiByte(CP_UTF8)`). Two reserved whole tokens: `~` = empty string,
`!` = not available. `=` and `,` are not escaped (a key splits on `;`, then at the first `=`).

    Segoe UI        -> Segoe%20UI
    MS Shell Dlg 2  -> MS%20Shell%20Dlg%202
    ""              -> ~
    微软雅黑        -> %E5%BE%AE%E8%BD%AF%E9%9B%85%E9%BB%91

## 3. Units and conventions (write them into the S line)

- Use `:px` for device pixels, `:lu` for logical units (differs from px only under a non-MM_TEXT mapping mode or
  a world transform; say which in a `N` line or in the key, e.g. `map=MM_TEXT`), `:pt` points, `:du` dialog
  units, `:fu` font design units (with `upem` recorded), `:ms` milliseconds.
- Font heights: record the request exactly as passed (`req_h=-12` is lfHeight = -12 = **character (em) height**
  12 px; `req_h=12` is a **cell height** of 12 px) and the results by their TEXTMETRIC names: `h` = tmHeight
  (cell = ascent + descent, includes internal leading), `il` = tmInternalLeading, `el` = tmExternalLeading.
- Put every condition that can change a value into the key: face, req_h, weight, italic, charset, quality, DC kind
  (`dc=screen|mem1bpp|dib32`), mapping mode, DPI awareness when the probe switches it per row. Process-wide
  conditions (system DPI, manifest variant, comctl32 version) are in `REC`/`ENV`.

## 4. ENV block written by `rec_init` (`--env full`, the default)

`--env min` writes groups 1-5, `--env none` nothing. Keys (values are tokens):

1. probe: `probe.exe`, `probe.exe_sha256` (first 16 hex of the exe's SHA-256), `probe.exe_size`, `probe.version`,
   `probe.format`, `probe.bits` (64/32), `probe.args`.
2. OS/host/time: `os.version` (RtlGetVersion, e.g. `10.0.19045`), `os.ubr`, `os.display_version`, `os.product`,
   `os.edition`, `os.buildlab`, `host.computer`, `host.user`, `host.session`, `host.elevated`, `time.local`,
   `time.utc`, `time.tz_bias_min`, `time.tz_name`, `time.tz_state`, `time.uptime_ms`.
3. locale: `locale.user`/`locale.system` (names), `locale.lcid_user|system|thread`, `locale.ui_user|system`,
   `locale.acp`, `locale.oemcp`, `locale.kbd`.
4. DPI/display: `dpi.system` (GetDpiForSystem), `dpi.awareness` (0 unaware, 1 system, 2 per-monitor; from
   GetThreadDpiAwarenessContext), `dpi.context`, `dpi.process_aware`, `dpi.logpixels` (screen DC LOGPIXELSX x Y,
   as seen by this process), `display.res`, `display.desktopres`, `display.bpp`, `display.vrefresh`,
   `display.size_mm`, `display.mode`, `display.monitors`, and the registry inputs `dpi.reg_logpixels`,
   `dpi.reg_win8dpiscaling`, `dpi.reg_applieddpi` (HKCU), `dpi.reg_hw_logpixels` (HKLM hardware profile).
5. `comctl32.version` / `comctl32.path` (5.82 without the v6 manifest, 6.x with it), `theme.active`.
6. full only: `sm.<NAME>` (60 GetSystemMetrics values, in this process's DPI context), `ncm.metrics` and
   `ncm.caption|smcaption|menu|status|message` (NONCLIENTMETRICS LOGFONTs: `h,w,wt,it,cs,q,pf,face`), `icm.*`,
   `spi.fontsmoothing*`, `spi.cleartype`, `stock.<OBJECT>` (LOGFONT of the 7 stock fonts), `fonts.dir`
   (file count, bytes, SHA-256/16 of the sorted lowercase `name<TAB>size` list of %WINDIR%\Fonts),
   `fonts.file.<name>` (size + SHA-256/16 of key UI font files: segoeui, tahoma, micross, arial, cour, the .fon set),
   `reg.fontsubstitutes` / `reg.fonts` / `reg.systemlink` (count + SHA-256/16 of the sorted values),
   `reg.fontsubst.<face>` (the substitution of MS Shell Dlg, MS Shell Dlg 2, Helv, ...), `reg.gre_init.*`
   (GRE_Initialize: which .fon files back System/Fixedsys/Terminal), `reg.fontdpi.*`.

## 5. Writing a probe (C)

    #include "rec.h"
    int main(int argc, char **argv) {
        rec_init(&argc, argv, "tm_basic", "1");        /* REC + ENV; strips --out/--com1/--tag/--env/--variant */
        rec_schema("tm", "face req_h", "got h:px a:px");
        rk k; rv v;
        rk_reset(&k); rk_s(&k, "face", "Segoe UI"); rk_i(&k, "req_h", -12);
        rv_reset(&v); rv_s(&v, "Segoe UI"); rv_i(&v, 15); rv_i(&v, 12);
        rec_row("tm", &k, &v);
        rec_hash("cw", &k, widths, sizeof widths);     /* H line */
        rec_bitmap("glyph", &k, w, h, 8, grey, n);     /* B line; see rec_dib_create / rec_gray_from_dib */
        return rec_finish(0);                          /* END line */
    }

Sink options (stripped from argv by `rec_init`): default stdout (raw `WriteFile`, no CRT newline translation);
`--out FILE`; `--com1` (CreateFile `\\.\COM1`; only when nothing else holds the port: under `tools/vm_run.sh`
use the default, the runner gives the probe COM1 as stdout). `--tag T` goes into `REC`. `--variant V` overrides the
variant, which otherwise is the part of the exe name after the first dot (`hello.v6pm2.exe` -> `v6pm2`, `hello.exe`
-> `base`). The writer buffers 64 KB and flushes at `rec_init`, when full, and at `END`; call `rec_flush()` before
anything that may hang. Stay deterministic: fixed iteration order, no addresses or times in R rows.

Size guidance: R rows for numbers; `H` for any array you only need to compare (char widths of a range, ABC widths,
kerning pairs, glyph outlines) and keep the full array only for the subsets you need to read; `B` for small
greyscale renders (8bpp, or 4bpp to halve the size: `rec_gray_from_dib(..., 4, ...)`); `X` for whole binaries
(font files, large tables) - they go to a gitignored place, see `data/README.md`.

## 6. tools/rec.py

    rec.py validate FILE...            rows/crc/status per recording (.rec, .rec.gz, serial.log, directory)
    rec.py split serial.log --outdir data/win10 --tag dpi96 [--runlog F] [--keep-bad] [--gzip-over BYTES]
    rec.py show FILE [--table GLOB] [--env]
    rec.py csv FILE [--table GLOB] [-o out.csv]
    rec.py diff A B [--tol N] [--tol COL=N] [--tol TABLE.COL=N] [--rel-tol R] [--ignore GLOB]
                    [--table GLOB] [--bitmap-tol N] [--env] [--allow-missing] [--allow-extra] [--csv OUT]
    rec.py legacy FILE                 TAG key=val lines for tools/compare_metrics.py (docs/COMPARE.md)
    rec.py blobs FILE --outdir DIR     extract X blobs (verified)
    rec.py bitmaps FILE [--outdir DIR] [--ascii]
    rec.py selftest [C-written recordings...]

`diff` matches recordings by (probe, variant), rows by (table, key); numeric columns compare with tolerances,
text exactly, hashes exactly, bitmaps per pixel (`--bitmap-tol` grey levels). Exit 0 = equal within tolerance,
1 = differences/missing rows. Python API: `rec.load(path) -> [Recording]` with `.env`, `.schemas`,
`.rows[table][key] -> [values]`, `.row_dicts(table)`, `.hashes`, `.bitmaps`, `.blobs`, `.notes`, `.ok`, `.errors`.

## 7. Versioning

`REC 1` is this document. A change that alters how an existing line parses bumps the number; adding a new tag or new
ENV keys does not (readers ignore unknown tags inside a recording as foreign lines). A probe changes its own
`v=` whenever its rows' meaning changes (new columns go at the end of an `S` line, or into a new table).
