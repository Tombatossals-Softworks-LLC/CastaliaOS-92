#!/usr/bin/env bash
# ======================================================================
# ci/memory.sh - the DGROUP watermark for CASTALIA/386
# ----------------------------------------------------------------------
# The medium memory model gives the whole program ONE 64 KB near-data
# segment (DGROUP): all near statics, the 8 KB stack AND the near heap
# that stdio buffers and malloc live in.  wlink refuses to link past
# 64 KB, but long before that the shrinking near heap causes subtle
# runtime failures (fopen returning NULL is the classic).  This script
# reads the Groups table of the map file the build already writes and
# fails the build when DGROUP grows past the watermark, so the memory
# diet the 1.0 roadmap calls for has a gauge - and a tripwire.
#
# Where the space actually goes, measured off the map at 0.55:
#   CONST  19752   string literals and const data
#   _BSS   18990   uninitialised near statics
#   STACK   8192
#   CONST2  2677
#   _DATA   2634
#
# CONST is the biggest single item, and the obvious lever does NOT work:
# wcc's -zc ("place literal strings in the code segment") leaves CONST
# byte-identical here - built clean with and without it, 0x4d28 both
# times - so it is not worth adding to CFLAGS and should not be tried
# again.  Nineteen kilobytes of strings spread across sixty-four modules
# is simply what the program says.  The lever that DOES work is the one
# this codebase already uses: mark individual objects `far` by hand,
# which is deliberate, greppable, and checked by ci/nearfar.sh.  A global
# -zt threshold would relocate arrays silently and lean on that gate to
# catch every truncation it caused; not worth it while the budget holds.
#
# Usage:  bash ci/memory.sh [castalia.map]   (exit 0 = within budget)
# ======================================================================
set -u
MAP="${1:-castalia.map}"

# Fail-threshold: past this, the near heap left over for stdio/malloc
# is too small to trust.  (64 KB total - 8 KB stack = 56 KB hard use;
# flagging at 56 KB leaves ~8 KB of heap headroom at all times.)
BUDGET=57344

if [ ! -f "$MAP" ]; then
  echo "memory: no $MAP found - build first (wlink writes it)"
  exit 1
fi

# The wlink map's Groups table reads:  DGROUP  <seg:off>  <hex size>
LINE=$(grep -E '^DGROUP[[:space:]]' "$MAP" | head -1)
HEX=$(printf '%s\n' "$LINE" | awk '{print $NF}' | tr -d '\r')

case "$HEX" in
  *[!0-9a-fA-F]* | "")
    echo "memory: could not parse the DGROUP size out of $MAP"
    echo "        (Groups table line was: '${LINE:-<missing>}')"
    echo "        FAILING, not passing: this gate guards the scarcest"
    echo "        resource in the build, and a wlink map-format change"
    echo "        must not silently switch it off.  Fix the parser."
    exit 1
    ;;
esac

USED=$((16#$HEX))
# Report against the BUDGET, which is the line that actually fails the
# build - measuring the gap to the 64 KB segment ceiling instead reported
# more than double the real headroom, which is exactly backwards for a
# gauge whose whole job is to inform the next diet decision.
FREE=$((BUDGET - USED))
PCT=$((USED * 100 / BUDGET))

echo "==> DGROUP watermark (near data: statics + 8 KB stack + near heap)"
printf '    used %6d bytes of the %d-byte budget (%d%%), %d to spare\n' \
       "$USED" "$BUDGET" "$PCT" "$FREE"

if [ "$USED" -gt "$BUDGET" ]; then
  echo "  ! DGROUP is past the $BUDGET-byte watermark."
  echo "    Move big tables to far memory (the demo.c/media.c pattern)"
  echo "    or shrink buffers - the near heap is about to starve."
  exit 1
fi

# And the OTHER budget, which moving tables to far memory spends instead.
# Far data lands in the image, so the .EXE is what comes out of the 640 KB
# the machine has - and the back buffer needs 64 KB of what is left AFTER
# the program loads.  Measured in DOSBox with the System Inspector: this
# tree reports 89 KB of conventional memory free once the shell is up and
# the buffer is allocated, and the previous release reported 97 KB, the
# difference being exactly the 8 KB that doubling the Disk Cabinet's and
# the picker's directory tables cost.
#
# So the DGROUP gauge alone is only half the picture, and "move it to far
# memory" is not free - it is a transfer.  460800 leaves room to grow and
# still catches the kind of growth that would push a real 1 MB machine
# into refusing to allocate the back buffer at all.
EXE_MAX=460800
if [ -f CASTALIA.EXE ]; then
  ESZ=$(wc -c < CASTALIA.EXE | tr -d ' ')
  echo "==> image size (far data lands here, and here comes out of the 640 KB)"
  printf '    CASTALIA.EXE is %d bytes of the %d-byte watermark\n' \
         "$ESZ" "$EXE_MAX"
  if [ "$ESZ" -gt "$EXE_MAX" ]; then
    echo "  ! the image is past the $EXE_MAX-byte watermark."
    echo "    Every byte here is one fewer for the 64 KB back buffer and"
    echo "    for anything launched without the freemem wrapper."
    exit 1
  fi
fi
echo "memory: OK"
