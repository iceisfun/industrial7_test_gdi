#!/usr/bin/env python3
"""screendump.py - QMP screendump helper for the w10lab VM (win10_run.sh puts the socket at OUTDIR/qmp.sock).

  screendump.py shot SOCK OUT.png            one screendump now (PNG; falls back to PPM -> PNG via PIL if needed)
  screendump.py watch OUTDIR [--max N]       wait for OUTDIR/qmp.sock, then for every line 'I7P SHOT name=<n>' that
                                             appears in OUTDIR/serial.log take OUTDIR/shots/<n>.png; exits when qemu goes away
The guest side (vmrunner plan verb 'shot <name> [wait_ms]', or a probe printing 'I7P SHOT name=<n>' to stdout and then
sleeping ~2 s) pauses so the frame is stable when the host grabs it. Screendumps are of the emulated VGA framebuffer
(-vga std), i.e. exactly what the guest display shows, at the guest's resolution.
"""
import json, os, re, socket, sys, time


class QMP:
    def __init__(self, path, timeout=10):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(timeout)
        self.s.connect(path)
        self.f = self.s.makefile("rwb")
        self._read()                                   # greeting
        self.cmd("qmp_capabilities")

    def _read(self):
        while True:
            line = self.f.readline()
            if not line:
                raise ConnectionError("qmp closed")
            m = json.loads(line)
            if "event" in m:
                continue
            return m

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args} if args else {"execute": name}).encode() + b"\n")
        self.f.flush()
        return self._read()

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def shot(q, out):
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    r = q.cmd("screendump", filename=out, format="png")
    if "error" in r:                                   # older qemu or no libpng: PPM then convert
        ppm = out[:-4] + ".ppm" if out.endswith(".png") else out + ".ppm"
        r = q.cmd("screendump", filename=ppm)
        if "error" in r:
            return r
        try:
            from PIL import Image
            Image.open(ppm).save(out)
            os.remove(ppm)
        except Exception as e:  # keep the ppm
            return {"return": {}, "note": "kept ppm (%s)" % e}
    return r


def watch(outdir, maxn=200):
    sock, serial = os.path.join(outdir, "qmp.sock"), os.path.join(outdir, "serial.log")
    q, pos, n, seen_sock = None, 0, 0, False
    deadline = time.time() + 7200                      # waiting for the oracle lock can take a while
    while time.time() < deadline:
        if q is None:
            if os.path.exists(sock):
                try:
                    q = QMP(sock)
                    seen_sock = True
                except OSError:
                    if seen_sock:
                        pass
                    time.sleep(0.5)
                    continue
            else:
                if seen_sock:
                    return 0
                time.sleep(1)
                continue
        try:
            with open(serial, "rb") as f:
                f.seek(pos)
                data = f.read()
        except FileNotFoundError:
            data = b""
        nl = data.rfind(b"\n")
        if nl >= 0:
            pos += nl + 1
            for line in data[:nl].decode("latin-1").split("\n"):
                m = re.match(r"I7P SHOT name=(\S+)", line.strip())
                if m and n < maxn:
                    name = re.sub(r"[^A-Za-z0-9_.+-]", "_", m.group(1))
                    try:
                        r = shot(q, os.path.join(outdir, "shots", name + ".png"))
                        print("screendump %s: %s" % (name, "ok" if "error" not in r else r["error"]), flush=True)
                        n += 1
                    except (OSError, ConnectionError) as e:
                        print("screendump %s failed: %s" % (name, e), flush=True)
                        return 0
        try:
            q.cmd("query-status")
        except (OSError, ConnectionError, ValueError):
            return 0                                   # qemu exited
        time.sleep(0.3)
    return 0


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "shot":
        q = QMP(sys.argv[2])
        print(shot(q, sys.argv[3]))
        q.close()
        return 0
    if len(sys.argv) >= 3 and sys.argv[1] == "watch":
        maxn = int(sys.argv[4]) if len(sys.argv) >= 5 and sys.argv[3] == "--max" else 200
        return watch(sys.argv[2], maxn)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
