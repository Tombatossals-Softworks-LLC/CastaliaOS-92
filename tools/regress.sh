#!/usr/bin/env bash
# ======================================================================
# tools/regress.sh - behavioural regression tests, driven through DOSBox
# ----------------------------------------------------------------------
# tests/ checks the file-format parsers against a host shim.  ci/smoke.sh
# checks that the program boots.  Neither touches what the program DOES,
# so every behavioural fix in 0.55 was verified once, by hand, and then
# guarded by nothing at all.
#
# These cases are the ones where a regression costs the user their work.
# Each drives the real binary under DOSBox and then asserts on the DISK,
# not on pixels: what is on C: afterwards is deterministic, a screenshot
# is not.  Every one of them corresponds to a bug that was actually in
# the shipped program.
#
# Requires: dosbox, Xvfb, ffmpeg, xdotool (same as tools/shot.sh).
#
# The helpers are run through an explicit interpreter - bash shot.sh,
# python3 pngdiff.py - and not as bare paths.  As bare paths every case
# died instantly on a fresh clone with 'Permission denied': the files are
# committed without the executable bit, so the suite only ever ran for
# someone who had chmod'd them by hand.  Ten of eleven cases 'failed' that
# way the first time this ran in CI, and the eleventh PASSED, because it
# asserts on a file being absent.  A checkout can lose the bit anyway -
# FAT and Windows do not carry it, which is not a hypothetical audience
# for a DOS shell - so naming the interpreter is the fix that keeps.
# Slow by nature - a DOSBox boot per case, about half a minute each.
#
#   tools/regress.sh            run all
#   tools/regress.sh save tag   run the named cases
# ======================================================================
set -u
SELF="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SELF/.." && pwd)"
OUT="$ROOT/shots/regress"
export OUT

pass=0; fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1"; fail=$((fail + 1)); }
seed() { rm -rf "$OUT/seed_$1"; mkdir -p "$OUT/seed_$1"; }
disk() { echo "$OUT/disk_$1"; }

# Open My Computer and descend into C:, the preamble most cases need.
# The coordinates are DOS-screen positions minus the ~48px the DOSBox
# window is offset by - see the note in tools/shot.sh.
CABINET='
xdotool mousemove --window "$W" 60 52; sleep 0.4
xdotool click --window "$W" --repeat 2 --delay 90 1; sleep 4
xdotool mousemove --window "$W" 179 92; sleep 0.4
xdotool click --window "$W" --repeat 2 --delay 90 1; sleep 3
'

# ---------------------------------------------------------------- save
# A document typed and saved with the mouse untouched.  New/Load/Save
# were click-only until 0.55: you could compose a document and have no
# way to keep it.
case_save() {
    seed save
    timeout 300 bash "$SELF/shot.sh" save :81 scrap "" -- '
      xdotool mousemove --window "$W" 60 52; sleep 0.4
      xdotool click --window "$W" --repeat 2 --delay 90 1; sleep 4
      T "regression"
      K F2 2.0
      T "R.TXT"
      K Return 2.5
      sleep 2
    ' >/dev/null 2>&1
    if grep -q "regression" "$(disk save)/R.TXT" 2>/dev/null
    then ok "save: F2 writes the document without a mouse"
    else bad "save: F2 did not produce R.TXT with the typed text"; fi
}

# ----------------------------------------------------------------- tag
# Tag three of five and delete: exactly those three, one confirmation.
case_tag() {
    seed tag
    for n in ALPHA BRAVO CHARLIE DELTA ECHO; do
        printf '%s\n' "$n" > "$OUT/seed_tag/$n.TXT"
    done
    timeout 300 bash "$SELF/shot.sh" tag :82 fileman "" -- "$CABINET"'
      K a 0.8
      K space 0.7; K space 0.7
      K Down 0.6; K Down 0.6; K space 0.7
      K Delete 1.8
      K Return 2.5
      sleep 2
    ' >/dev/null 2>&1
    d=$(disk tag)
    if [ ! -e "$d/ALPHA.TXT" ] && [ ! -e "$d/BRAVO.TXT" ] &&
       [ ! -e "$d/CHARLIE.TXT" ] && [ -e "$d/DELTA.TXT" ] &&
       [ -e "$d/ECHO.TXT" ] && [ -e "$d/CASTALIA.EXE" ]
    then ok "tag: Del removes the tagged three and nothing else"
    else bad "tag: wrong set deleted"; fi
}

