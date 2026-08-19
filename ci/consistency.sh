#!/usr/bin/env bash
# ======================================================================
# ci/consistency.sh - CASTALIA/386 repository consistency checks
# ----------------------------------------------------------------------
# Guards against the drift that a manual, multi-file build system invites:
#   * the version in src/castalia.h must be echoed by the docs,
#   * every src/*.c must be wired into ALL THREE build definitions
#     (Makefile, BUILD.BAT, castalia.lnk) - a module compiled but not
#     linked, or linked in one build recipe but not another, is a classic
#     "works on my machine" trap,
#   * every src/*.h must be a Makefile header dependency (so a header edit
#     triggers the full rebuild the project relies on),
#   * the object list and the source list must be the same size.
#
# Usage:  bash ci/consistency.sh   (exit 0 = consistent, 1 = drift found)
# ======================================================================
set -u
cd "$(dirname "$0")/.."

fail=0
flag() { printf '  ! %s\n' "$1"; fail=1; }

echo "==> version string parity (castalia.h vs docs)"
V=$(grep -oE 'CAST_VERSION[[:space:]]+"[0-9.]+"' src/castalia.h | grep -oE '[0-9]+\.[0-9]+')
if [ -z "$V" ]; then
  flag "could not read CAST_VERSION from src/castalia.h"
else
  echo "    CAST_VERSION = $V"
  grep -q "Version $V" README.TXT         || flag "README.TXT does not mention 'Version $V'"
  grep -q "Version $V" release/RELEASE.TXT || flag "release/RELEASE.TXT does not mention 'Version $V'"
fi

# The About box states the module count, the line count and the
# warnings-as-errors flag.  All three had drifted: it claimed 59 modules
# when there were 64, and "warnings as errors (-wx)" long after that was
# found to be false - -wx is the warning LEVEL, the compiler prints the
# warning and exits 0.  Every other copy of that claim was corrected and
# this one was missed, because nothing was comparing it to anything.
# Settings offers a fixed list of wallpapers.  Two ways that rots: an
# entry whose file was renamed or removed (the user picks it and gets
# nothing), and a new image dropped into assets/ that nobody adds to the
# list (it ships and is unreachable).  Both are mechanically checkable,
# so neither has to be noticed by hand.
# SCREEN_W and SCREEN_H are runtime ints - 640 and 480 in Mode 12h - and
# int is SIXTEEN BITS here.  SCREEN_H * 73 is 35040, which wraps to
# -30496 and turns a height into a negative number; the control it sized
# then silently does not appear, with no error to notice.  splash.c has
# carried a comment about exactly this since fit_scale was written, and
# filedlg.c walked into it anyway, so it is a gate now rather than a
# thing people are expected to remember.
#   32767/640 = 51, 32767/480 = 68: anything above those needs a (long).
# FORMATS.TXT lists the internal verbs a user can type into Run.  The
# existing check above goes one way - every verb execute_command handles
# must be in INTERNAL_VERBS - and nothing checked that the DOCUMENTED
# ones exist at all.  Two did not: "music", promising an applet retired
# in 0.44, and "demos", a spelling of the Light Show nobody had wired up.
# Typing either got "No such command or program".
echo "==> every verb FORMATS.TXT documents is one execute_command handles"
for v in $(sed -n '/Internal verbs:/,/Anything else is run/p' docs/FORMATS.TXT |
           tr ' ' '\n' | tr -d '.,' | grep -E '^[a-z0-9]+$' |
           grep -vE '^(internal|verbs|anything|else|is|run|as|a|dos|program)$' |
           sort -u); do
  grep -q "streqi(command, \"$v\")" src/main.c ||
    flag "FORMATS.TXT documents the verb '$v'; execute_command does not handle it"
done

# And the mirror.  The check above stops the manual promising something
# that does not exist; this one stops something existing that the manual
# never mentions - a shipped applet with no documented way to launch it.
# Corral and the Typing Tutor arrived in 0.53 like that, and Help had
# been reachable only by clicking since it was written.
#
# The unit is the CLUSTER, not the verb.  Nearly every applet answers to
# two or three names (snake/serpent, patience/solitaire) and listing all
# of them would make the manual a thesaurus, so the rule is that at
# least ONE name per else-if branch must be documented.  The awk pulls
# each branch's condition - which may wrap onto a second line - and
# collects the string literals out of it.
echo "==> every applet execute_command opens has a verb FORMATS.TXT lists"
doc_verbs=" $(sed -n '/Internal verbs:/,/Anything else is run/p' docs/FORMATS.TXT |
              tr ' ' '\n' | tr -d '.,' | grep -E '^[a-z0-9]+$' | sort -u | tr '\n' ' ')"
