/* ======================================================================
 * sblaster.c - Sound Blaster digital audio for CASTALIA/386
 * ----------------------------------------------------------------------
 * See sblaster.h.  Detection reads the BLASTER environment variable (and
 * falls back to probing the usual base ports); playback is one single-cycle
 * 8-bit DMA transfer with the card's IRQ masked at the PIC, so there is no
 * interrupt handler and nothing hardware-stateful survives the call.
 * ====================================================================== */
#include <conio.h>     /* inp / outp / kbhit / getch                       */
#include <dos.h>       /* _dos_allocmem / _dos_freemem / MK_FP             */
#include <string.h>    /* _fmemcpy                                         */
#include <stdlib.h>    /* getenv                                           */
#include "sblaster.h"
#include "system.h"    /* sys_ticks / sys_idle                             */

#define DMABUF_BYTES 16384U

/* ---- detected resources --------------------------------------------- */
static int  g_state = -1;      /* -1 unknown, 0 none, 1 present            */
static int  g_base  = 0x220;
static int  g_irq   = 7;
static int  g_dma   = 1;
static int  g_major = 0;
static char g_name[24] = "";

/* ---- DMA-safe buffer (never crosses a 64 KB page) ------------------- */
static unsigned            g_dmaseg = 0;
static unsigned char far  *g_dmabuf = (unsigned char far *)0;
static unsigned long       g_dmaphys = 0;

/* 8-bit DMA page-register ports, indexed by channel 0..3. */
static const unsigned g_pageport[4] = { 0x87, 0x83, 0x81, 0x82 };

/* A short I/O delay (a few hundred ns per read of the dummy port 0x80). */
static void io_delay(void)
{
    int i;
    for (i = 0; i < 8; ++i) (void)inp(0x80);
}

/* ---- DSP primitives -------------------------------------------------- */
static void dsp_write(int base, unsigned char v)
{
    int t;
    for (t = 0; t < 30000; ++t)
        if (!(inp(base + 0x0C) & 0x80)) break;   /* write buffer ready     */
    outp(base + 0x0C, v);
}
static int dsp_read(int base)
{
    int t;
    for (t = 0; t < 30000; ++t)
        if (inp(base + 0x0E) & 0x80) break;      /* data available         */
    return inp(base + 0x0A);
}
static bool_t dsp_reset(int base)
{
    int t;
    outp(base + 0x06, 1);
    io_delay();                                  /* hold reset >= 3 us      */
    outp(base + 0x06, 0);
    for (t = 0; t < 200; ++t) {
        if (inp(base + 0x0E) & 0x80)
            if (inp(base + 0x0A) == 0xAA) return TRUE;
        io_delay();
    }
    return FALSE;
}

/* ---- BLASTER string parsing ----------------------------------------- */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int parse_hex(const char *s)
{
    int v = 0, d;
    while ((d = hexval(*s)) >= 0) { v = v * 16 + d; ++s; }
    return v;
}
static int parse_dec(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return v;
}
static void name_from_major(int major)
{
    const char *n = "Sound Blaster";
    if      (major >= 4) n = "Sound Blaster 16";
    else if (major == 3) n = "Sound Blaster Pro";
    else if (major == 2) n = "Sound Blaster 2.0";
    {   int i = 0; while (n[i] && i < (int)sizeof(g_name) - 1) { g_name[i] = n[i]; ++i; } g_name[i] = '\0'; }
}

static bool_t detect(void)
{
    const char *b = getenv("BLASTER");
    int found_base = 0;

    if (b != (const char *)0) {
        int i;
        for (i = 0; b[i]; ++i) {
            char c = b[i];
            if ((c == 'A' || c == 'a') && hexval(b[i + 1]) >= 0) g_base = parse_hex(b + i + 1);
            else if ((c == 'I' || c == 'i') && b[i+1] >= '0' && b[i+1] <= '9') g_irq = parse_dec(b + i + 1);
            else if ((c == 'D' || c == 'd') && b[i+1] >= '0' && b[i+1] <= '9') g_dma = parse_dec(b + i + 1);
        }
        if (dsp_reset(g_base)) found_base = 1;
    }
    if (!found_base) {                           /* probe the usual ports   */
        static const int cand[] = { 0x220, 0x240, 0x260, 0x210, 0x230, 0x250, 0x280 };
        int k;
        for (k = 0; k < (int)(sizeof(cand) / sizeof(cand[0])); ++k) {
            if (dsp_reset(cand[k])) { g_base = cand[k]; found_base = 1; break; }
        }
        if (found_base) { g_irq = 7; g_dma = 1; }
    }
    if (!found_base) return FALSE;

    dsp_write(g_base, 0xE1);                      /* DSP version             */
    g_major = dsp_read(g_base);
    (void)dsp_read(g_base);                       /* minor (unused)          */
    if (g_dma < 0 || g_dma > 3) g_dma = 1;        /* need an 8-bit channel   */
    name_from_major(g_major);
    return TRUE;
}