# ---------------------------------------------------------------- bulk
# Tagged copy into a folder: files arrive, originals stay.
case_bulk() {
    seed bulk
    mkdir -p "$OUT/seed_bulk/DEST"
    printf 'A\n' > "$OUT/seed_bulk/ALPHA.TXT"
    printf 'B\n' > "$OUT/seed_bulk/BRAVO.TXT"
    timeout 300 bash "$SELF/shot.sh" bulk :83 fileman "" -- "$CABINET"'
      K a 0.8; K space 0.7; K space 0.7
      C 313 64
      sleep 1.5
      K Down 0.7; K Return 2.0
      sleep 1
      C 386 314
      sleep 2.5
    ' >/dev/null 2>&1
    d=$(disk bulk)
    if [ -s "$d/DEST/ALPHA.TXT" ] && [ -s "$d/DEST/BRAVO.TXT" ] &&
       [ -s "$d/ALPHA.TXT" ] && [ -s "$d/BRAVO.TXT" ]
    then ok "bulk: tagged copy lands in the folder, originals kept"
    else bad "bulk: copy did not produce both files on both sides"; fi
}

# ---------------------------------------------------------------- self
# Copying into the folder the files are already in must be REFUSED.
# Unguarded, fopen(dst,"wb") truncates before the read starts and the
# files come back empty - which is exactly what shipped for one commit.
case_self() {
    seed self
    printf 'MUST SURVIVE\n' > "$OUT/seed_self/ALPHA.TXT"
    printf 'ALSO SURVIVES\n' > "$OUT/seed_self/BRAVO.TXT"
    # The Return matters.  THREE independent things stop a same-folder
    # copy - bulk_to_folder's folder check, copy_file's own path check,
    # and the "N files are there already. Replace them?" prompt - and
    # without answering that prompt the run stops at a dialog and the
    # files survive no matter what the guards do.  A test that passes
    # because it never reached the dangerous code is not a test; this
    # was verified by disabling both guards and watching it still pass.
    timeout 300 bash "$SELF/shot.sh" self :84 fileman "" -- "$CABINET"'
      K a 0.8; K space 0.7; K space 0.7
      C 313 64
      sleep 1.5
      C 386 314
      sleep 2.0
      K Return 2.5
      sleep 2
    ' >/dev/null 2>&1
    d=$(disk self)
    if grep -q "MUST SURVIVE" "$d/ALPHA.TXT" 2>/dev/null &&
       grep -q "ALSO SURVIVES" "$d/BRAVO.TXT" 2>/dev/null
    then ok "self: copying into their own folder leaves them intact"
    else bad "self: files were emptied by a same-folder copy"; fi
}

# ---------------------------------------------------------------- deck
# A CARDFILE.DAT longer than the sixteen-card table must not be written
# back short.  Flipping a card is enough to trigger the save.
case_deck() {
    seed deck
    python3 -c "
import sys
cards = ['CARD %02d' % i for i in range(1, 21)]
open(sys.argv[1], 'w').write('\f\n'.join(cards) + '\f\n')" \
        "$OUT/seed_deck/CARDFILE.DAT"
    before=$(md5sum < "$OUT/seed_deck/CARDFILE.DAT")
    timeout 300 bash "$SELF/shot.sh" deck :85 cardfile "" -- '
      xdotool mousemove --window "$W" 60 52; sleep 0.4
      xdotool click --window "$W" --repeat 2 --delay 90 1; sleep 4
      K Next 1.2; K Next 1.2
      T "edited"
      sleep 1.5
    ' >/dev/null 2>&1
    after=$(md5sum < "$(disk deck)/CARDFILE.DAT" 2>/dev/null || echo none)
    if [ "$before" = "$after" ]
    then ok "deck: a twenty-card file is not rewritten short"
    else bad "deck: CARDFILE.DAT was modified - cards lost"; fi
}

