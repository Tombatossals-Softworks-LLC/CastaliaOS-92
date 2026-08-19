## What this changes

<!-- One paragraph. What is different for someone using Castalia? -->

## Why

<!-- The bug, the missing behaviour, or the itch. -->

## Checked before opening

- [ ] `wmake` builds clean — the tree is warnings-as-errors (`-wx -we`)
- [ ] A new `src/*.c` is wired into **all three** build definitions:
      `Makefile`, `BUILD.BAT` and `castalia.lnk` (`bash ci/consistency.sh`)
- [ ] C89 house style holds: `bash ci/lint.sh`
- [ ] Parser changes come with a case in `tests/test_parsers.c`
      (`make -C tests` runs them under ASan + UBSan)
- [ ] User-visible changes are described in README.TXT's release history
- [ ] Tried it in DOSBox in **both** video modes if it touches drawing
      (320x200x256 and 640x480x16)

## Screenshots

<!-- For anything visual. tools/shot.sh captures the real thing. -->
