#!/usr/bin/env python3
# ======================================================================
# tools/pngdiff.py A.png B.png - percent of pixels that differ
# ----------------------------------------------------------------------
# Prints one integer: the percentage of pixels that are not identical
# between the two images, or 0 if either cannot be read or they are
# different sizes.
#
# tools/regress.sh uses this for the one assertion it cannot make against
# the disk.  A dialog sized with a negative height DRAWS NOTHING but
# still runs its modal loop - invisible and alive - so a keyboard-driven
# save through it writes exactly the same file as a visible one.  What
# separates the two is whether the screen changed, and "how much of it
# changed" is position- and palette-independent, so the check survives a
# dialog moving or a theme swap.
#
# Decodes through ffmpeg rather than an image library: this repository's
# tooling already requires ffmpeg, and PIL is not guaranteed to be there.
# ======================================================================
import subprocess
import sys


def raw(path):
    r = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path,
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True)
    return r.stdout


def main():
    if len(sys.argv) != 3:
        print(0)
        return
    a = raw(sys.argv[1])
    b = raw(sys.argv[2])
    if not a or not b or len(a) != len(b):
        print(0)
        return
    total = len(a) // 3
    if total == 0:
        print(0)
        return
    diff = 0
    for i in range(0, total * 3, 3):
        if a[i] != b[i] or a[i + 1] != b[i + 1] or a[i + 2] != b[i + 2]:
            diff += 1
    print(diff * 100 // total)


main()
