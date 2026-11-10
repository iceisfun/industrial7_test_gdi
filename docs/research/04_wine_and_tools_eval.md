# 04 - Wine and tooling evaluation

Role: decide what Wine is good for here, check the host data-pipeline tools, and document the Win10 VM contract
(`~/w10lab/tools/win10_run.sh`) from first-hand runs. Facts are labelled FACT (command run by me, or decomp/bb-depot
evidence) or INFER (my reading). Units: all heights are pixels at 96 dpi; `h` = tmHeight (cell height = ascent+descent),
`il` = tmInternalLeading, request `-N` = character height N px (em), not cell height.

Probe: `tools/probes/tm_hello.c` (mingw x86_64, static). For 9 faces x heights {-11,-12,-16} it prints TM fields,
GetTextExtentPoint32A("The quick brown fox", 19 chars), GetCharWidth32A for `i`,`W`,`0`, and the EnumFontFamiliesEx count.
It uses `CreateCompatibleDC(NULL)` (a 1x1 monochrome memory DC, 96 dpi), not a screen DC. FACT: its Segoe UI -12 result
(15/12/3, ave 6, max 31) is identical to the np_metrics Win10 probe result (bb depot m192 run in m199, `font.segoeui_m12`
height=15 ascent=12 intlead=3 ave=6 max=31), so the DC choice does not change that row.

## 1. Wine as a GDI font-metrics oracle: NO (plumbing check only)

Setup (FACT): `tools/wine_run.sh build/tm_hello.exe`, DISPLAY unset (null driver), fresh copy of the base prefix. NOTE: the
script's default `WINE` is the Proton-GE build, `wine-11.0 (Staging)` (`$WINE --version`), not Ubuntu system `wine-9.0`
(which is also installed, `/usr/bin/wine`). I only ran the 11.0 build; 9.0 was not run (INFER: same FreeType-based
engine, same conclusion). Headless works: dpi 96, 32bpp, 1024 wide, exit clean, ~seconds.

What Wine supplies (FACT, listing of `~/.cache/test_pattern/prefix/drive_c/windows/Fonts` + `fc-match`): the prefix has only Wine's own
`arial.ttf`, `cour.ttf`, `marlett.ttf`, `msyh.ttf` and the bitmap `.fon` set (courier, small fonts, ser/sserif, vga). Segoe UI is not installed
(`fc-list | grep -ci "segoe|tahoma|micross"` = 0); `fc-match "Segoe UI"` and `"Tahoma"` -> DejaVu Sans, `Arial` -> Liberation Sans,
`Courier New` -> Liberation Mono. Wine reports the requested face name back from GetTextFace (`got=` equals the request for every
face, incl. Segoe UI), so even face substitution is invisible through the name. Unknown face `NoSuchFace` -> `Arial` in both systems.

Side-by-side, 27 rows (9 faces x 3 heights), Win10 = local VM run described in section 3, Wine = same exe (FACT):

| field | rows equal Wine==Win10 (of 27) |
|---|---|
| tmHeight | 14 |
| tmAscent | 14 |
| tmDescent | 21 |
| tmInternalLeading | 17 |
| tmAveCharWidth | 20 |
| tmMaxCharWidth | **3** |
| extent cx (19-char string) | 22 |
| extent cy | 14 |
| char widths i / W / 0 | 24 / 24 / 25 |
| all fields except max | 11 |

Selected rows (`h/ascent/descent/intlead/ave/max`):

| face, req | Win10 | Wine |
|---|---|---|
| Segoe UI -11 | 13/11/2/2/6/28 | 12/10/2/1/6/13 |
| Segoe UI -12 | **15**/12/3/3/6/31 (ext19 108x15) | **14**/11/3/1/7/14 (ext19 100x14) |
| Segoe UI -16 | 21/17/4/5/9/41 | 17/14/3/2/9/19 |
| Tahoma -11 | 13/11/2/2/5/27 | 13/11/2/2/5/16 |
| MS Shell Dlg -11 | 13/11/2/2/5/23 | 13/11/2/2/5/16 |
| MS Shell Dlg 2 -11 | 13/11/2/2/5/27 | 13/11/2/2/5/16 |
| Arial -11 | 14/11/3/3/5/29 | 12/10/2/1/6/14 |
| Courier New -12 | 15/12/3/3/7/9 | 14/10/4/2/7/8 |
| System (any req) | 16/13/3/3/7/14 | 16/13/3/3/7/15 |
| MS Sans Serif -11/-12/-16 | 13,13,20 (max 11,11,16) | identical (bitmap fonts, same .fon data) |

