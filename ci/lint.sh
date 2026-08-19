#!/usr/bin/env bash
# ======================================================================
# ci/lint.sh - CASTALIA/386 source hygiene (C89 house style)
# ----------------------------------------------------------------------
# Enforces the coding rules stated in src/castalia.h: C89/C90 only, no
# C++ line comments, spaces not tabs, LF endings, plain 7-bit ASCII, and
# a trailing newline.  Scans src/*.c and src/*.h only.  No toolchain is
# required, so it runs anywhere bash and GNU grep are present.
#
# Usage:  bash ci/lint.sh          (exit 0 = clean, 1 = problems found)
# ======================================================================
set -u
cd "$(dirname "$0")/.."

fail=0
flag() { printf '  ! %s\n' "$1"; fail=1; }
SRC=(src/*.c src/*.h install/*.c)

echo "==> C89 forbids C++ line comments ( // ... )"
hits=$(grep -nE '//' "${SRC[@]}" | grep -vE '://' || true)
if [ -n "$hits" ]; then echo "$hits"; flag "C++ line comments found"; fi

echo "==> hard tabs (the sources are space-indented)"
hits=$(grep -lP '\t' "${SRC[@]}" || true)
if [ -n "$hits" ]; then echo "$hits"; flag "tab characters found"; fi

echo "==> trailing whitespace"
hits=$(grep -nP '[ \t]+$' "${SRC[@]}" || true)
if [ -n "$hits" ]; then echo "$hits" | head -30; flag "trailing whitespace"; fi

echo "==> CRLF line endings (LF only)"
hits=$(grep -lP '\r$' "${SRC[@]}" || true)
if [ -n "$hits" ]; then echo "$hits"; flag "CRLF line endings"; fi

echo "==> non-ASCII bytes (the sources are 7-bit ASCII)"
hits=$(grep -lP '[^\x00-\x7F]' "${SRC[@]}" || true)
if [ -n "$hits" ]; then echo "$hits"; flag "non-ASCII bytes"; fi

echo "==> file ends with a newline"
for f in "${SRC[@]}"; do
  if [ -s "$f" ] && [ -n "$(tail -c1 "$f")" ]; then flag "no final newline: $f"; fi
done

if [ "$fail" -eq 0 ]; then echo "lint: OK"; else echo "lint: FAILED"; fi
exit "$fail"
