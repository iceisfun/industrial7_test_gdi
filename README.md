# gdi32.dll conformance corpus

Black-box tests for Windows 10 x64 `gdi32.dll`. The DLL is never linked: each test loads it with `LoadLibrary`,
resolves entry points with `GetProcAddress` (`U(Name)` gives a pointer typed from the SDK header;
`UIN("module.dll", Name)` pins one module, which matters for forwarders) and calls through the pointer.

Modules searched by `U()`: gdi32.dll gdi32full.dll user32.dll comctl32.dll ntdll.dll kernel32.dll.

    make                         # cross-compile build/<area>.exe (mingw-w64)
    build\<area>.exe [substr]    # on Windows; optional test-name filter
    tools\run_all.bat            # everything, results\<area>.txt (add /destructive for dx_* areas)
    python tools\summarize.py results [--failures]

Output: `RUN/OK/FAIL/SKIP` per test, `  FAIL file:line expr (detail)` per failed check, `SUMMARY` last;
exit code 1 if any test failed. `NOTE` lines record observed, version-dependent behaviour.

Tests that need a second process re-launch the exe (`--child <name>`). Scratch files live in
`%TEMP%\gdit_<pid>\` and are removed on exit. `tools/*_exports.txt` list the named exports.

See `HANDOFF.md` (when present) for how to run and triage the Windows passes.

## Data probes (recorders) - the D12 data suite

`probes/<name>/` are recorders, not conformance tests: they link gdi32/user32/comctl32/dwrite directly and write
REC v1 lines (docs/PROBE_FORMAT.md) through `common/rec.c`. Real Win10 recordings live in `data/win10/`.

    make probes                    # build/probes/<name>.exe + build/vmrunner.exe
    make probe-variants            # manifest variants: <name>.{u,v6,sys,pm2,v6sys,v6pm2}.exe ; make probes32 -> <name>.x86.exe
    tools/wine_probe.sh hello      # plumbing check under headless Wine (never data)
    tools/vm_run.sh hello,hello.v6pm2 --dpi 144     # real Win10 VM run -> data/win10/hello/dpi144{,.v6pm2}.rec
    python3 tools/rec.py validate|show|csv|diff|split ...

See docs/VM_RUN.md for the VM chain (payload, runner, DPI two-stage boot, screendumps).
