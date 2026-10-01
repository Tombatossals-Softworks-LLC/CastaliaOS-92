#!/usr/bin/env bash
# ======================================================================
# ci/relnotes.sh [version] - the release notes for one version, from the
# only place they are already written down: README.TXT's RELEASE HISTORY.
# ----------------------------------------------------------------------
# The history in README.TXT ships with the bundle and is written per
# release anyway.  Re-typing it into a GitHub release body is how the two
# start disagreeing, so the tag notes are cut from the README instead.
#
# With no argument the version is read from src/castalia.h.
#
#   bash ci/relnotes.sh        notes for the current CAST_VERSION
#   bash ci/relnotes.sh 0.55   notes for a specific version
# ======================================================================
set -u
cd "$(dirname "$0")/.."

V="${1:-}"
if [ -z "$V" ]; then
  V=$(grep -oE 'CAST_VERSION[[:space:]]+"[0-9.]+"' src/castalia.h |
      grep -oE '[0-9]+\.[0-9]+')
fi
[ -n "$V" ] || { echo "relnotes: no version given and none in src/castalia.h" >&2; exit 1; }

# From the "NEW IN <V>" heading up to (but not including) the next one.
notes=$(awk -v v="$V" '
  $0 ~ "^NEW IN " v "( |$|,|-)" { on = 1; print; next }
  on && /^NEW IN /                { exit }
  on                              { print }
' README.TXT)

if [ -z "$notes" ]; then
  echo "relnotes: README.TXT has no 'NEW IN $V' section" >&2
  exit 1
fi

printf '%s\n' "$notes"
