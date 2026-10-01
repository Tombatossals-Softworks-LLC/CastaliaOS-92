#!/usr/bin/env bash
# ======================================================================
# ci/package.sh [outdir] - build the distribution archive from release/
# ----------------------------------------------------------------------
# What a person downloads from the Releases page.  release/ holds the
# bundle; this is that bundle as one .zip, named for the version, laid
# out so unzipping it on a FAT drive gives ONE directory with
# CASTALIA.EXE and INSTALL.EXE at the top - not a release/ folder to
# descend into first.
#
# It lives here, and not inside the release workflow, for the same reason
# the staging script does: a step that only ever runs on a tag is a step
# first tested on the day it matters.  This repository has no tags yet,
# so until now every line of the release path was untried.  CI runs this
# on every change and throws the archive away; the tag build runs the
# same script and keeps it.
#
# The archive is built and read back with python3's zipfile rather than
# zip/unzip.  Not for elegance: the tools/ scripts already require
# python3, while zip is not on a minimal container - and a packaging
# step you cannot run on the machine you are writing it on is how the
# path stayed untested in the first place.
#
# Verifies what it produced: the archive must carry the executables, the
# INI, both installers, the docs and the assets, and the CASTALIA.EXE
# inside it must still be an MZ image.  "zip exited 0" is not evidence.
#
#   bash ci/package.sh            -> ./castalia-<version>.zip
#   bash ci/package.sh /tmp/out   -> /tmp/out/castalia-<version>.zip
# ======================================================================
set -eu
cd "$(dirname "$0")/.."

OUTDIR="${1:-$(pwd)}"
mkdir -p "$OUTDIR"

V=$(grep -oE 'CAST_VERSION[[:space:]]+"[0-9.]+"' src/castalia.h |
    grep -oE '[0-9]+\.[0-9]+')
[ -n "$V" ] || { echo "package: could not read CAST_VERSION from src/castalia.h"; exit 1; }

python3 - "$V" "$OUTDIR" <<'PYEOF'
import os
import sys
import zipfile

version, outdir = sys.argv[1], sys.argv[2]
stage = "castalia-" + version
path = os.path.join(outdir, stage + ".zip")

# Every file under release/, stored as <stage>/<relative path>.
entries = []
for root, dirs, files in os.walk("release"):
    dirs.sort()
    for name in sorted(files):
        full = os.path.join(root, name)
        arc = os.path.join(stage, os.path.relpath(full, "release"))
        entries.append((full, arc))

if not entries:
    print("package: release/ is empty")
    sys.exit(1)

with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for full, arc in entries:
        z.write(full, arc)

print("==> %s.zip" % stage)
with zipfile.ZipFile(path) as z:
    names = z.namelist()
    for info in sorted(z.infolist(), key=lambda i: i.filename):
        print("    %9d  %s" % (info.file_size, info.filename))

    print("==> the archive carries the whole bundle")
    fail = False
    required = [
        "CASTALIA.EXE", "INSTALL.EXE", "CASTALIA.INI",
        "INSTALL.BAT", "CASTSHEL.BAT", "README.TXT", "RELEASE.TXT",
        "ASSETS/ICONS/CASTLE.GIF", "ASSETS/MEDIA/ODE.MID",
    ]
    for f in required:
        if stage + "/" + f not in names:
            print("  ! %s is not in the archive" % f)
            fail = True

    # An MZ header read back OUT of the zip catches a bundle staged from
    # a truncated or half-written build.
    exe = stage + "/CASTALIA.EXE"
    if exe in names and z.read(exe)[:2] != b"MZ":
        print("  ! the CASTALIA.EXE inside the archive is not a DOS executable")
        fail = True

if fail:
    print("package: FAILED")
    sys.exit(1)
print("package: OK  (%d bytes, %d files)" % (os.path.getsize(path), len(entries)))
PYEOF