# ----------------------------------------------------------------- m12
# The picker, in Mode 12h.  Sizing its height with SCREEN_H * 73 overflows
# a 16-bit int at 480, comes out negative, and the picker DRAWS NOTHING
# while Mode 13h stays perfect.
#
# This case asserts on PIXELS, alone among these, and it has to.  The
# first version drove a keyboard save through the picker and checked the
# file on disk - and it passed with the overflow reintroduced, because a
# picker with a negative height still runs its modal loop: invisible but
# alive, so a blind Type-name-Enter writes the same file as a visible one
# would.  The disk cannot tell those apart.  What separates them is
# whether anything APPEARED, so this grabs the screen either side of the
# F2 and requires a good fraction of it to have changed - which is
# independent of where the dialog lands and of the active theme.
case_m12() {
    seed m12
    VIDEO=mode12h XSIZE=720x580 \
    timeout 300 bash "$SELF/shot.sh" m12 :86 scrap "" -- '
      K Down 1.2; K Return 5.0
      sleep 3
      SNAP before
      K F2 3.0
      sleep 1
      SNAP after
    ' >/dev/null 2>&1
    changed=$(python3 "$SELF/pngdiff.py" "$OUT/m12_before.png" "$OUT/m12_after.png")
    changed=${changed:-0}
    if [ "$changed" -ge 10 ]
    then ok "m12: the picker appears in Mode 12h ($changed% of the screen changed)"
    else bad "m12: F2 drew nothing ($changed% changed) - picker invisible"; fi
}