bool_t sb_present(void)
{
    if (g_state < 0)
        g_state = detect() ? 1 : 0;
    return g_state ? TRUE : FALSE;
}
const char *sb_name(void) { return sb_present() ? g_name : ""; }
int sb_base(void) { return g_base; }
int sb_irq(void)  { return g_irq; }
int sb_dma(void)  { return g_dma; }

/* ---- DMA buffer allocation (aligned to dodge a 64 KB crossing) ------- */
static bool_t dma_alloc(void)
{
    unsigned seg, dma_seg, dma_off, off_in_page;
    unsigned long linear, skip;
    if (g_dmabuf != (unsigned char far *)0) return TRUE;
    if (_dos_allocmem((unsigned)((2UL * DMABUF_BYTES + 15UL) / 16UL), &seg) != 0)
        return FALSE;
    g_dmaseg = seg;
    linear = (unsigned long)seg << 4;
    off_in_page = (unsigned)(linear & 0xFFFFUL);
    if ((unsigned long)off_in_page + DMABUF_BYTES > 0x10000UL)
        skip = 0x10000UL - off_in_page;
    else
        skip = 0;
    dma_seg = (unsigned)(seg + (unsigned)(skip >> 4));
    dma_off = (unsigned)(skip & 0x0F);
    g_dmabuf = (unsigned char far *)MK_FP(dma_seg, dma_off);
    g_dmaphys = ((unsigned long)dma_seg << 4) + dma_off;
    return TRUE;
}

void sb_release(void)
{
    if (g_dmaseg != 0) { _dos_freemem(g_dmaseg); g_dmaseg = 0; }
    g_dmabuf = (unsigned char far *)0;
}

bool_t sb_play_8bit(const unsigned char far *samples, unsigned nsamp, unsigned rate)
{
    unsigned ch, addrp, cntp, pagep, off16, page, tc;
    unsigned char picport, oldmask;
    unsigned long dur, start;

    if (!sb_present() || nsamp < 1 || rate < 1) return FALSE;
    ch = (unsigned)sb_dma();
    if (ch > 3) return FALSE;
    if (nsamp > DMABUF_BYTES) nsamp = DMABUF_BYTES;
    if (!dma_alloc()) return FALSE;

    _fmemcpy(g_dmabuf, samples, nsamp);

    addrp = ch << 1;
    cntp  = (ch << 1) + 1;
    pagep = g_pageport[ch];
    off16 = (unsigned)(g_dmaphys & 0xFFFFUL);
    page  = (unsigned)(g_dmaphys >> 16);

    /* Mask the card's IRQ so its end-of-block interrupt needs no handler. */
    picport = (g_irq < 8) ? 0x21 : 0xA1;
    oldmask = inp(picport);
    outp(picport, (unsigned char)(oldmask | (1 << (g_irq & 7))));

    /* Program the 8237 for a single-cycle memory->device transfer. */
    outp(0x0A, (unsigned char)(0x04 | ch));       /* mask channel            */
    outp(0x0C, 0x00);                             /* clear byte flip-flop    */
    outp(0x0B, (unsigned char)(0x48 | ch));       /* single, read (playback) */
    outp(addrp, (unsigned char)(off16 & 0xFF));
    outp(addrp, (unsigned char)(off16 >> 8));
    outp(cntp,  (unsigned char)((nsamp - 1) & 0xFF));
    outp(cntp,  (unsigned char)((nsamp - 1) >> 8));
    outp(pagep, (unsigned char)page);
    outp(0x0A, (unsigned char)ch);                /* unmask channel          */

    /* DSP: speaker on, sample rate (time constant), single-cycle output.
       Clamp the quotient: below ~3907 Hz it exceeds 255 and the byte-wide
       time constant would wrap to a wildly fast playback rate. */
    {
        unsigned long q = 1000000UL / rate;
        if (q > 250UL) q = 250UL;
        tc = 256 - (unsigned)q;
    }
    dsp_write(g_base, 0xD1);
    dsp_write(g_base, 0x40);
    dsp_write(g_base, (unsigned char)tc);
    dsp_write(g_base, 0x14);
    dsp_write(g_base, (unsigned char)((nsamp - 1) & 0xFF));
    dsp_write(g_base, (unsigned char)((nsamp - 1) >> 8));

    /* Wait it out, timed off the BIOS tick; any key stops it early. */
    dur = ((unsigned long)nsamp * 182UL) / (10UL * rate) + 2UL;
    start = sys_ticks();
    while (sys_ticks() - start < dur) {
        if (kbhit()) { getch(); dsp_write(g_base, 0xD0); break; }  /* halt   */
        sys_idle();
    }

    (void)inp(g_base + 0x0E);                     /* ack the 8-bit IRQ       */
    dsp_write(g_base, 0xD3);                      /* speaker off             */
    outp(picport, oldmask);                       /* restore the PIC mask    */
    return TRUE;
}
