#!/usr/bin/env bash
# ======================================================================
# ci/nearfar.sh - the medium-model near/far trap
# ----------------------------------------------------------------------
# In the MEDIUM model data pointers are NEAR.  Passing a `far` object to
# a function whose parameter is a plain `T *` silently truncates the
# segment: the callee then reads or writes DGROUP at that offset instead.
# Open Watcom does not warn, even at -wx.
#
# This has bitten the project four times now - blacking out the Light
# Show (a far ramp handed to `u8 *`), corrupting CONST with filenames (a
# far table handed to `FileEntry *`), deleting the wrong file (e->name
# through a far pointer into remove()), and writing window geometry into
# the string pool (&g_last_geom[k] into a near Rect *).
#
# The first three versions of this gate lived here as greps and reported
# OK while the last two bugs were live in the tree.  The real work is now
# in ci/nearfar.py, which scans every argument position, resolves callees
# across translation units and against libc, understands far POINTERS and
# not just far arrays, and checks assignments as well as calls.  This
# wrapper only picks an interpreter.
# ======================================================================
set -u
cd "$(dirname "$0")/.."

for py in python3 python; do
  if command -v "$py" >/dev/null 2>&1; then
    exec "$py" ci/nearfar.py
  fi
done

echo "nearfar: no python found - CANNOT CHECK"
echo "         (this gate guards silent memory corruption; do not"
echo "          treat a missing interpreter as a pass)"
exit 1
