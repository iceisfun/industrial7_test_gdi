# data/

- `data/win10/<probe>/<tag>[.<variant>].rec` - recordings from the real Win10 22H2 VM, written only by
  `tools/vm_run.sh` (via `tools/rec.py split`, after the END trailer CRC validated). `<tag>` defaults to
  `dpi<N>[-<W>x<H>]`; `<variant>` is the manifest variant of the exe (`v6`, `sys`, `pm2`, `v6pm2`, `x86` ...;
  none = no manifest). Recordings larger than 4 MB are stored gzipped (`.rec.gz`, mtime 0); all tools read both.
- `data/win10/_runs/<tag>.<probes>.log` - provenance of the boot that produced them: serial.log SHA-256 and size,
  the files written, and the runner markers (`I7VM`, `I7VMRUN`, `I7P BEGIN/END rc= ms= timeout=`).
- `data/raw/` - gitignored. Large raw outputs (font files copied out of the guest, full serial logs, big blobs).
  If something there matters, commit a manifest (`name size sha256`) next to the derived data instead.
- Never put Wine output here (`build/wine/` is where `tools/wine_probe.sh` writes); Wine is not an oracle.

Format: docs/PROBE_FORMAT.md. Running: docs/VM_RUN.md.
