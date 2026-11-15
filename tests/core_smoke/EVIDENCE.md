# Evidence: core_smoke

Area: harness smoke test (`tests/core_smoke/smoke.c`). It checks that the test runner, the module resolver, a negative
`GetProcAddress` and the child-process helper work. It makes no claim about gdi32 behaviour.

Oracle: Windows 10 22H2 (19045.2965, en-US) guest, `tools/vm_test.sh core_smoke` together with the other two areas of this batch.
Runs: two boots of the earlier code and two boots of the final code (runs 3 and 4), all `PASS tests=3 failed=0 skipped=0 checks=4/4`,
logs identical after sorting. No test code was changed in this area.

Source codes: D = Microsoft documentation, O = observed on Win10 22H2 19045.2965 (n/n = stable boots), X = decompilation / export table.

| Test | Expectation | Source | Asserted / NOTE | Stability |
|---|---|---|---|---|
| smoke_module_loads | `t_module("gdi32.dll")` (LoadLibrary) returns non-NULL | D loadlibrary (a module handle on success, NULL on failure); gdi32.dll is part of every Windows 10 install | asserted; resolve-only by design, not behavioural coverage and not counted as such | 4/4 |
| smoke_getprocaddress_negative | `GetProcAddress(kernel32, "NoSuchExport_xyz")` returns NULL and last error is `ERROR_PROC_NOT_FOUND` (127) | D getprocaddress (NULL on failure, extended error) + O | asserted | 4/4 |
| smoke_child_roundtrip | the helper starts the same exe as `--child echo 42` and the exit code comes back as 42 within 20 s | harness contract (`CHILD`, `t_run_child` in `common/t.c`), D getexitcodeprocess | asserted | 4/4 |

Skips: none. Nothing in this area depends on SKU, locale, fonts or DPI.

## Weakness review

- `smoke_module_loads` is an existence check. It is kept because it is the precondition of every other area (all entry points
  are resolved through `U(Name)`), but it must not be read as conformance coverage.
- `smoke_child_roundtrip` would pass with any exit code that equals the argument, so it proves argument passing and exit-code
  return only. Several other areas rely on exactly that path (`char_widths` `bad_params`, `font_resources`).

## Ideas not implemented

- A test that `t_run_child` reports a timeout for a child that never exits (the harness uses it in `bad_params`; not an oracle
  property, so out of scope for this corpus).
