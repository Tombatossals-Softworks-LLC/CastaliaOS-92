#!/usr/bin/env bash
# ======================================================================
# ci/stage-release.sh - refresh release/ from the tree, then verify it
# ----------------------------------------------------------------------
# release/ is the ready-to-run bundle committed into the repository so
# the project can be unzipped onto a DOS machine without a Watcom
# toolchain.  Staging it used to be written out twice - once in the
# Makefile's `release` target and once, copy-pasted, in the CI job that
# commits the fresh binary back.  Two copies of a copy step is exactly
# the drift ci/release.sh exists to catch, one level up: the CI copy went
# a whole round without restaging ASSETS, so a new icon reached users
# only if somebody remembered to do it by hand.
#
# Both callers run THIS script now.  It stages, then hands off to
# ci/release.sh to prove the result is faithful.
#
#   bash ci/stage-release.sh            stage and verify
#   bash ci/stage-release.sh --strict   also demand byte-parity with
#                                       the CASTALIA.EXE in the root
# ======================================================================
set -eu
cd "$(dirname "$0")/.."

for exe in CASTALIA.EXE INSTALL.EXE; do
  [ -f "$exe" ] || { echo "stage-release: $exe not built"; exit 1; }
done

cp CASTALIA.EXE  release/CASTALIA.EXE
cp INSTALL.EXE   release/INSTALL.EXE
cp README.TXT    release/README.TXT
cp CASTALIA.INI  release/CASTALIA.INI
cp INSTALL.BAT   release/INSTALL.BAT
cp CASTSHEL.BAT  release/CASTSHEL.BAT

# Staged fresh every time: a deleted icon has to disappear from the
# bundle too, and a mirror that only ever grows is not a mirror.
rm -rf release/ASSETS
mkdir -p release/ASSETS
cp -r assets/icons  release/ASSETS/ICONS
cp -r assets/media  release/ASSETS/MEDIA
cp -r assets/themes release/ASSETS/THEMES

bash ci/release.sh "$@"
