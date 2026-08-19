/* ======================================================================
 * gif.c - GIF image decoder for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "gif.h"

/* LZW work tables live in one far block (prefix 4096*2, suffix 4096,
   stack 4096 = 16 KB), grabbed from DOS for the decode and freed after. */
static unsigned lzw_seg = 0;
static u16 far *g_prefix;
static u8  far *g_suffix;
static u8  far *g_stack;

static bool_t lzw_alloc(void)
{
    if (_dos_allocmem(1024, &lzw_seg) != 0)   /* 16 KB = 1024 paragraphs   */
        return FALSE;
    g_prefix = (u16 far *)MK_FP(lzw_seg, 0);         /* 0..8191            */
    g_suffix = (u8 far *)MK_FP(lzw_seg, 8192);       /* 8192..12287        */
    g_stack  = (u8 far *)MK_FP(lzw_seg, 12288);      /* 12288..16383       */
    return TRUE;
}
static void lzw_free(void)
{
    if (lzw_seg) { _dos_freemem(lzw_seg); lzw_seg = 0; }
}

/* ---- sub-block bit reader -------------------------------------------- */
static FILE        *g_f;
static unsigned char g_sb[256];
static int           g_sblen, g_sbpos;
static unsigned long g_acc;
static int           g_bits;
static bool_t        g_eod;

static void br_init(FILE *f)
{
    g_f = f; g_sblen = 0; g_sbpos = 0; g_acc = 0; g_bits = 0; g_eod = FALSE;
}
/* Fetch the next data byte from the sub-block stream, refilling blocks. */
static int br_byte(void)
{
    if (g_sbpos >= g_sblen) {
        int len;
        if (g_eod) return -1;
        len = fgetc(g_f);
        if (len <= 0) { g_eod = TRUE; return -1; }    /* 0 = block terminator */
        if (fread(g_sb, 1, (unsigned)len, g_f) != (unsigned)len) {
            g_eod = TRUE; return -1;
        }
        g_sblen = len; g_sbpos = 0;
    }
    return g_sb[g_sbpos++];
}
/* Pull an LZW code of `size` bits (LSB first).  -1 at end of data. */
static int br_code(int size)
{
    int code;
    while (g_bits < size) {
        int b = br_byte();
        if (b < 0) return -1;
        g_acc |= ((unsigned long)b) << g_bits;
        g_bits += 8;
    }
    code = (int)(g_acc & (((unsigned long)1 << size) - 1));
    g_acc >>= size;
    g_bits -= size;
    return code;
}

/* ---- interlace-aware pixel writer ------------------------------------ */
static unsigned char far *o_buf;
static int o_w, o_h, o_maxw, o_maxh;
static int o_x, o_y, o_pass;
static bool_t o_interlaced;

static const int PASS_START[4] = { 0, 4, 2, 1 };
static const int PASS_STEP[4]  = { 8, 8, 4, 2 };

static void emit(u8 v)
{
    /* Rows are stored PACKED by the clamped image width: the consumers
       (wallpaper, Picture Show) read them back with buf + y*w, so using
       maxw as the stride would shear any image narrower than the screen. */
    int stride = (o_w < o_maxw) ? o_w : o_maxw;
    if (o_y < o_maxh && o_x < stride)
        o_buf[(long)o_y * stride + o_x] = v;
    if (++o_x >= o_w) {
        o_x = 0;
        if (o_interlaced) {
            o_y += PASS_STEP[o_pass];
            while (o_y >= o_h && o_pass < 3) {
                ++o_pass;
                o_y = PASS_START[o_pass];
            }
        } else {
            ++o_y;
        }
    }
}

static unsigned rd16f(FILE *f)
{
    int a = fgetc(f), b = fgetc(f);
    return (unsigned)(a & 0xFF) | ((unsigned)(b & 0xFF) << 8);
}