# ---------------------------------------------------------------- quit
# Shut Down with unsaved preferences must ASK.  Settings applies every
# change live, so the desktop already wears the new theme while
# CASTALIA.INI still holds the old one; wm_close_id() grew a prompt for
# that and ok_to_quit() never did, so closing the window asked and
# quitting the shell did not.
#
# The control boot is not optional.  The assertion for each fix is that
# EXITED.TXT is ABSENT - the shell is still up, held by the prompt - and
# absence is also what you get when the script never reached Shut Down
# at all.  So the first run drives the identical tail with nothing
# dirtied and requires the file to BE there; without it, all these
# cases would pass just as happily against a shell that crashed on
# boot.  If the control fails the rest are skipped and said to prove
# nothing, rather than reported as passes.
#
#   F10 Up Enter   the Dominus menu, last row, "Shut Down..."
#   Enter          "Leave Castalia and return to DOS?" -> Yes
#   Escape         one key does the right thing in both runs: on the
#                  clean one it dismisses the "safe to turn off your
#                  computer" farewell, which WAITS for a key and is why
#                  the first draft of this case saw no EXITED.TXT even
#                  when the shell had agreed to quit; on the dirty one
#                  it answers No to the preferences prompt.  So the two
#                  runs send byte-identical keys apart from the two that
#                  dirty Settings, and nothing else can explain a
#                  difference in the result.
#              $1 name  $2 display  $3 icon  $4 keys  $5 extra keys after
#              the shut-down Yes, for a case whose save pops its own
#              dialog on the way out and must dismiss it first
quit_run() {
    timeout 300 bash "$SELF/shot.sh" "$1" "$2" "$3" "" -- '
      K Down 1.0
      K Return 4.0
      '"$4"'
      K F10 2.0
      K Up 1.0
      K Return 2.5
      K Return 3.0
      '"${5:-}"'
      K Escape 2.0
      sleep 3
    ' >/dev/null 2>&1
}
case_quit() {
    seed quitclean
    quit_run quitclean :87 settings ""
    if [ -e "$(disk quitclean)/EXITED.TXT" ]
    then ok "quit: Shut Down with nothing unsaved returns to DOS"
    else bad "quit: the control run never reached Shut Down - the cases below prove nothing"
         return; fi

    seed quitdirty
    quit_run quitdirty :88 settings 'K Down 1.0
      K Return 3.0'
    if [ ! -e "$(disk quitdirty)/EXITED.TXT" ]
    then ok "quit: unsaved preferences stop the shell leaving"
    else bad "quit: Shut Down discarded unsaved preferences without asking"; fi

    # The Agenda writes on every change and so has no dirty flag; what it
    # has instead is a save that DECLINES when AGENDA.TXT holds more
    # entries than the fourteen-row table, because rewriting it from the
    # truncated table would delete the rest.  Twenty entries here, then
    # Space to tick a box: the tick is real on screen and reaches no
    # disk, and the red banner saying so dies with the window.
    seed quitlost
    python3 -c "
import sys
open(sys.argv[1], 'w').write(''.join('[ ] ITEM %02d\n' % i
                                     for i in range(1, 21)))" \
        "$OUT/seed_quitlost/AGENDA.TXT"
    quit_run quitlost :90 agenda 'K space 1.5'
    if [ ! -e "$(disk quitlost)/EXITED.TXT" ]
    then ok "quit: an edit the Agenda could not save stops the shell leaving"
    else bad "quit: Shut Down discarded an unsavable Agenda edit without asking"; fi

    # A save that CANNOT OPEN ITS FILE - the other way a save does not
    # happen, and the one nothing had ever exercised: card.c returned
    # from fopen(NULL) in silence until 0.56, while agenda.c had always
    # reported the identical condition.
    #
    # CARDFILE.DAT is a DIRECTORY here, and that is not a joke: chmod 444
    # was the obvious way and it does nothing, because this container
    # runs as root and root ignores the permission bits - the first
    # version of this case passed the file straight through to the disk
    # with "edited" written into it.  Nothing can fopen a directory for
    # writing, root included, and the code path taken is the one under
    # test: fopen returns NULL.
    #
    # The deck loads empty, so card_open lays down its welcome card and
    # marks it dirty; PgDn then flushes (go_card commits on the way out
    # of a card) and the flush fails.  The first Return dismisses the
    # "Could not write" message, the extra Return dismisses it again on
    # the way out, and the Escape answers No to "the deck could not be
    # saved, leave anyway?".
    seed quitro
    mkdir -p "$OUT/seed_quitro/CARDFILE.DAT"
    quit_run quitro :91 cardfile 'T "edited"
      K Next 2.0
      K Return 1.5' 'K Return 2.0'
    if [ ! -e "$(disk quitro)/EXITED.TXT" ]
    then ok "quit: a deck on a write-protected file stops the shell leaving"
    else bad "quit: Shut Down discarded an unwritable Cardfile without asking"; fi
}