# Deliberately undocumented: bsod/crash is an easter egg, and
# recentclear is what Start > Documents > Clear the list calls, not
# something anyone would type.
exempt=" bsod recentclear "
clusters=$(awk '
  /streqi\(command,/            { buf = buf $0; collecting = 1 }
  collecting && !/streqi\(command,/ { buf = buf $0 }
  collecting && /\{[[:space:]]*$/ {
    n = 0; s = buf; out = ""
    while (match(s, /streqi\(command, "[a-z0-9]+"\)/)) {
      v = substr(s, RSTART, RLENGTH)
      sub(/^streqi\(command, "/, "", v); sub(/"\)$/, "", v)
      out = out (n++ ? " " : "") v
      s = substr(s, RSTART + RLENGTH)
    }
    if (n) print out
    buf = ""; collecting = 0
  }
' src/main.c)
# The first draft of this check had a quoting bug that made awk exit
# with a syntax error, so it compared an empty list against the manual
# and passed.  A parser that finds nothing must be a failure, not a
# clean bill of health.
nclust=$(printf '%s\n' "$clusters" | grep -c '[a-z]')
echo "    $nclust applet branches in execute_command"
[ "$nclust" -ge 40 ] ||
  flag "only $nclust branches parsed out of execute_command - the awk above is broken, not main.c"
# Fed by here-doc, not a pipe: flag() sets a variable, and a while-read
# on the right of a | runs in a subshell where that assignment is lost.
# The check would print its complaint and still exit 0.
while read -r cluster; do
  [ -n "${cluster:-}" ] || continue
  case "$exempt" in *" ${cluster%% *} "*) continue ;; esac
  found=no
  for v in $cluster; do
    case "$doc_verbs" in *" $v "*) found=yes ;; esac
  done
  [ "$found" = yes ] ||
    flag "execute_command opens an applet for '$cluster'; FORMATS.TXT documents none of those names"
done <<EOF
$clusters
EOF

# A dialog is a FIXED 36 characters wide (dialog.c: font_adv() * 36), and
# draw_body elides anything past (36*adv - 16)/adv characters from the
# LEFT with "..", which for Mode 13h's 6-pixel advance is 33.  So a body
# line of 34 loses its first word and reads ".. disk may be full or
# read-only." - still a sentence, which is why nobody noticed: five of
# them had shipped, one losing eight characters.
#
# This is the same class as the Cardfile header that ran off the edge as
# "NOT SAVI".  Text that does not fit is not a rendering detail in a
# program whose error messages are the only thing telling you your work
# was not saved.  dialog_input's prompt sits on the same row and is
# elided the same way; none are over today, and this is how they stay
# that way.
echo "==> no dialog body line or prompt is wider than the dialog (33)"
python3 - <<'PYEOF' || flag "a dialog line or prompt is too long to fit"
import glob, re, sys
pat = re.compile(r'dialog_(?:message|confirm|input)\s*\((.*?)\)\s*;', re.S)
lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
bad = 0
for f in sorted(glob.glob('src/*.c')):
    src = open(f).read()
    for m in pat.finditer(src):
        args = lit.findall(m.group(1))
        line = src[:m.start()].count('\n') + 1
        for a in args[1:3]:
            if len(a) > 33:
                print("  ! %s:%d is %d chars: %s" % (f, line, len(a), a))
                bad = 1
sys.exit(bad)
PYEOF

# The 0.55 keyboard sweep found five controls the mouse could reach and
# the keyboard could not, three of which cost the user work rather than
# convenience - and the two applets it found with a click handler and no
# key handler at all were the Character Map (fixed then) and the Music
# Box (fixed in 0.56, once anything could open it).  The invariant it
# established is worth keeping rather than re-sweeping by hand.
#
# Display-only windows are fine: the Colors board and the System
# Inspector have no click handler either, so there is nothing to drive.
echo "==> every applet with a click handler has a key handler too"
python3 - <<'PYEOF' || flag "an applet can be driven by the mouse and not the keyboard"
import glob, re, sys
EXEMPT = {'wm_bar'}          # the taskbar and title bar, not an applet
clicks, keys = set(), set()
for f in glob.glob('src/*.h'):
    s = open(f).read()
    clicks |= set(re.findall(r'\b(\w+)_click\s*\(', s))
    keys   |= set(re.findall(r'\b(\w+)_key\s*\(', s))
if len(clicks) < 10:
    print("  ! only %d click handlers found - this check is broken" % len(clicks))
    sys.exit(1)
bad = 0
for m in sorted(clicks - keys - EXEMPT):
    print("  ! %s_click exists and %s_key does not - mouse only" % (m, m))
    bad = 1
sys.exit(bad)
PYEOF

# assoc_app_for() decides which applet opens a double-clicked file, and
# each of those applets has a Load box whose filter is a SEPARATE list
# written somewhere else - so they drift, and the applet ends up opening
# a file it then refuses to show you.  Two had: the Gramophone plays WAV
# and MID and its two pickers asked for *.WA? and *.* respectively, and
# the Scrap Box is registered for six extensions and offered *.TXT.
echo "==> every picker offers what its applet is registered to open"
python3 ci/assoc.py || flag "an applet opens a file type its own picker hides"

echo "==> every internal verb resolves to an icon of its own"
python3 ci/icons.py || flag "a verb falls through to the generic file icon"

echo "==> SCREEN_W/SCREEN_H multiplications cannot overflow a 16-bit int"
while IFS=: read -r f n text; do
  [ -n "${f:-}" ] || continue
  case "$text" in *"(long)"*) continue ;; esac
  dim=$(printf '%s' "$text" | grep -oE 'SCREEN_[WH][[:space:]]*\*[[:space:]]*[0-9]+' | head -1)
  [ -n "$dim" ] || continue
  mul=$(printf '%s' "$dim" | grep -oE '[0-9]+$')
  case "$dim" in
    *SCREEN_W*) lim=51 ;;
    *)          lim=68 ;;
  esac
  [ "$mul" -le "$lim" ] ||
    flag "$f:$n multiplies $dim with no (long) - overflows a 16-bit int"
