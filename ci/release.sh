#!/bin/sh
# ======================================================================
# ci/release.sh - is release/ still a faithful copy of what we build?
# ----------------------------------------------------------------------
# release/ is committed so the repository can be unzipped and run without
# a Watcom toolchain.  It is a hand-staged *copy*, and copies drift: its
# README sat three versions behind the real one, still telling people
# that Alt+Tab cycles windows - advice that stopped being true when the
# cycler moved to Shift+Tab because most BIOSes never deliver Alt+Tab.
# Nothing caught it, because nothing was looking.
#
# What is checked on every run:
#   * release/README.TXT is byte-identical to README.TXT
#   * release/ASSETS mirrors assets/ (same files, same bytes, FAT-cased)
#   * release/CASTALIA.EXE carries the current CAST_VERSION string
#   * release/INSTALL.EXE is present and is a DOS executable
#
# --strict additionally requires both release/ executables to be
# byte-identical to a freshly built pair.  The Watcom build is reproducible, so
# that comparison is meaningful - but it would also mean every source
# commit had to restage the binary, so it is reserved for tag builds,
# where the shipped bytes genuinely have to be the built bytes.
#
# `wmake release` is how you make any of this pass again.
# ======================================================================
set -u
cd "$(dirname "$0")/.."

strict=0
[ "${1:-}" = "--strict" ] && strict=1

fail=0
flag() { printf '  ! %s\n' "$1"; fail=1; }

echo "==> release/README.TXT matches README.TXT"
if [ ! -f release/README.TXT ]; then
  flag "release/README.TXT is missing"
elif ! cmp -s README.TXT release/README.TXT; then
  flag "release/README.TXT has drifted from README.TXT (run: wmake release)"
  diff README.TXT release/README.TXT | head -20
fi

# The other three files the bundle carries from the root: the shipped
# configuration (whose comments are also the INI manual), the installer
# and the shell wrapper.  Each is committed twice and nothing compared
# them, so a key documented in the root copy could be missing from the
# copy users actually get.  They are identical files; keep them so.
echo "==> release/ carries the current INI, batch installer and wrapper"
for f in CASTALIA.INI INSTALL.BAT CASTSHEL.BAT; do
  if [ ! -f "release/$f" ]; then
    flag "release/$f is missing"
  elif ! cmp -s "$f" "release/$f"; then
    flag "release/$f has drifted from $f (run: wmake release)"
    diff "$f" "release/$f" | head -10
  fi
done

# assets/<lower> is staged as release/ASSETS/<UPPER>.  The filenames are
# already 8.3 uppercase in the source tree; only the directory names are
# folded, so the mirror is a plain per-file byte comparison.
echo "==> release/ASSETS mirrors assets/"
for pair in icons:ICONS media:MEDIA themes:THEMES; do
  src="assets/${pair%%:*}"
  dst="release/ASSETS/${pair##*:}"
  if [ ! -d "$dst" ]; then
    flag "$dst is missing"
    continue
  fi
  for f in "$src"/*; do
    [ -e "$f" ] || continue
    b=$(basename "$f")
    if [ ! -f "$dst/$b" ]; then
      flag "$dst/$b is missing (present in $src)"
    elif ! cmp -s "$f" "$dst/$b"; then
      flag "$dst/$b differs from $src/$b"
    fi
  done
  for f in "$dst"/*; do
    [ -e "$f" ] || continue
    b=$(basename "$f")
    [ -f "$src/$b" ] || flag "$dst/$b is not in $src (stale)"
  done
done

# `strings` lives in binutils, which a minimal container need not have -
# and without this the check did not report "I cannot look", it reported
# "the binary is the wrong version", which is a different bug entirely.
# grep -a over the image answers the same question with no toolchain.
version_in_binary() {
  if command -v strings >/dev/null 2>&1; then
    strings -a "$1" | grep -qx "$2"
  else
    # tr splits the image on non-printables, which is what `strings` does;
    # -qx then demands the whole run be the version, so 0.5 cannot match
    # inside 0.56 and a stale binary cannot pass by coincidence.
    LC_ALL=C tr -c '[:print:]' '\n' < "$1" | grep -qx "$2"
  fi
}

# A release binary from an older version is the drift that actually hurts
# users: they run the EXE, not the sources.  The version string is baked
# into the About box, so grepping the image for it is enough to catch a
# binary that shipped a whole version behind.
echo "==> release/CASTALIA.EXE is built from this version"
V=$(grep -oE 'CAST_VERSION[[:space:]]+"[0-9.]+"' src/castalia.h |
    grep -oE '[0-9]+\.[0-9]+')
if [ -z "$V" ]; then
  flag "could not read CAST_VERSION from src/castalia.h"
elif [ ! -f release/CASTALIA.EXE ]; then
  flag "release/CASTALIA.EXE is missing"
elif ! version_in_binary release/CASTALIA.EXE "$V"; then
  flag "release/CASTALIA.EXE does not carry version $V (run: wmake release)"
else
  echo "    version $V"
fi

# The graphical installer is the second thing `wmake` builds, and until
# now it was the only build output nothing wanted: .gitignore hid it,
# staging skipped it, so the INSTALL.EXE that README.TXT and the press
# kit both describe reached no user.  It carries no version string of its
# own (it prints "Castalia 92 Setup", not a number), so what is checked
# here is that it is there and that it is a DOS executable.
echo "==> release/INSTALL.EXE is present and is a DOS executable"
if [ ! -f release/INSTALL.EXE ]; then
  flag "release/INSTALL.EXE is missing (run: wmake release)"
elif [ "$(head -c2 release/INSTALL.EXE)" != "MZ" ]; then
  flag "release/INSTALL.EXE is not an MZ (DOS) executable"
else
  echo "    $(wc -c < release/INSTALL.EXE | tr -d ' ') bytes"
fi

if [ "$strict" = "1" ]; then
  echo "==> release/ executables are byte-identical to the build (--strict)"
  for exe in CASTALIA.EXE INSTALL.EXE; do
    if [ ! -f "$exe" ]; then
      flag "$exe has not been built; --strict needs it"
    elif ! cmp -s "$exe" "release/$exe"; then
      flag "release/$exe is not the binary this tree builds"
    fi
  done
fi

if [ "$fail" != "0" ]; then
  echo "release: FAILED"
  exit 1
fi
echo "release: OK"
