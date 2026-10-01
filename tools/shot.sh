#!/usr/bin/env bash
# ======================================================================
# tools/shot.sh - drive CASTALIA.EXE under DOSBox and photograph it
# ----------------------------------------------------------------------
# ci/smoke.sh answers "does it boot".  This answers "does the thing I
# just changed LOOK right and DO the right thing", which is a different
# question and the one that has caught the most in this project: a
# logotype whose chrome ramp sliced its own lowercase, a warning that ran
# off the edge as "NOT SAVI", a file picker that dropped clicks, a bulk
# copy that emptied the files it was copying.  None of those were visible
# by reading the code; every one was obvious in a screenshot.
#
# Requires: dosbox, Xvfb, ffmpeg, xdotool.
#
#   tools/shot.sh <name> <display> [icon1_cmd] [icon2_cmd] -- <script>
#
# <name>        prefix for the output PNGs and the scratch disk
# <display>     an unused X display, e.g. :91
# icon1/icon2   internal verbs to put on the desktop as icons, so the
#               script can launch them ("fileman", "scrap", "gram", ...)
# <script>      shell run with the DOSBox window bound to $W and these
#               helpers available:
#                 K <key> [pause]     xdotool key
#                 T <text>            xdotool type
#                 C <x> <y> [button]  move and click
#                 SNAP <tag>          write <name>_<tag>.png
#
# Environment:
#   VIDEO=mode12h   boot in 640x480x16 instead of Mode 13h
#   XSIZE=720x580   Xvfb/grab size.  Mode 12h needs this: the DOSBox
#                   window is offset downward, so at 640x480 the bottom
#                   of the DOS screen - the taskbar - falls outside the
#                   grab entirely.
#   OUT=<dir>       where PNGs and scratch disks go (default: ./shots)
#   FLOPPY=1        also mount <OUT>/seed_<name>_a/ as A: with -t floppy,
#                   so removable-media behaviour can be tested at all -
#                   DOS reports a drive's type through INT 21h AX=4408h
#                   and every drive DOSBox mounts normally is "fixed"
#   SMALLA=<KB>     instead, imgmount a FAT12 image of that size as A:.
#                   A directory mount passes writes to the host and so
#                   never runs out of room; an image does, which is the
#                   only way to reach the disk-full paths at all.
#   CYCLES=<n>      DOSBox cycles (default 20000).  A 386SX/16 is about
#                   1000-1200 and a 486DX2/66 about 12000, so this is
#                   the only way to ask "is it still usable on the
#                   machine it is FOR" - the roadmap's last open item is
#                   real hardware, and this is the nearest thing to it.
#   SMALLFREE=<N>   with SMALLA: leave only about N bytes free, the rest
#                   taken by a FILLER.DAT with a real FAT chain.  For a
#                   save that caps its document at 4 KB, an empty 160 KB
#                   floppy is not full enough to fail.
#
# Pre-seeded files: anything in <OUT>/seed_<name>/ is copied onto C:
# before boot, which is how you test against a directory of 250 files or
# a hand-made CARDFILE.DAT.
#
# Left behind on C: when - and only when - CASTALIA.EXE returned to DOS:
# EXITED.TXT, written by the line after it in the autoexec.  "Did the
# shell quit?" is otherwise not a question the disk can answer, and the
# unsaved-work prompts on the way out are exactly the kind of thing that
# needs it.  Note that its ABSENCE also means "the script never got as
# far as quitting", so a case that asserts on absence needs a companion
# that asserts on presence, or it passes for the wrong reason.
#
# ---- ONE HARD-WON DETAIL ---------------------------------------------
# xdotool's window coordinates are NOT the DOS screen's.  The DOSBox
# window content sits about 48 pixels BELOW the window origin, so to
# click something you measured at screen y=112 in a screenshot you pass
# y=64.  Clicks that land 48px low select the wrong list row and look
# like the feature is broken.  Measure in the PNG, subtract 48.
#
# Also: DOSBox coalesces a fast double click.  Two clicks a second apart
# (two C calls) is the reliable way to select-then-activate a list row;
# "xdotool click --repeat 2" often registers as one.
# ======================================================================
set -u

