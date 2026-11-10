# State of industrial7_test_gdi (2026-11-10, lane lanec)

This replaces `workflows/RESUME.md` (written at the suspension of the runaway "gdi-font-suite" Workflow, now stale).
DO NOT relaunch `workflows/gdi-font-suite-*.js` (two runs, many Opus agents; it is kept only as a record, with its journals).

## Done (verified)
- Recorder probes for 8 families + `hello` + `ui_metrics_ss6`: font_metrics, char_widths, face_substitution, ui_metrics, control_heights,
  glyph_bitmaps, dpi_scaling, font_inventory (probes/, REC v1 in docs/PROBE_FORMAT.md, tools/rec.py).
- Real Windows 10 22H2 (19045.2965) recordings in `data/win10/<probe>/` (101 files, all validate with `tools/rec.py validate`): DPI 96 full for every
  family, DPI sweeps 96..192 for ui_metrics / control_heights / dpi_scaling, control_heights v6 manifest variant, ui_metrics cold144 reference.
  Each family has a README (what, rows, boots, determinism). Provenance logs: `data/win10/_runs/`.
- Determinism: two fresh boots identical for 6 families; the documented exceptions are `GetSystemMetrics` idx 93 and `SystemParametersInfoForDpi` at
  72 dpi (undefined cells) and the sweep-vs-cold-boot DPI state (see data/win10/ui_metrics/README.md: use cold144 as the 144 reference).
- Local oracle: `tools/win10_run_local.sh` runs a throw-away qcow2 overlay of the Win10 base image (bb depot m2) in QEMU with the payload on a CD and
  results over COM1; `tools/vm_run.sh` drives it (docs/VM_RUN.md, section 'Running on lanec').
- One probe bug fixed on real Windows: control_heights sent TTM_GETBUBBLESIZE under comctl32 5.82 (access violation); now only under v6.

## Not done
- docs/D12_DECISION_DATA.md (the decision tables), golden tests (tools/gen_golden.py + tests/golden_*), COVERAGE.md/HANDOFF.md, a run of the conformance
  tests under tests/ against Win10 (EVIDENCE.md per area), system/per-monitor DPI-aware variants of control_heights, the sweep stages other than 96 were
  not repeated for determinism, std-user recordings (the guest auto-logon is an elevated Administrator).
- Compare against the i7 side (tools/compare_metrics.py exists) once an i7 recording of the same probes exists.

## Tracking
bb topic #1157 (industrial7_test_gdi). Guest facts: Win10 Home 22H2 19045.2965, en-US, 1024x768x32, qemu -cpu host -smp 2 -m 4G, user Administrator (elevated).