done <<EOF
$(grep -nE 'SCREEN_[WH][[:space:]]*\*[[:space:]]*[0-9]+' src/*.c src/*.h 2>/dev/null)
EOF

echo "==> every wallpaper Settings offers exists, and every one shipped is offered"
for p in $(grep -oE 'ASSETS\\\\ICONS\\\\[A-Z0-9_]+\.(ICN|GIF)' src/settings.c |
           sed 's/ASSETS\\\\ICONS\\\\//' | sort -u); do
  [ -f "assets/icons/$p" ] ||
    flag "src/settings.c offers $p; assets/icons/$p does not exist"
done
for f in assets/icons/*.GIF; do
  b=$(basename "$f")
  grep -q "$b" src/settings.c ||
    flag "assets/icons/$b ships but Settings never offers it"
done

echo "==> the About box's own numbers match the tree"
mods=$(ls src/*.c | wc -l | tr -d ' ')
lines=$(cat src/*.c | wc -l | tr -d ' ')
for claimed in $(grep -oE '[0-9]+ (source )?modules' src/about.c |
                 grep -oE '^[0-9]+'); do
  [ "$claimed" = "$mods" ] ||
    flag "src/about.c says $claimed modules; src/*.c is $mods"
done
for claimed in $(grep -oE '[0-9]+,000\+ lines of C' src/about.c |
                 grep -oE '^[0-9]+'); do
  # The claim is "N,000+", so it must not OVERstate, and should not be
  # so far under that it is quaint.
  lo=$((claimed * 1000))
  hi=$((lo + 1999))
  if [ "$lines" -lt "$lo" ]; then
    flag "src/about.c claims ${claimed},000+ lines of C; src/*.c is $lines"
  elif [ "$lines" -gt "$hi" ]; then
    flag "src/about.c claims ${claimed},000+ lines of C; src/*.c is already $lines"
  fi
done
grep -q 'warnings as errors (-we)' src/about.c ||
  flag "src/about.c does not say warnings are errors with -we (-wx is only the LEVEL)"

echo "==> every verb execute_command() handles is listed in INTERNAL_VERBS"
# These two drifted apart twice.  A verb execute_command handles but
# is_internal does not know is worse than useless: a [shortcut] with
# freemem=true naming it UNLOADS the shell and hands the word to DOS, so
# "find" used to run DOS's own FIND.EXE.  Compare the two sets directly.
handled=$(sed -n '/^static void execute_command/,/^static const char \* const far INTERNAL_VERBS/p' src/main.c \
          | grep -oE 'streqi\(command, "[^"]+"\)' | sed 's/.*"\(.*\)".*/\1/' | sort -u)
listed=$(sed -n '/^static const char \* const far INTERNAL_VERBS/,/^};/p' src/main.c \
          | grep -oE '"[^"]+"' | tr -d '"' | sort -u)
missing=$(comm -23 <(echo "$handled") <(echo "$listed"))
extra=$(comm -13 <(echo "$handled") <(echo "$listed"))
if [ -n "$missing" ]; then
  flag "verbs handled by execute_command but missing from INTERNAL_VERBS:"
  echo "$missing" | sed 's/^/      /'
fi
if [ -n "$extra" ]; then
  flag "verbs in INTERNAL_VERBS that execute_command never handles:"
  echo "$extra" | sed 's/^/      /'
fi
if [ -z "$missing" ] && [ -z "$extra" ]; then
  echo "    $(echo "$listed" | wc -l | tr -d ' ') verbs, both lists agree"
fi

echo "==> every window kind is registered in window.c's applet table"
# The table drives draw, keys and right-click.  A kind added to window.h
# and forgotten here silently loses all three - the same bug class the
# verb guard above exists to stop, so it gets the same treatment.
allkinds=$(grep -oE '^#define (WIN_[A-Z0-9_]+)' src/window.h \
           | awk '{print $2}' | grep -vE 'WIN_KIND_COUNT' | sort -u)
tabled=$(sed -n '/^static const AppEntry far g_app\[\]/,/^};/p' src/window.c \
         | grep -oE 'WIN_[A-Z0-9_]+' | sort -u)
# WIN_GENERIC and WIN_SYSINFO deliberately use the generic text page.
exempt="WIN_GENERIC
WIN_SYSINFO
WIN_HELP"
need=$(comm -23 <(echo "$allkinds") <(echo "$tabled") | comm -23 - <(echo "$exempt" | sort))
if [ -n "$need" ]; then
  flag "window kinds missing from g_app[] (no draw, keys or right-click):"
  echo "$need" | sed 's/^/      /'
else
  echo "    $(echo "$tabled" | wc -l | tr -d ' ') kinds registered"
fi

echo "==> every src/*.c is wired into Makefile + BUILD.BAT + castalia.lnk"
for c in src/*.c; do
  b=$(basename "$c" .c)
  grep -q "$b\.obj"  Makefile      || flag "Makefile: $b.obj not listed"
  grep -qiE "\b$b\b" BUILD.BAT     || flag "BUILD.BAT: $b not listed"
  grep -qiE "\b$b\b" castalia.lnk  || flag "castalia.lnk: $b not listed"
done

# The two build paths must agree on the FLAGS, not just the module list.
# BUILD.BAT sat at -wx alone for a whole round after the Makefile gained
# -we, while BUILD.TXT claimed both were warnings-fatal - so the
# native-DOS path still had the exact hole that had just been closed.
echo "==> Makefile and BUILD.BAT agree that warnings are fatal (-we)"
grep -q -- "-we" Makefile  || flag "Makefile: -we missing (warnings not fatal)"
for n in $(grep -c -- "wcc " BUILD.BAT); do :; done
bad=$(grep -- "wcc " BUILD.BAT | grep -vc -- "-we" || true)
[ "${bad:-0}" -eq 0 ] || flag "BUILD.BAT: $bad wcc line(s) without -we"

echo "==> every src/*.h is a Makefile header dependency"
for h in src/*.h; do
  b=$(basename "$h")
  grep -q "$b" Makefile || flag "Makefile HDRS: $b not listed"
done

echo "==> object count matches source count"
# (insmain.obj belongs to the separate INSTALL.EXE, not the shell link)
nc=$(ls src/*.c | wc -l | tr -d ' ')
no=$(grep -oE '[a-z0-9_]+\.obj' Makefile | grep -v '^insmain\.obj$' | sort -u | wc -l | tr -d ' ')
echo "    $nc source files, $no distinct objects"
[ "$nc" = "$no" ] || flag "source count ($nc) != Makefile object count ($no)"

echo "==> every numbered screenshot is indexed in README.TXT"
# The index in README.TXT is the only catalogue of what the shots show,
# and it is hand-written.  It had silently skipped from 23 to 28: the
# System Inspector, the Benchmark, the Music Box and the minigames were
# captured, committed, and then described nowhere.
on_disk=$(ls docs/screenshots | grep -E '^[0-9]{2}-.*\.png$' | sed 's/\.png$//' | sort)
indexed=$(grep -oE '^    [0-9]{2}-[a-z0-9-]+\.png' README.TXT |
          sed 's/^ *//; s/\.png$//' | sort -u)
unindexed=$(comm -23 <(echo "$on_disk") <(echo "$indexed"))
phantom=$(comm -13 <(echo "$on_disk") <(echo "$indexed"))
if [ -n "$unindexed" ]; then
  flag "screenshots on disk that README.TXT never mentions:"
  echo "$unindexed" | sed 's/^/      /'
fi
if [ -n "$phantom" ]; then
  flag "screenshots README.TXT indexes that are not in docs/screenshots:"
  echo "$phantom" | sed 's/^/      /'
fi
[ -n "$unindexed$phantom" ] || \
  echo "    $(echo "$on_disk" | wc -l | tr -d ' ') screenshots, all indexed"

echo "==> README.md carries the current version"
# README.md is the shop window - the page every visitor reads first, and
# the copy furthest from the code.  Its version badge is checked here for
# the same reason release/README.TXT is: nothing else would notice.
if [ ! -f README.md ]; then
  flag "README.md is missing"
elif [ -n "$V" ] && ! grep -q "$V" README.md; then
  flag "README.md does not mention version $V"
fi

if [ "$fail" -eq 0 ]; then echo "consistency: OK"; else echo "consistency: FAILED"; fi
exit "$fail"
