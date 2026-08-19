#!/usr/bin/env bash
# ======================================================================
# ci/smoke.sh [path-to-CASTALIA.EXE] - headless boot smoke test
# ----------------------------------------------------------------------
# Boots the real DOS build under DOSBox on a virtual X server and asserts
# that it gets far enough to paint its graphical desktop.  A crash, a hang
# at the DOS prompt or a failure to reach Mode 13h all leave a blank (one
# or two colour) framebuffer; a live desktop - wallpaper, icons, taskbar -
# has hundreds of colours, which is the signal we poll for.
#
# Requires: dosbox, Xvfb, and EITHER ImageMagick (import + convert) OR
# ffmpeg + python3.  The fallback exists because the machine this is
# most useful on - a dev box with the Watcom cross-compiler and a DOSBox
# harness - is not guaranteed to have ImageMagick, and a boot test you
# cannot run is not a boot test.  Both paths measure the same thing.
# Writes the last screenshot to ./ci-smoke.png for the CI to upload.
#
# Usage:  bash ci/smoke.sh [CASTALIA.EXE]   (exit 0 = booted, 1 = did not)
# ======================================================================
set -u
cd "$(dirname "$0")/.."

EXE="${1:-CASTALIA.EXE}"
[ -f "$EXE" ] || { echo "smoke: '$EXE' not found"; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/disk"
cp "$EXE" "$work/disk/CASTALIA.EXE"
[ -f release/CASTALIA.INI ] && cp release/CASTALIA.INI "$work/disk/"
[ -d release/ASSETS ]       && cp -r release/ASSETS   "$work/disk/ASSETS"

cat > "$work/dosbox.conf" <<EOF
[sdl]
output=surface
[mixer]
nosound=true
[cpu]
core=auto
cycles=60000
[autoexec]
mount c $work/disk
c:
CASTALIA.EXE
EOF

XSZ=1024x768
have() { command -v "$1" >/dev/null 2>&1; }
if ! (have import && have convert) && ! (have ffmpeg && have python3); then
  echo "smoke: need ImageMagick (import+convert) or ffmpeg+python3"
  exit 1
fi

# Grab the root window.
grab() {
  if have import; then
    import -window root "$1" 2>/dev/null
  else
    ffmpeg -y -f x11grab -video_size "$XSZ" -i "$DISPLAY" \
           -frames:v 1 "$1" >/dev/null 2>&1
  fi
}

# Unique colours in the grab - the signal that a DESKTOP is on screen
# rather than a blank framebuffer or a DOS prompt.
ncolors() {
  if have convert; then
    convert "$1" -format "%k" info: 2>/dev/null || echo 0
  else
    ffmpeg -v error -i "$1" -f rawvideo -pix_fmt rgb24 - 2>/dev/null |
    python3 -c 'import sys
d = sys.stdin.buffer.read()
print(len({d[i:i+3] for i in range(0, len(d) - 2, 3)}))'
  fi
}

export DISPLAY=:99
Xvfb :99 -screen 0 ${XSZ}x24 >/dev/null 2>&1 &
xvfb=$!
sleep 3
SDL_VIDEODRIVER=x11 dosbox -conf "$work/dosbox.conf" >"$work/dosbox.log" 2>&1 &
dbox=$!

shot=./ci-smoke.png
ok=0
colors=0
for i in $(seq 1 20); do
  sleep 2
  grab "$shot" || true
  if [ -f "$shot" ]; then
    colors=$(ncolors "$shot")
    colors=${colors:-0}
    echo "  attempt $i: $colors unique colours"
    if [ "$colors" -gt 48 ]; then ok=1; break; fi
  fi
done

kill "$dbox"  2>/dev/null || true
kill "$xvfb"  2>/dev/null || true
wait 2>/dev/null || true

if [ "$ok" -eq 1 ]; then
  echo "smoke: desktop rendered ($colors unique colours) - OK"
  exit 0
fi

echo "smoke: the framebuffer never became a desktop (max $colors colours)"
echo "--- dosbox.log ---"
cat "$work/dosbox.log" || true
exit 1
