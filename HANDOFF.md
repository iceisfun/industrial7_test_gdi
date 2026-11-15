# Handoff: continuing the GDI text / font / metrics corpus

This repository records how Windows 10 (22H2, build 19045.2965 for the committed data) lays out and renders text and UI, in two
forms. Windows 10 is the only oracle; headless Wine is a plumbing check and its numbers never go into `data/`.

1. **Recorders** (`probes/<name>/`): measure behaviour and write REC v1 text (`docs/PROBE_FORMAT.md`). Recordings live in
   `data/win10/<probe>/`, with a README per family and provenance logs in `data/win10/_runs/`.
2. **Conformance tests** (`tests/<area>/`): black-box tests that must pass on real Windows 10. The system DLLs are never linked;
   entry points are resolved at run time (`U(Name)` in `common/t.h`). Each new area has an `EVIDENCE.md` that states what every
   expectation is based on and which items are observation only.

## What you need

- A Windows 10 guest that boots disposable (throw-away disk overlay), with an automatic logon, a run hook that executes a payload
  directory from an attached CD, returns results on a serial port, and powers off at the end. `docs/VM_RUN.md` describes the
  contract in detail (payload layout, serial markers, DPI changes by logoff, display modes).
- A cross compiler (`x86_64-w64-mingw32-gcc`, plus `i686-w64-mingw32-gcc` for 32-bit probes and `windres` for manifest variants),
  GNU make, Python 3.

## Running things

    make                                  # tests -> build/<area>.exe, probes -> build/probes/*.exe, build/vmrunner.exe
    make probes32 / make probe-variants   # 32-bit and manifest variants of every probe

Recorders on the guest (writes `data/win10/<probe>/<tag>.rec`, validates each recording, keeps a provenance log):

    tools/vm_run.sh hello                             # one probe, 96 DPI
    tools/vm_run.sh font_metrics,char_widths          # several probes, one boot
    tools/vm_run.sh ui_metrics --dpi 96,144,192       # several DPIs in one boot (logoff between)
    tools/vm_run.sh hello.v6pm2 --dpi 144             # manifest variant
    python3 tools/rec.py validate data/win10/<probe>/*.rec
    python3 tools/rec.py diff NEW.rec data/win10/<probe>/dpi96.rec     # diffs=0 expected; time/uptime/path fields are environment only

Conformance tests on the guest (one boot for any number of areas):

    tools/vm_test.sh core_smoke font_metrics outline_and_abc
    tools/vm_test.sh $(ls tests | grep -v compare_fixtures | grep -v '^dx_')       # every default area

Per area it writes `<area>.log`, `summary.txt` (PASS/FAIL per area, then every SKIP line and a NOTE count) and `results.tsv`.
An area passes only with exit code 0, a `SUMMARY` line with `failed=0`, and no `FAIL` line. A SKIP always carries a reason.
The test harness accepts one argument, a substring filter on test names. On any host without a guest, `make` plus
`tools/wine_probe.sh PROBE` shows that the code builds and runs, nothing more.

## REC format pointers

`docs/PROBE_FORMAT.md` is the specification: line tags (`REC ENV S R H B N X XC END`), tokens and escaping, row keys, the CRC-32
trailer, and the environment keys every recording carries (OS build, DPI, locale, fonts, metrics). `tools/rec.py` has
`validate`, `show`, `csv`, `diff`, `split`. `tools/compare_metrics.py` compares a recording of the same probe from another
source against the Windows 10 data and reports differences as data; `docs/COMPARE.md` explains the workflow.
Per-family READMEs under `data/win10/<probe>/` say what a row is and which cells are undefined or state dependent.

## Rules the corpus follows

- A conformance expectation is traceable to documentation, an observed Windows 10 result, or a recording, and that source is written
  in `EVIDENCE.md`. Undocumented behaviour is reported with `NOTE` and asserted only when it is stable across boots.
- A failing test that reveals a misunderstanding is corrected to match Windows 10; assertions are not weakened to get green.
- Behaviour that depends on SKU, locale, installed fonts, DPI, monitor layout or privilege skips with a concrete reason.
- Tests clean up what they create and do not depend on order. Recordings are produced only by the guest runner, never hand edited,
  and must be identical across two fresh boots (documented exceptions are in the family READMEs).
- Do not add paths, host names, ticket numbers or assumptions about any one implementation to the corpus.

## Coverage

`COVERAGE.md` is generated: `python3 tools/coverage.py . tools/gdi32_exports.txt tools/gdi32full_exports.txt` (static scan,
writes `tools/uncovered.txt`, not tracked), then `python3 tools/gen_coverage_md.py`. Hand-maintained inputs:
`tools/uncovered_justified.txt` (name, reason), `tools/coverage_observe_only.txt` (called but only observed). A name is
counted once in exactly one class; the document says which numbers the tooling produces.

## Done

- Recorder families (13): font_metrics, char_widths, face_substitution, ui_metrics, control_heights, glyph_bitmaps, dpi_scaling,
  font_inventory, char_placement, drawtext_layout, exttextout_extents, outline_metrics, kerning_pairs (+ hello, ui_metrics_ss6), recorded at 96 DPI,
  with DPI sweeps for ui_metrics / control_heights / dpi_scaling, awareness and comctl32 v6 variants of control_heights at five DPIs, and a
  standard-user (restricted token) ui_metrics recording (`runstd` verb of the guest runner, `docs/STD_USER_RUNS.md`).
- Conformance areas (20, each with an `EVIDENCE.md`): the 15 hand-written areas plus five generated golden areas
  (`tests/golden_*`, produced by `tools/gen_golden.py` from `docs/D12_DECISION_DATA.md` data; regenerate both generators after changing
  a recording: `python3 tools/gen_d12.py` and `python3 tools/gen_golden.py`, `--check` verifies them).
- Guest runners for recorders (`tools/vm_run.sh`) and for tests (`tools/vm_test.sh`), `COVERAGE.md`.
- Above 96 DPI the guest is not deterministic between boots (STATE.md "DPI states"); do not treat one sweep recording as a stable reference there.

## Not done

See `STATE.md` for the current list. In short: opt-in destructive `dx_*` areas (font installation limits, corrupt font files, handle
exhaustion, runtime DPI / display / locale changes), tests at non-96 DPI, more recorder ideas (ABC widths as a recorder, font linking and
fallback, locale and charset variants, high contrast, RTL and complex scripts for GetCharacterPlacement), a real standard-user account,
and comparison against other oracle data.