SELF="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SELF/.." && pwd)"
OUT="${OUT:-$ROOT/shots}"

NAME="${1:?usage: shot.sh <name> <display> [icon1] [icon2] -- <script>}"
DISP="${2:?missing display, e.g. :91}"
I1="${3:-}"; I2="${4:-}"
shift 4 2>/dev/null || shift $#
[ "${1:-}" = "--" ] && shift
BODY="${*:-}"

for t in dosbox Xvfb ffmpeg xdotool; do
  command -v "$t" >/dev/null 2>&1 || { echo "shot: need $t"; exit 1; }
done
[ -f "$ROOT/CASTALIA.EXE" ] || { echo "shot: build CASTALIA.EXE first"; exit 1; }

mkdir -p "$OUT"
DISK="$OUT/disk_$NAME"
rm -rf "$DISK"; mkdir -p "$DISK"
cp -r "$ROOT/assets" "$DISK/ASSETS" 2>/dev/null
cp "$ROOT/CASTALIA.EXE" "$DISK/CASTALIA.EXE"

# A seed directory may carry its own CASTALIA.INI; only write one if not.
if [ ! -f "$OUT/seed_$NAME/CASTALIA.INI" ]; then
  {
    echo "[system]"; echo "theme=classic"; echo "video=${VIDEO:-mode13h}"
    echo "animations=true"; echo "sound=false"; echo "screensaver=0"
    echo "[desktop]"; echo "pattern=gradient"
    [ -n "$I1" ] && { echo "icon1_name=$I1"; echo "icon1_command=$I1"; }
    [ -n "$I2" ] && { echo "icon2_name=$I2"; echo "icon2_command=$I2"; }
  } > "$DISK/CASTALIA.INI"
fi
[ -d "$OUT/seed_$NAME" ] && cp -r "$OUT/seed_$NAME/." "$DISK/"
[ -n "${SMALLA:-}" ] && python3 "$SELF/mkfloppy.py" "$OUT/a_$NAME.img" "$SMALLA" ${SMALLFREE:-}

XSZ="${XSIZE:-640x480}"
cat > "$OUT/box_$NAME.conf" <<EOF
[sdl]
output=surface
autolock=false
[render]
aspect=false
[mixer]
nosound=true
[cpu]
core=auto
cputype=386
cycles=${CYCLES:-20000}
[autoexec]
${FLOPPY:+mount a $OUT/seed_${NAME}_a -t floppy}
${SMALLA:+imgmount a $OUT/a_$NAME.img -t floppy}
mount c $DISK
c:
CASTALIA.EXE
ECHO EXITED > EXITED.TXT
EOF

pkill -f "Xvfb $DISP" 2>/dev/null; sleep 1
export DISPLAY="$DISP"
Xvfb "$DISP" -screen 0 "${XSZ}x24" -nolisten tcp >/dev/null 2>&1 &
sleep 2
SDL_VIDEODRIVER=x11 dosbox -conf "$OUT/box_$NAME.conf" \
    >"$OUT/log_$NAME.txt" 2>&1 &
DBOX=$!
sleep 12

W=$(xdotool search --name DOSBox | head -1)
[ -z "$W" ] && { echo "shot: no DOSBox window appeared"; exit 1; }

K(){ xdotool key --clearmodifiers --window "$W" "$1"; sleep "${2:-0.8}"; }
T(){ xdotool type --window "$W" --delay 80 "$1"; sleep 0.6; }
C(){ xdotool mousemove --window "$W" "$1" "$2"; sleep 0.3
     xdotool click --window "$W" "${3:-1}"; sleep 1.0; }
SNAP(){ ffmpeg -y -f x11grab -video_size "$XSZ" -i "$DISP" -frames:v 1 \
        "$OUT/${NAME}_$1.png" >/dev/null 2>&1; }

eval "$BODY"

kill "$DBOX" 2>/dev/null; pkill -f "Xvfb $DISP" 2>/dev/null
echo "shot: $(ls "$OUT/${NAME}"_*.png 2>/dev/null | wc -l) frame(s) in $OUT"
