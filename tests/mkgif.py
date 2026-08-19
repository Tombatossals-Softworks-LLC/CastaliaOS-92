#!/usr/bin/env python3
"""Generate the GIF fixtures for tests/test_parsers.c.

A hostile GIF is only interesting if the decoder gets far enough to be
hurt by it, so these are built with a real LZW encoder and then damaged
in specific ways, rather than assembled from plausible-looking bytes.
"""
import sys

def lzw_encode(pixels, min_code_size):
    clear, end = 1 << min_code_size, (1 << min_code_size) + 1
    size = min_code_size + 1
    table = {(i,): i for i in range(1 << min_code_size)}
    nxt = end + 1
    out, cur, nbits = bytearray(), 0, 0
    def emit(code):
        nonlocal cur, nbits
        cur |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(cur & 0xFF); cur >>= 8; nbits -= 8
    emit(clear)
    w = ()
    for p in pixels:
        wp = w + (p,)
        if wp in table:
            w = wp
        else:
            emit(table[w])
            table[wp] = nxt; nxt += 1
            if nxt > (1 << size) and size < 12:
                size += 1
            w = (p,)
    if w: emit(table[w])
    emit(end)
    if nbits: out.append(cur & 0xFF)
    return bytes(out)

def gif(w, h, pixels, ncol=4, mcs=2):
    b = bytearray(b"GIF89a")
    b += bytes([w & 255, w >> 8, h & 255, h >> 8])
    bits = max(1, (ncol - 1).bit_length())
    b += bytes([0x80 | (bits - 1), 0, 0])
    for i in range(1 << bits):
        b += bytes([(i * 60) & 255, (i * 30) & 255, (i * 90) & 255])
    b += b","
    b += bytes([0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, 0])
    b += bytes([mcs])
    data = lzw_encode(pixels, mcs)
    for i in range(0, len(data), 255):
        chunk = data[i:i+255]
        b += bytes([len(chunk)]) + chunk
    b += b"\x00;"
    return bytes(b)

def gif_maxchain():
    """A well-formed stream that grows ONE dictionary chain to its maximum.

    After the clear it emits root 0, then repeatedly emits the code the
    decoder is about to define (the KwKwK case), so entry N chains to
    N-1 all the way to 4095.  Decoding the last code walks ~4090 links
    and pushes one byte per link - the deepest the LZW output stack can
    legitimately be driven, and therefore the real test of its bound.
    """
    mcs = 2
    clear, end = 1 << mcs, (1 << mcs) + 1
    size = mcs + 1
    out, cur, nbits = bytearray(), 0, 0
    def emit(code):
        nonlocal cur, nbits
        cur |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(cur & 0xFF); cur >>= 8; nbits -= 8
    emit(clear)
    emit(0)
    nxt = end + 1
    while nxt < 4096:
        emit(nxt)              # == nxt: defines it, extends the chain by 1
        nxt += 1
        if nxt == (1 << size) and size < 12:
            size += 1
    for _ in range(4):         # re-walk the maximal chain a few times
        emit(4095)
    emit(end)
    if nbits: out.append(cur & 0xFF)
    data = bytes(out)

    w = h = 16
    b = bytearray(b"GIF89a")
    b += bytes([w & 255, w >> 8, h & 255, h >> 8])
    b += bytes([0x80 | 1, 0, 0])
    for i in range(4):
        b += bytes([(i * 60) & 255, (i * 30) & 255, (i * 90) & 255])
    b += b","
    b += bytes([0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, 0])
    b += bytes([mcs])
    for i in range(0, len(data), 255):
        chunk = data[i:i+255]
        b += bytes([len(chunk)]) + chunk
    b += b"\x00;"
    return bytes(b)

def gif_wildcodes():
    """clear, a legal root, then codes far beyond the defined dictionary.

    The decoder must reject a code above `nxt`; if it ever stops doing so
    it will chase g_prefix[] entries that were never written, and on DOS
    those hold whatever the previous owner of the block left behind.  The
    host shim poisons freshly allocated blocks for exactly this reason, so
    the chase would run straight off the tables and ASan would catch it.
    """
    mcs = 2
    clear, end = 1 << mcs, (1 << mcs) + 1
    size = mcs + 1
    out, cur, nbits = bytearray(), 0, 0
    def emit(code):
        nonlocal cur, nbits
        cur |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(cur & 0xFF); cur >>= 8; nbits -= 8
    emit(clear)
    emit(0)                      # a legal root, so decoding really starts
    size = 12                    # widen so big codes are expressible
    for _ in range(64):
        emit(4000)               # far beyond nxt (which is still 6)
    emit(end)
    if nbits: out.append(cur & 0xFF)
    data = bytes(out)

    w = h = 16
    b = bytearray(b"GIF89a")
    b += bytes([w & 255, w >> 8, h & 255, h >> 8])
    b += bytes([0x80 | 1, 0, 0])
    for i in range(4):
        b += bytes([(i * 60) & 255, (i * 30) & 255, (i * 90) & 255])
    b += b","
    b += bytes([0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, 0])
    b += bytes([mcs])
    for i in range(0, len(data), 255):
        chunk = data[i:i+255]
        b += bytes([len(chunk)]) + chunk
    b += b"\x00;"
    return bytes(b)

def cfile(name, data):
    out = ["static const unsigned char %s[] = {" % name]
    for i in range(0, len(data), 12):
        out.append("    " + ", ".join("0x%02X" % c for c in data[i:i+12]) + ",")
    out.append("};")
    return "\n".join(out)

W = H = 16
good = gif(W, H, [(x ^ y) & 3 for y in range(H) for x in range(W)])

# Same file with every LZW data byte set to 0xFF: the code stream then
# indexes dictionary entries that were never defined, which is what sends
# the prefix chase off the end of the stack.
i = good.index(b",")
img_hdr_end = i + 11          # separator + 8 geometry bytes + packed + mcs
chaos = bytearray(good)
j = img_hdr_end
while j < len(chaos):
    n = chaos[j]
    if n == 0 or j + 1 + n > len(chaos):
        break
    for k in range(j + 1, j + 1 + n):
        chaos[k] = 0xFF
    j += n + 1
chaos = bytes(chaos)

# A file that claims 4096x4096 but carries a 16x16 payload.
huge = bytearray(good)
huge[6:10] = bytes([0x00, 0x10, 0x00, 0x10])
huge = bytes(huge)

# Truncated mid-LZW: the decoder must notice the stream ended.
trunc = good[:len(good) - 6]

with open(sys.argv[1], "w") as f:
    f.write("/* Generated by tests/mkgif.py - do not edit by hand. */\n")
    f.write("/* A valid 16x16 GIF, and three damaged versions of it. */\n\n")
    f.write(cfile("GIF_GOOD", good) + "\n\n")
    f.write(cfile("GIF_CHAOS", chaos) + "\n\n")
    f.write(cfile("GIF_HUGE_DIMS", huge) + "\n\n")
    f.write(cfile("GIF_TRUNC", trunc) + "\n\n")
    f.write(cfile("GIF_MAXCHAIN", gif_maxchain()) + "\n\n")
    f.write(cfile("GIF_WILDCODES", gif_wildcodes()) + "\n")
