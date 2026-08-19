#!/usr/bin/env python3
# ======================================================================
# tools/mkfloppy.py OUT.IMG [KB] [FREE] - a FAT12 floppy image
# ----------------------------------------------------------------------
# A DOS shell spends its life writing to floppies, and the interesting
# thing a floppy does is FILL UP - which is exactly the case none of the
# tests could reach.  DOSBox's "mount -freesize" only changes what INT
# 21h AH=36h REPORTS; a directory mount passes writes through to the
# host, so the disk never actually runs out.  An image does, so a copy
# of something bigger than the image is a genuine short write.
#
# Default 160 KB: the original single-sided PC format, which DOSBox
# accepts from "imgmount a <img> -t floppy" and which is small enough
# that almost any real file overruns it.
#
# FREE, if given, is roughly how many bytes to leave: the rest is taken
# up by a FILLER.DAT with a real FAT chain behind it.  Shrinking the
# image instead does not work past a point - DOSBox reads the geometry
# back out of the BPB and stops recognising sizes no real floppy had -
# and a nearly-full disk is the honest shape of the problem anyway.  A
# save only fails if the disk has less room than the document, so
# "160 1000" - a kilobyte free - is how you make a small one fail.
#
# Deliberately hand-rolled: mkfs.fat and mtools are not present in this
# container, and the format is a BPB, two one-sector FATs and a root
# directory - less code than arguing about the dependency.
# ======================================================================
import struct
import sys


def put12(fat, n, val):
    """Write FAT12 entry n - two entries share every three bytes."""
    i = n * 3 // 2
    if n % 2 == 0:
        fat[i] = val & 0xFF
        fat[i + 1] = (fat[i + 1] & 0xF0) | ((val >> 8) & 0x0F)
    else:
        fat[i] = (fat[i] & 0x0F) | ((val << 4) & 0xF0)
        fat[i + 1] = (val >> 4) & 0xFF


def build(total_kb, free_bytes=None):
    bps = 512                              # bytes per sector
    total = total_kb * 1024 // bps         # 320 sectors at 160 KB
    spc, reserved, nfats, roots, spf = 1, 1, 2, 64, 1

    boot = bytearray(bps)
    boot[0:3] = b'\xEB\x3C\x90'
    boot[3:11] = b'MSDOS5.0'
    struct.pack_into('<H', boot, 11, bps)
    boot[13] = spc
    struct.pack_into('<H', boot, 14, reserved)
    boot[16] = nfats
    struct.pack_into('<H', boot, 17, roots)
    struct.pack_into('<H', boot, 19, total)
    boot[21] = 0xFE                        # media descriptor, 160 KB
    struct.pack_into('<H', boot, 22, spf)
    struct.pack_into('<H', boot, 24, 8)    # sectors per track
    struct.pack_into('<H', boot, 26, 1)    # heads
    boot[510:512] = b'\x55\xAA'

    fat = bytearray(bps * spf)
    fat[0:3] = b'\xFE\xFF\xFF'             # media byte + end of chain

    root_bytes = roots * 32
    root = bytearray(root_bytes)
    root_sectors = root_bytes // bps
    data_sectors = total - reserved - spf * nfats - root_sectors
    data = bytearray(data_sectors * bps)

    if free_bytes is not None:
        want = data_sectors * bps - free_bytes
        n = max(0, min(data_sectors, (want + bps - 1) // bps))
        if n:
            # A real chain, not just a size: DOS believes the FAT, so a
            # directory entry with no clusters behind it leaves the disk
            # as empty as it started and the test proves nothing.
            for k in range(n):
                nxt = 0xFFF if k == n - 1 else (k + 3)
                put12(fat, k + 2, nxt)
            for k in range(n * bps):
                data[k] = 0xDB
            root[0:11] = b'FILLER  DAT'
            root[11] = 0x20                        # archive
            struct.pack_into('<H', root, 26, 2)    # first cluster
            struct.pack_into('<I', root, 28, n * bps)

    return bytes(boot) + bytes(fat) * nfats + bytes(root) + bytes(data)


def main():
    if len(sys.argv) < 2:
        print("usage: mkfloppy.py OUT.IMG [KB] [FREE_BYTES]")
        return 1
    kb = int(sys.argv[2]) if len(sys.argv) > 2 else 160
    free = int(sys.argv[3]) if len(sys.argv) > 3 else None
    open(sys.argv[1], 'wb').write(build(kb, free))
    return 0


if __name__ == '__main__':
    sys.exit(main())