# ---------------------------------------------------------------- full
# A copy onto a FULL DISK must not leave the wreckage behind.
#
# copy_file reports the failure - "Copy failed, the disk may be full or
# write protected" - and then left a truncated file at the destination
# under the name the user chose, which op_copy may just have talked them
# into overwriting a real file to free up.  The tidy-up existed; the
# short-write branch returned before reaching it, and a short write is
# the FIRST thing a full disk does.
#
# This is the one case that needs a disk image.  A DOSBox directory
# mount passes writes through to the host, so C: never runs out of room
# however small you tell DOS it is - "mount -freesize" changes only what
# INT 21h AH=36h reports.  So A: is a real 160 KB FAT12 image (built by
# tools/mkfloppy.py, since this container has no mkfs.fat or mtools) and
# the file copied onto it is CASTALIA.EXE, comfortably twice its size.
# The assertion reads the image's root directory afterwards.
case_full() {
    seed full
    SMALLA=160 timeout 300 bash "$SELF/shot.sh" full :89 fileman "" -- '
      K Down 1.0
      K Return 4.0
      K Right 0.8
      K Return 3.0
      K c 1.0
      K F5 3.0
      SNAP before
      # Backspace EDITS THE NAME here - the picker is in save mode, so it
      # is not the go-up key it is in the Disk Cabinet.  Clear the
      # pre-filled COPY_OF.EXE and type an absolute path instead.
      for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
        xdotool key --clearmodifiers --window "$W" BackSpace; sleep 0.12
      done
      sleep 0.6
      T "A:\\BIG.EXE"
      K Return 8.0
      sleep 4
      SNAP after
    ' >/dev/null 2>&1
    left=$(python3 "$SELF/fatls.py" "$OUT/a_full.img" 2>/dev/null)
    # An empty floppy is what a REFUSED copy leaves - and also what a run
    # that never started leaves.  This case asserted on the absence alone
    # and so reported success the first time the suite ran in CI, where
    # every case had died on a "Permission denied" before DOSBox was even
    # launched: ten cases failed honestly, this one passed for the wrong
    # reason.  The screen is the positive half.  Between the open picker
    # and the finished attempt the display must have MOVED; a run that
    # never booted compares two identical black frames and scores 0.
    #
    # The bar is ANY movement, not a percentage worth tuning.  A refused
    # copy repaints a small dialog and about 3% of the screen, measured
    # - so a threshold set near that number would be a flake waiting for
    # a dialog to move two pixels, while the thing being ruled out here
    # scores exactly zero.  0 vs not-0 is the whole question.
    changed=$(python3 "$SELF/pngdiff.py" "$OUT/full_before.png" \
                                         "$OUT/full_after.png")
    changed=${changed:-0}
    if [ -n "$left" ]; then
        bad "full: the failed copy left $left on A:"
    elif [ "$changed" -lt 1 ]; then
        bad "full: the shell never got as far as trying (${changed}% of the screen changed) - this case proves nothing"
    else
        ok "full: a copy that runs out of disk leaves nothing behind (${changed}% of the screen changed)"
    fi
}

# -------------------------------------------------------------- nosave
# A save that FAILED must not mark the document saved.
#
# The Scrap Box clears its dirty flag only when scrap_save returns TRUE,
# and that is the whole defence: fopen("w") truncates before a byte goes
# out, so a full disk leaves a short file on disk while the real document
# is still in memory - and if the applet believed it was saved, Shut Down
# would take it without asking and the only copy left would be the
# truncated one.  hiscore.c carries a comment about the same mistake
# being made there once.
#
# A: is a 160 KB image with about a kilobyte free, because an EMPTY 160 KB
# floppy is not full enough: the Scrap Box caps a document at 4 KB.
case_nosave() {
    seed nosave
    SMALLA=160 SMALLFREE=1000 \
    timeout 300 bash "$SELF/shot.sh" nosave :92 scrap "" -- '
      K Down 1.0
      K Return 4.0
      for i in $(seq 1 40); do
        xdotool type --window "$W" --delay 6 \
          "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
      done
      sleep 1
      K F2 3.0
      for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
        xdotool key --clearmodifiers --window "$W" BackSpace; sleep 0.1
      done
      sleep 0.5
      T "A:\\BIG.TXT"
      K Return 6.0
      K Return 2.0
      K F10 2.0
      K Up 1.0
      K Return 2.5
      K Return 3.0
      K Escape 2.0
      sleep 3
    ' >/dev/null 2>&1
    short=$(python3 "$SELF/fatls.py" "$OUT/a_nosave.img" 2>/dev/null | grep -c "^BIG.TXT")
    if [ ! -e "$(disk nosave)/EXITED.TXT" ] && [ "$short" = 1 ]
    then ok "nosave: a save that ran out of disk leaves the document unsaved"
    else bad "nosave: the failed save was treated as done (exited=$( [ -e "$(disk nosave)/EXITED.TXT" ] && echo yes || echo no), file=$short)"; fi
}

