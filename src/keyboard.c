/* ======================================================================
 * keyboard.c - Keyboard input for CASTALIA/386
 * ----------------------------------------------------------------------
 * Uses the Open Watcom conio routines kbhit()/getch(), which sit on top
 * of BIOS INT 16h.  kbhit() reports whether a key is waiting without
 * removing it; getch() removes and returns it.  An extended key arrives
 * as a 0 (or 0xE0) prefix byte followed by the scan code on a second
 * getch().
 * ====================================================================== */
#include <conio.h>
#include "keyboard.h"

int kb_poll(void)
{
    int c;

    if (!kbhit())
        return KEY_NONE;

    c = getch();
    if (c == 0 || c == 0xE0) {
        /* Extended key: the real scan code is the next byte. */
        c = getch();
        return 0x100 | c;
    }
    return c;
}

void kb_flush(void)
{
    while (kbhit())
        (void)getch();
}
