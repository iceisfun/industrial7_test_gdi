# Comparing a recording against the Win10 data

Tools: `tools/compare_metrics.py` (stdlib only), `scripts/run_all.ps1` / `scripts/run_all.sh` (collect recs on any host),
`tools/make_compare_fixtures.py` + `tests/compare_fixtures/` (self-test). Purpose: run the same probes on another
environment (Industrial7 gdi32 with D12 on or off, a candidate font set, another Windows build) and get a ranked,
grouped diff against the Win10 22H2 (19045.2965) reference, with guesses at the root cause.

## Workflow

    # 1. on the other environment (any Windows host, or Industrial7's runner)
    powershell -ExecutionPolicy Bypass -File scripts\run_all.ps1 -Dir build -Out recs\i7-d12on
    scripts/run_all.sh -d build -o recs/i7-d12on               # Git-Bash/MSYS; PROBE_RUNNER=<prefix> to wrap each exe
    # 2. compare with the Win10 recs (file, serial.log or a directory of *.rec/*.log/*.txt)
    python3 tools/compare_metrics.py WIN10_RECS recs/i7-d12on --md report.md --csv report.csv --tol 0 --ignore max
    # 3. regression-test the tool itself
    python3 tools/compare_metrics.py --selftest

Exit status: 0 = no out-of-tolerance difference and no missing rows, 1 = differences, 2 = bad input. Row-set flags:
`--allow-missing` (reference rows absent in OTHER do not fail), `--allow-extra`.

## Record format the tool understands

Line oriented, ASCII. `TAG [bare ids] key=value key=value ...` where TAG is `[A-Z][A-Z0-9_]*`, as in `tools/probes/tm_hello.c`
(`TM face=Segoe UI req_h=-11 dpi=96 got=Segoe UI h=13 a=11 ...`) and the NPMET style (`NPMET font.segoeui_m12 height=15 ...`).
A value runs to the next ` ident=`, so spaces inside values (face names) are fine. Everything that is not a record is ignored
(banners, `ver` output, `I7VM:` lines, `RUN`/`DONE`/`RC`/`RUNALL`/`ENV`/`BLOB`/`B64` tags, CR characters).

- **Row identity (key)** = TAG + bare non-keyed tokens + the identity fields. Identity fields: `face font dpi weight italic charset quality ch
  char code cp text str name key id idx index case test dc mode stock pitch family style variant sample` and anything starting `req_`, `in_`, `lf`.
  Add more with `--id NAME`; force a field to be treated as a measurement with `--value NAME`. Repeated keys get `#2`, `#3`.
  Rows are matched by key, never by position, so probe order may differ.
- **Measured fields** are everything else: ints, hex (`pf=0x27`, compared as flag bits, diff shown as XOR), floats, text (`got=...`, compared exactly,
  this is how face substitution shows up), and `WxH` pairs (`ext19=106x13` becomes `ext19.cx` and `ext19.cy`). A lone number (`ENUMCOUNT 682`) is field `value`.
- Diff sign is OTHER minus WIN10. Units are whatever the probe records (pixels in the current probes; request `-N` = character height N px,
  `h` = tmHeight cell height).

## Options

| option | meaning |
|---|---|
| `--tol N` | default absolute tolerance for numeric fields (default 0) |
| `--tol FIELD=N`, `--tol FAM.FIELD=N` | per-field / per-family tolerance (repeatable) |
| `--rel-tol R` | also accept `|diff|/|ref| <= R` (numeric, non-flag) |
| `--ignore GLOB` | skip fields (`max`, `ext19.*`, `TM.max`); ignored fields are not compared at all |
| `--weight FIELD=W` | ranking weight; defaults: h/a/d/il/el/cy 3, other `.cx/.cy` 2, `max` 0.5, rest 1 |
| `--family GLOB` | restrict to probe families (tags) |
| `--group-by face,req_h,dpi` | dimensions for the grouped tables and for the "concentrated in" hints (any identity field) |
| `--top N` | rows in the ranked table (default 40; the CSV always has everything) |
| `--left-name`, `--right-name` | labels (default `win10`, `other`) |

Within-tolerance differences are listed as `in_tolerance=1` in the CSV and counted in the summary but never scored or ranked.

## Report (markdown) and CSV