# ------------------------------------------------------------ scrapbig
# A ten-kilobyte document survives a load and a save unchanged.
#
# The Scrap Box buffer lives in FAR memory as of 0.56 - it was the
# largest thing left in DGROUP, which the memory gate had at 93% - and
# holds 16 KB instead of 4 KB.  The medium model passes a plain char *
# as NEAR, so fread, fwrite and memmove cannot see a far buffer: the
# load and save now stage through a 512-byte near buffer and the insert
# and delete moves are hand-rolled.  Four places where a wrong segment
# or an off-by-one silently mangles the user's document.
#
# md5 in equals md5 out is the whole assertion, and it covers the CR
# strip on the way in and the CRLF re-expansion on the way out as well.
# The file is 10,440 bytes: over the OLD 4 KB cap, so this case could not
# even have been written before the change.
case_scrapbig() {
    seed scrapbig
    python3 -c "
import sys
lines = ['Line %04d - the quick brown fox jumps over the lazy dog.' % i
         for i in range(1, 181)]
open(sys.argv[1], 'wb').write(('\r\n'.join(lines) + '\r\n').encode())" \
        "$OUT/seed_scrapbig/BIG.TXT"
    before=$(md5sum < "$OUT/seed_scrapbig/BIG.TXT")
    # The Save button is clicked, not Enter: with a name already in the
    # field and the selection sitting on a FOLDER, Enter descends into it
    # rather than accepting - see the picker's key handler.
    timeout 300 bash "$SELF/shot.sh" scrapbig :93 scrap "" -- '
      K Down 1.0
      K Return 4.0
      K F3 3.0
      K Down 1.0
      K Return 5.0
      K F2 3.0
      C 386 314
      sleep 2
      K Return 4.0
      sleep 3
    ' >/dev/null 2>&1
    after=$(md5sum < "$(disk scrapbig)/BIG.TXT" 2>/dev/null || echo none)
    if [ "$before" = "$after" ]
    then ok "scrapbig: a 10 KB document loads and saves back unchanged"
    else bad "scrapbig: the document did not survive the round trip"; fi
}

# ----------------------------------------------------------- slowkeys
# Typing must not lose characters on the machine this shell is FOR.
#
# The main loop handled ONE key per pass, which is plenty at Mode 13h
# speeds on anything modern and not plenty on a 386SX in Mode 12h: a
# compose there takes long enough that a typist outruns the frame rate,
# the fifteen-key BIOS buffer fills, and the BIOS throws the rest away.
# dialog.c and filedlg.c had the same shape, and a character dropped
# out of a FILENAME is not cosmetic - it saves to the wrong file.
#
# cycles=1100 is about a 386SX/16, which is the low end of what the
# README claims to run on.  Slow by design: this case exists precisely
# because everything else runs at 20000.
case_slowkeys() {
    seed slowkeys
    CYCLES=1100 VIDEO=mode12h XSIZE=720x580 \
    timeout 400 bash "$SELF/shot.sh" slowkeys :94 scrap "" -- '
      sleep 8
      K Down 2.5
      K Return 12.0
      T "the quick brown fox jumps over the lazy dog"
      sleep 4
      K F2 6.0
      T "K.TXT"
      sleep 2
      K Return 8.0
      sleep 4
    ' >/dev/null 2>&1
    got=$(cat "$(disk slowkeys)/K.TXT" 2>/dev/null | tr -d '\r\n')
    if [ "$got" = "the quick brown fox jumps over the lazy dog" ]
    then ok "slowkeys: nothing is dropped when typing at 386SX speed"
    else bad "slowkeys: keys were lost - got [$got]"; fi
}

ALL="save tag bulk self deck m12 quit full nosave scrapbig slowkeys"
for c in ${*:-$ALL}; do
    case " $ALL " in
      *" $c "*) "case_$c" ;;
      *) echo "  ?     no such case: $c"; fail=$((fail + 1)) ;;
    esac
done

echo "$((pass + fail)) run, $fail failed"
[ "$fail" -eq 0 ]