bool_t gif_decode(const char *path, unsigned char far *out,
                  int maxw, int maxh, int *w, int *h,
                  unsigned char *pal768, int *ncol)
{
    FILE *f;
    unsigned char hdr[13];
    int  gctflag, gctsize, i, iw, ih, ipacked, mcs;
    int  clear, endc, codesize, nxt, first, oldcode, fb;

    f = fopen(path, "rb");
    if (f == NULL)
        return FALSE;
    if (fread(hdr, 1, 13, f) != 13 ||
        hdr[0] != 'G' || hdr[1] != 'I' || hdr[2] != 'F') {
        fclose(f); return FALSE;
    }
    gctflag = hdr[10] & 0x80;
    gctsize = 2 << (hdr[10] & 7);
    *ncol   = 0;
    if (gctflag) {
        if (fread(pal768, 1, (unsigned)(gctsize * 3), f) !=
            (unsigned)(gctsize * 3)) { fclose(f); return FALSE; }
        *ncol = gctsize;
    }

    /* Walk blocks to the first image; skip extensions. */
    for (;;) {
        int c = fgetc(f);
        if (c < 0 || c == 0x3B) { fclose(f); return FALSE; }  /* trailer/eof */
        if (c == 0x21) {                                   /* extension     */
            fgetc(f);                                      /* label         */
            for (;;) {                                     /* skip subblocks */
                int len = fgetc(f);
                if (len <= 0) break;
                fseek(f, (long)len, SEEK_CUR);
            }
            continue;
        }
        if (c == 0x2C)                                     /* image         */
            break;
    }

    fgetc(f); fgetc(f);                    /* image left  (ignored)          */
    fgetc(f); fgetc(f);                    /* image top   (ignored)          */
    iw = (int)rd16f(f);
    ih = (int)rd16f(f);
    ipacked = fgetc(f);
    if (ipacked & 0x80) {                  /* local colour table overrides   */
        int lsize = 2 << (ipacked & 7);
        if (fread(pal768, 1, (unsigned)(lsize * 3), f) != (unsigned)(lsize * 3)) {
            fclose(f); return FALSE;
        }
        *ncol = lsize;
    }

    if (iw < 1 || ih < 1) { fclose(f); return FALSE; }
    if (!lzw_alloc()) { fclose(f); return FALSE; }

    o_buf = out; o_w = iw; o_h = ih; o_maxw = maxw; o_maxh = maxh;
    o_x = 0; o_y = 0; o_pass = 0;
    o_interlaced = (ipacked & 0x40) ? TRUE : FALSE;
    o_y = o_interlaced ? PASS_START[0] : 0;

    mcs = fgetc(f);
    if (mcs < 2 || mcs > 8) { lzw_free(); fclose(f); return FALSE; }
    br_init(f);
    clear = 1 << mcs;
    endc  = clear + 1;
    codesize = mcs + 1;
    nxt = clear + 2;
    for (i = 0; i < clear; ++i) { g_prefix[i] = 0xFFFF; g_suffix[i] = (u8)i; }
    first = 1; oldcode = -1; fb = 0;

    for (;;) {
        int code = br_code(codesize);
        int sp = 0, cur;
        if (code < 0 || code == endc)
            break;
        if (code == clear) {
            codesize = mcs + 1;
            nxt = clear + 2;
            first = 1;
            continue;
        }
        if (first) {                       /* first symbol after a clear     */
            if (code >= clear)             /* must be a root: corrupt file   */
                break;
            first = 0;
            oldcode = code;
            fb = g_suffix[code];
            emit((u8)fb);
            continue;
        }
        cur = code;
        if (cur >= nxt) {                  /* KwKwK: not yet in the table    */
            if (cur > nxt)                 /* beyond KwKwK: corrupt stream   */
                break;
            g_stack[sp++] = (u8)fb;
            cur = oldcode;
        }
        /* The guard has to come BEFORE the store: g_prefix (8192) +
           g_suffix (4096) + g_stack (4096) exactly fill the 16 KB block,
           so letting sp reach 4096 and then storing wrote one byte past
           the allocation - straight into the next MCB, corrupting the DOS
           arena chain.  Reachable from any corrupt GIF. */
        while (cur >= clear && sp < 4095) { /* chase the prefix chain        */
            g_stack[sp++] = g_suffix[cur];
            cur = g_prefix[cur];
        }
        fb = g_suffix[cur];
        if (sp < 4096)
            g_stack[sp++] = (u8)fb;
        while (sp > 0)                     /* output in reverse              */
            emit(g_stack[--sp]);
        if (nxt < 4096) {                  /* extend the dictionary          */
            g_prefix[nxt] = (u16)oldcode;
            g_suffix[nxt] = (u8)fb;
            ++nxt;
            if (nxt == (1 << codesize) && codesize < 12)
                ++codesize;
        }
        oldcode = code;
    }

    lzw_free();
    fclose(f);
    *w = (iw < maxw) ? iw : maxw;
    *h = (ih < maxh) ? ih : maxh;
    return TRUE;
}
