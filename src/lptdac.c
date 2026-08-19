/* ======================================================================
 * lptdac.c - Parallel-port DACs for CASTALIA/386: Covox & Sound Source
 * ====================================================================== */
#include <string.h>
#include <conio.h>     /* inp / outp / kbhit / getch                      */
#include <dos.h>       /* MK_FP                                           */
#include "lptdac.h"

#define MODE_OFF   0
#define MODE_COVOX 1
#define MODE_DSS   2

#define DSS_RATE   7000U     /* the Sound Source FIFO drains at ~7 kHz    */

static int      g_mode = MODE_OFF;
static unsigned g_base = 0;          /* LPT data port; +1 status, +2 ctrl */
static char     g_name[20] = "";

/* LPT base ports live in the BIOS data area at 0040:0008 (LPT1..LPT3).
   A zero entry means DOS never found that port. */
static unsigned bios_lpt_base(int lpt)
{
    unsigned far *bda = (unsigned far *)MK_FP(0x40, 8);
    if (lpt < 1 || lpt > 3)
        return 0;
    return bda[lpt - 1];
}

void lptdac_config(const char *mode, int lpt)
{
    g_mode = MODE_OFF;
    g_base = 0;
    g_name[0] = '\0';
    if (mode == NULL)
        return;
    if (lpt < 1 || lpt > 3)
        lpt = 1;
    if      (strcmp(mode, "covox") == 0) g_mode = MODE_COVOX;
    else if (strcmp(mode, "dss")   == 0) g_mode = MODE_DSS;
    else
        return;
    g_base = bios_lpt_base(lpt);
    if (g_base == 0) {                 /* no such port: stay silent, safe   */
        g_mode = MODE_OFF;
        return;
    }
    strcpy(g_name, (g_mode == MODE_COVOX) ? "Covox LPT " : "Snd Source LPT ");
    {
        int n = (int)strlen(g_name);
        g_name[n - 1] = (char)('0' + lpt);   /* the space becomes the digit */
        g_name[n] = '\0';
    }
}

bool_t lptdac_present(void)  { return (g_mode != MODE_OFF) ? TRUE : FALSE; }
unsigned lptdac_max_rate(void) { return (g_mode == MODE_DSS) ? DSS_RATE : 0; }
const char *lptdac_name(void)  { return g_name; }

/* ---- PIT pacing (same trick as the PC-speaker burst in media.c):
   channel 0 free-runs at 1.19318 MHz, so counting its downticks gives a
   CPU-speed-independent sample clock. */
static unsigned pit_count(void)
{
    unsigned lo, hi;
    outp(0x43, 0x00);                  /* latch channel 0                  */
    lo = inp(0x40);
    hi = inp(0x40);
    return (hi << 8) | lo;
}
static void pit_wait(unsigned period)
{
    unsigned start = pit_count(), now, el;
    do {
        now = pit_count();
        el  = (unsigned)((start - now) & 0xFFFF);     /* ch0 counts down   */
    } while (el < period);
}

bool_t lptdac_play(const u8 far *s, int n, unsigned rate)
{
    int i;
    if (g_mode == MODE_OFF || g_base == 0 || n < 1)
        return FALSE;

    if (g_mode == MODE_COVOX) {
        /* Free-running ladder DAC: one byte per sample period.  The port
           keeps the last byte's level, so finish on mid-scale silence. */
        unsigned period = (unsigned)(1193180UL / (rate ? rate : DSS_RATE));
        for (i = 0; i < n; ++i) {
            outp(g_base, s[i]);
            pit_wait(period);
            if (kbhit()) { getch(); break; }
        }
        outp(g_base, 128);
    } else {
        /* Sound Source: wait for room in the 16-byte FIFO (status bit 6
           high = full), present the byte, clock it in by pulsing control
           bit 3.  The FIFO drains at ~7 kHz and paces the whole stream;
           the guard keeps a missing/unplugged box from hanging the loop. */
        unsigned stat = g_base + 1, ctrl = g_base + 2;
        outp(ctrl, 0x04);              /* select the box, clock line idle   */
        for (i = 0; i < n; ++i) {
            unsigned guard = 8000;
            while ((inp(stat) & 0x40) && --guard != 0)
                ;
            outp(g_base, s[i]);
            outp(ctrl, 0x0C);          /* clock high...                     */
            outp(ctrl, 0x04);          /* ...and low: byte enters the FIFO  */
            if ((i & 63) == 0 && kbhit()) { getch(); break; }
        }
    }
    return TRUE;
}
