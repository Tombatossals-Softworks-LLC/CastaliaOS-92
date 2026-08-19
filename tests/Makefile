# ======================================================================
# tests/Makefile - host-compiled sanitizer tests for the DOS parsers
# ----------------------------------------------------------------------
# The shell itself is 16-bit DOS and cannot be sanitized, but the three
# file-format parsers are plain C that takes hostile input, and every
# memory-safety bug found in this codebase so far has been in one of
# them.  They compile UNMODIFIED against tests/host.h, which erases the
# far/near distinction and backs _dos_allocmem with malloc, so ASan sees
# a real heap block of exactly the requested size.
#
#   make -C tests        build and run
#   make -C tests clean
# ======================================================================
CC      ?= gcc
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  := -std=c89 -Wall -Wextra -Wno-unused-parameter -g -O1 \
           -include host.h -I. -I../src $(SAN)
LDFLAGS := $(SAN)

SRC := test_parsers.c host.c ../src/gif.c ../src/icon.c ../src/config.c \
       ../src/textscan.c

all: run

hosttests: $(SRC) host.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

run: hosttests
	ASAN_OPTIONS=detect_leaks=1 ./hosttests

clean:
	rm -f hosttests cast_test_tmp.bin

.PHONY: all run clean