Sections: Summary (verdict, counts); Root-cause hints; Per probe family; Per field (min/max/mean signed diff); Grouped by face / req_h / dpi;
Top-N ranked differences (key, field, both values, diff, relative, score); Missing / extra rows.
Score of a numeric diff = field weight x |diff| (flags: x number of differing bits); a text mismatch or an absent field counts as 10 / 5 x weight; a missing row
as 10. CSV columns: `family,key,face,req_h,dpi,field,<left>,<right>,diff,rel,kind,in_tolerance,score` (+ rows with `kind=missing_in_*`/`extra_in_*`).

## Root-cause hints (INFER: heuristics over the diff pattern, not measurements)

Per (family, field) with out-of-tolerance diffs, in this order of precedence:
1. **systematic offset**: >= 90% of rows differ by one constant ("tmAscent +1 in all 27 rows").
2. **proportional scale**: >= 5 rows with ratio within 3% of a median not 1 (different size, DPI or advance algorithm, not rounding).
3. **concentrated by dimension**: >= 80% of the rows of one face / height / DPI cell (single dimension first, then pairs) differ while other cells
   mostly match ("off in 9/9 rows with req_h=-11 (diffs +1 x9)").
4. isolated (<= 3 rows, keys named) or scattered (no dimension explains it, range and top diffs given).
5. text fields: `value A on the reference appears as B in n rows` (face substitution, e.g. `got`).
6. flag fields: most common XOR mask.
Plus family-level hints: rows sharing the same multi-field diff signature (`[a +1, h +1]` = one cause), "height diff equals ascent + descent diff"
(fix the parts first), and missing rows grouped by family and by face/height/DPI value (probe not run, crashed, or key format changed).

## Self-test fixtures

`tests/compare_fixtures/win10_tm_hello.rec` is the REAL Win10 22H2 (19045.2965, 96 dpi) output of `tools/probes/tm_hello.c`
(recorded 2026-11-09 in the local VM, 27 TM rows + HELLO + ENUMCOUNT). `tools/make_compare_fixtures.py` derives the other `.rec` files by mutating it
(ascent +1 at -11, Segoe -12 as Wine reports it, face substitution, halved max width, +1 jitter, a face's rows dropped, 25% wider extents, changed flag bits)
and writes `cases.json` with expected verdicts, counts and hint substrings. `compare_metrics.py --selftest` runs 14 cases (14/14 pass). Regenerate fixtures
with `python3 tools/make_compare_fixtures.py`; extend by adding a mutation and a case. Fixtures contain derived numbers only (no font files).

Two further FACTs from this role: running `scripts/run_all.ps1` inside the Win10 VM (Windows PowerShell 5.1) and comparing its `tm_hello.rec` to the
fixture gives `MATCH: 29 ref rows ... 0 out-of-tolerance diffs` (the probe is repeatable across boots); and the Wine 11.0 run through `run_all.sh`
yields 105 out-of-tolerance diffs, hints such as `TM.ext19.cx: off in 3/3 rows with face=Segoe UI`, as expected for a non-oracle (see docs/research/04).

## run_all.ps1 / run_all.sh

Run each `*.exe` found in `-Dir` (default `build/`; several dirs allowed), stdout to `<Out>/<name>.rec`, stderr to `<name>.err`, with a per-probe timeout
(default 300 s; killed on expiry). `dx_*` (destructive) areas are skipped unless `-Destructive` / `-D`. Filters: `-Include`/`-Exclude` (PS) or `-i`/`-x` (sh) name globs.
Extra arguments after `-ProbeArgs` / `--` go to every exe. Output directory default `recs/<COMPUTERNAME>-<build>-<stamp>`; it contains
`_env.rec` (host, OS build, UBR, DPI, fonts-dir file count; tag `RUNALL`, ignored by the comparer), `_status.rec` (one `RUNALL rc name= rc= seconds= lines= exe_sha256=` per exe)
and `MANIFEST.txt` (sha256, size, name of every file). The script exit status is non-zero if any probe failed or timed out.
`PROBE_RUNNER` (sh only) prefixes every exe, e.g. `PROBE_RUNNER=tools/wine_run.sh` for plumbing checks; do NOT name it `WINE`, because `wine_run.sh` reads `$WINE` as the wine binary
(doing so recursed and filled the disk during development of this role). Wine output is never a reference.
Note: a probe that writes to COM1 itself (`tm_hello --com1`) is not suited to this harness; probes should write to stdout. For the Win10 VM, wrap the call
in `run.cmd` and `type` the produced files to COM1 (see `~/w10lab/README.md`); `run_all.ps1` was validated that way (PSRC 0, 30 lines, hash of the probe exe in `_status.rec`).
