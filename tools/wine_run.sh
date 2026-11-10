#!/usr/bin/env bash
# Run one test exe under Wine in a throw-away, HEADLESS sandbox.
#   tools/wine_run.sh build/<area>.exe [args...]   (all args after the exe are passed through)
# - fresh copy of the base prefix per run (tests cannot pollute each other or your real prefix)
# - no X: DISPLAY/WAYLAND_DISPLAY are unset so Wine uses its built-in null display driver; this keeps
#   the main X session untouched (many parallel wine runs with persistent explorer.exe exhausted the X
#   server's client slots before). Set TP_DISPLAY=xephyr to run inside a nested Xephyr server instead
#   (only for tests that genuinely need a display driver); the nested server is torn down afterwards.
# - hard timeout, and the prefix's wineserver and children are killed + the prefix removed afterwards.
set -uo pipefail
EXE="$1"; FILTER="${2:-}"
WINE="${WINE:-$HOME/work/wine/proton-ge-custom/build/dist/files/bin/wine}"
BASE="${TP_BASE_PREFIX:-$HOME/.cache/test_pattern/prefix}"
TMO="${TP_TIMEOUT:-600}"
[ -d "$BASE/drive_c" ] || "$(dirname "$0")/make_base_prefix.sh" >&2
RUN="$(mktemp -d "${TMPDIR:-/tmp}/tp_prefix.XXXXXX")"
cp -a --reflink=auto "$BASE/." "$RUN/"
cleanup() {
  WINEPREFIX="$RUN" "$(dirname "$WINE")/wineserver" -k 2>/dev/null
  sleep 0.3
  rm -rf "$RUN"
  [ -n "${XEPID:-}" ] && kill "$XEPID" 2>/dev/null
}
trap cleanup EXIT
ENVV=(WINEPREFIX="$RUN" WINEARCH=win64 WINEDEBUG="${WINEDEBUG:--all}")
if [ "${TP_DISPLAY:-none}" = "xephyr" ]; then
  N=$((100 + RANDOM % 800)); Xephyr ":$N" -screen 1024x768 -nolisten tcp >/dev/null 2>&1 & XEPID=$!
  sleep 1; ENVV+=(DISPLAY=":$N")
  env -u WAYLAND_DISPLAY "${ENVV[@]}" timeout "$TMO" "$WINE" "$EXE" "${@:2}"
else
  env -u DISPLAY -u WAYLAND_DISPLAY "${ENVV[@]}" timeout "$TMO" "$WINE" "$EXE" "${@:2}"
fi
