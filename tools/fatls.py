#!/usr/bin/env python3
# ======================================================================
# tools/fatls.py IMG - list the root directory of a FAT12 floppy image
# ----------------------------------------------------------------------
# The other half of tools/mkfloppy.py.  A test that copies onto a full
# floppy has to be able to ask what ended up there, and the image is the
# only record - "NAME.EXT size" per line, nothing for an empty disk.
#
# Reads the BPB rather than assuming the 160 KB geometry, so it still
# answers if mkfloppy.py is asked for a different size.
# ======================================================================
import struct
import sys


def main():
    if len(sys.argv) != 2:
        return 1
    try:
        img = open(sys.argv[1], 'rb').read()
    except IOError:
        return 1
    if len(img) < 512:
        return 1
    bps   = struct.unpack_from('<H', img, 11)[0]
    res   = struct.unpack_from('<H', img, 14)[0]
    nfat  = img[16]
    roots = struct.unpack_from('<H', img, 17)[0]
    spf   = struct.unpack_from('<H', img, 22)[0]
    if bps == 0 or roots == 0:
        return 1
    off = (res + nfat * spf) * bps
    for i in range(roots):
        e = img[off + i * 32: off + i * 32 + 32]
        if len(e) < 32 or e[0] == 0:
            break
        if e[0] == 0xE5 or (e[11] & 0x08):     # deleted, or volume label
            continue
        base = e[0:8].decode('ascii', 'replace').rstrip()
        ext  = e[8:11].decode('ascii', 'replace').rstrip()
        size = struct.unpack_from('<I', e, 28)[0]
        print("%s%s %d" % (base, ('.' + ext) if ext else '', size))
    return 0


sys.exit(main())