Reading (INFER, from the table): Wine agrees where the face is a bitmap font or where Wine ships a metric-compatible Tahoma
(Tahoma, MS Shell Dlg (2), MS Sans Serif, System: heights and advances match, hence the matching 97/114/146 px extents). It disagrees on
exactly the questions D12 asks: Segoe UI -12 gives 14 not 15 (the "15/13 heights" question), internal leading 1 not 3, and
tmMaxCharWidth is wrong almost everywhere (Wine reports FreeType's glyph max, Windows reports a table-derived max; 28/31/41 vs 13/14/19).
Wine's Arial/Courier cell heights follow Liberation/own metrics, 1-3 px off. Conclusion: Wine's FreeType path is not Windows' font
mapper + hinted ClearType metrics. It must never be a source of recorded values; use it only to prove a probe compiles, runs,
parses its own output and has no crashes before spending a VM boot. Also not an oracle for EnumFontFamiliesEx counts
(Wine 794 because it folds in host fontconfig fonts; Win10 682).

## 2. Host tools for the data pipeline (FACT, `which`/import checks)

| tool | status |
|---|---|
| xorriso | 1.5.6, present (used by win10_run.sh to build the payload ISO) |
| qemu-img / qemu-system-x86_64 | 8.2.2 present (KVM used) |
| python3 | 3.12.3; PIL (Pillow) 10.2.0, numpy 1.26.4, fontTools 4.46.0, zlib: all importable |
| freetype-py | missing (not needed; fontTools reads head/hhea/OS-2/hmtx/cmap/glyf; PIL's ImageFont gives FreeType rasterisation if wanted) |
| x86_64-w64-mingw32-gcc, i686-w64-mingw32-gcc | present |
| wine | system 9.0 + Proton-GE 11.0 Staging build; headless only through tools/wine_run.sh |
| curl, bb | present |

Pipeline implications (INFER): PIL can write the tiny greyscale PNG/PGM bitmaps and hash them; numpy for diffing/packing metric arrays;
fontTools can cross-check TrueType tables (unitsPerEm, hhea ascent/descent/lineGap, OS/2 usWin*/sTypo*, hmtx) once the real font
files are copied out of the guest (segoeui.ttf, tahoma.ttf, micross.ttf, arial.ttf, cour*.ttf are Microsoft-licensed: ship over serial
into a gitignored dir, commit only derived numbers and hashes). That derived-from-tables vs measured comparison is the cheapest way to
explain/predict the 15/13 heights without more VM time.

## 3. The Win10 VM contract (read of `win10_run.sh`, `README.md`, bb board, earlier probes, plus an empirical run)

`LAB_READY` existed when I tested ("ok ... build 19045.2965 smoke=runs/smoke1"). Base disk 4.8 GiB (sha256 991856e1...c4e matches the m2 manifest).

FACT, from the script:
- `win10_run.sh PAYLOAD_DIR [OUTDIR]`: needs `PAYLOAD_DIR/run.cmd`; copies the dir to `i7run/`, adds an empty `shutdown` file, rewrites run.cmd to CRLF,
  builds an ISO (`-V I7RUN`, read-only CD-ROM, drive D: in my run), makes a qcow2 overlay on the base, takes `flock ~/w10lab/oracle.lock` (blocks while another
  lane holds it), runs qemu (kvm, 4 GiB, 4 vCPU, 1024x768 std VGA, VNC :78, user-net with `restrict=on`, `-no-reboot`) with `-serial file:OUT/serial.log`,
  default timeout `ORACLE_TIMEOUT=900` s, deletes the overlay unless `W10_KEEPDISK`/`W10_OVERLAY`.
- The guest auto-logs on (Administrator, `i7-win10\administrator`, build 10.0.19045.2965), then hook `C:\i7\boot.cmd` scans D..K for `\i7run\run.cmd`, prints
  `I7VM: logon <time>` / `I7VM: payload D:\i7run` to COM1, runs it, then powers off.
- No internet; the payload ISO is read-only, so COM1 is the only way back (plus the optional `W10_NETEXTRA` guestfwd to a host server, untested by me).
- Evidence from bb board (#737, #762, FACT as quoted): `boot.cmd` runs `call run.cmd` WITHOUT relaying its stdout to COM1; only the exit code reaches serial.
  So every output must be redirected explicitly.

FACT, how earlier probes worked: `bb depot get m192` (win10_np_metrics_probe_free.exe) and `m205` (cc30_lv_geometry_probe.exe) are console PE32+ exes importing only
GDI32/KERNEL32/USER32 (`objdump -p`), whose strings show only `GetStdHandle`/`WriteFile`: they write plain lines to STDOUT and rely on `run.cmd` redirection (`> COM1`).
The task60 run archive (m199, run via the separate `/data/oracle` driver, not the local script) shows the same line format (`NPMET <key> k=v ...`), with `out.txt` captured
by the driver and `serial.log` carrying only the `I7VM:` / `ORACLE START/DONE` markers. Those archives are driver-specific; with the local script the single output channel is serial.log.

### Empirical hello run (FACT; run1, 2026-11-09)
Payload `tools/probes/hello_payload/run.cmd` + `build/tm_hello.exe` + 200000 random bytes. Wall time of `win10_run.sh` end to end: **40 s** (boot + run + poweroff).
Results (serial.log 282622 bytes):
1. `exe > COM1` (cmd redirection) works; the exe's `\n` lines arrive as LF, while `echo`/`ver`/`whoami` lines arrive CRLF. serial.log is therefore mixed LF/CRLF: parse with `splitlines()`/strip `\r`.
2. The exe doing `CreateFileA("\\\\.\\COM1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, ...)` + WriteFile works with no `SetCommState`; output is byte-identical to the redirected run (diff empty, 30 lines).
   Caveat: if cmd also has COM1 open at the time (`> COM1` held open) the sharing mode 0 could clash; I ran them sequentially, which worked.
3. Binary: `certutil -encode rand.bin rand.b64` then `type rand.b64 > COM1`: 200000 B -> 4183 lines of base64 (276 KB incl. headers). Decoding the serial.log slice with `base64 -d` gave sha256 de20a132...143d, equal to the source file. Transfer took ~1.2 s (15:18:21.01 -> 15:18:22.25), i.e. roughly 220 KB/s through qemu's file chardev (the emulated UART does not throttle to the baud rate). No loss at that size.
4. Win10 probes can take screen DCs and a desktop: logon is interactive at 1024x768, 96 dpi (`GetDeviceCaps` dpiX/Y 96, bpp 32).

### What a probe payload must do (checklist)
1. `run.cmd` must itself redirect or exes must write COM1; never rely on stdout relay. Locate the payload by scanning D..K for `\i7run\run.cmd` (drive letter is not guaranteed); copy anything to be written to `%TEMP%` (payload is read-only).
2. Start with a banner line `RUN <id> <tool version> <date>` and end with a sentinel `DONE`; a missing sentinel flags a timeout/truncation. Echo `ver` and the build once per boot.
3. Line-oriented, stable `KEY k=v ...` records (as the probe here and NPMET do); fixed key order, no timestamps inside data lines; ASCII only.
4. Small results (< ~100 KB): print as text. Big or binary results (greyscale bitmaps, font files, hash tables): emit as lines `BLOB <name> <n_bytes> <sha256>` then `B64 <name> <seq> <crc32> <chunk>` lines of <= 76 chars... chunks of ~3 KB base64 with a per-chunk CRC32 are cheap insurance, and a final per-blob SHA-256 (verified on the host). `certutil -encode` plus `type` is a zero-code option (verified above, strip the BEGIN/END lines); a C exe can base64 itself to avoid cmd quirks. Compress first (zlib/`makecab`/`tar` is on Win10) when the data is repetitive.
5. Size budget (INFER from the measured rate): 15 min boot cap at ~220 KB/s is theoretically ~190 MB, but serial.log lives in the run dir and is hosted by the host filesystem; keep raw output <= ~20-50 MB per boot and let the host side dedupe/compress. A 1 MB font via base64 is ~5 s.
6. Batch many probes per boot (boot+shutdown overhead is ~35 s of the 40 s). Keep each probe crash-isolated (one exe per area, `run.cmd` ignores errorlevel and writes `RC <name> <n>` after each) so one crash does not lose the boot.
7. Bound every probe (no waits for input/UI that can hang; if a window message pump is needed, use a timer and exit). ORACLE_TIMEOUT kills qemu hard; partial serial.log is still kept.
8. Do not run qemu directly; one guest at a time via the lock; use `W10_KEEPDISK=1` only for deliberate two-stage runs (e.g. install a font, reboot).
9. Record provenance with the data: exe sha256, build 10.0.19045.2965, DPI, DC type used (screen DC vs memory DC), user (Administrator).

Unverified/open: measured behaviour with a screen DC vs the 1bpp memory DC for non-Segoe faces (Win10 Arial -11 gives cell height 14 with il 3 here, which looks larger than
Arial's hhea metrics at 11 px would suggest, INFER; worth an A/B with GetDC(NULL) in the first real data run); the system Wine 9.0 binary; W10_NETEXTRA host-server path; DPI changes in the guest.
