# Host tests

The shell is 16-bit DOS real-mode code and cannot be run under a
sanitizer.  Its three **file-format parsers**, though, are plain C that
takes fully hostile input — a GIF or `.ICO` named in `CASTALIA.INI` or
picked in the Sketch Pad's Load box, an INI written by hand — and that is
where every memory-safety bug found in this project has been.

`tests/host.h` erases the DOS-isms (`far`, the near/far pointer split) and
backs `_dos_allocmem` with `malloc`, so **the parser sources compile
unmodified** and ASan sees a real heap block of exactly the size the code
asked DOS for.  A one-byte overrun is then a reported heap-buffer-overflow
instead of a silent write into the next MCB.

    make -C tests          # build and run
    make -C tests clean

## What the corpus covers

GIF fixtures are produced by `tests/mkgif.py`, which contains a real LZW
*encoder* — a hostile file only proves something if the decoder gets far
enough to be hurt by it, so these are valid streams that are then damaged
in specific ways, not plausible-looking bytes.  `GIF_MAXCHAIN` grows one
dictionary chain to entry 4095 and walks it, which is the deepest the LZW
output stack can legitimately be driven.

Freshly allocated blocks are **poisoned with 0xFF** rather than left as
malloc's zeroed pages: DOS hands back whatever the last owner left there,
and a decoder that trusts an unwritten dictionary entry behaves very
differently on dirty memory.

## Two honest limits

* **16-bit overflow does not reproduce here.**  The host `int` is 32 bits,
  so the wrap-around class of bug (`w * bpp` overflowing, a length
  truncating from 32 to 16 bits) simply cannot happen in this build.  These
  tests assert the *behaviour* the guards produce; they cannot prove the
  guards are still needed.  That class is caught by review and by the
  `-wx -we` Watcom build, not here.

* **Some guards are defence-in-depth and no fixture can trip them.**  The
  LZW `sp < 4095` bound, for instance, is unreachable while the `cur > nxt`
  code validation above it stands.  Removing either one alone leaves the
  suite green.  Removing icon.c's `rowbytes` bound, by contrast, is caught
  immediately — that one is load-bearing.
