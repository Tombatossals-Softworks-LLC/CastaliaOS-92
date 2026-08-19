/* ======================================================================
 * icon.c - External bitmap icons for CASTALIA/386
 * ----------------------------------------------------------------------
 * Two formats share one IconBitmap (32x32 of theme-slot indices, or the
 * transparent sentinel): the human-readable .ICN text format, and real
 * Windows .ICO files (1/4/8/24/32-bpp DIB + AND mask).  An .ICO's colours
 * are mapped once at load to the nearest of the 16 active theme slots, so
 * genuine Win95 icon art renders here AND recolours with the theme.
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "icon.h"
#include "video.h"

static u8 hexval(char c)
{
    if (c >= '0' && c <= '9') return (u8)(c - '0');
    if (c >= 'a' && c <= 'f') return (u8)(10 + c - 'a');
    if (c >= 'A' && c <= 'F') return (u8)(10 + c - 'A');
    return ICON_TRANSPARENT;          /* '.', ' ', anything else          */
}

/* ---- ICO (Windows icon) decoder -------------------------------------- */

/* The 16 active theme colours, cached at load so nearest_slot() need not
   hit the DAC shadow for every one of an icon's palette entries. */
static int g_thr[16], g_thg[16], g_thb[16];
static void cache_theme(void)
{
    int s;
    u8  r, g, b;
    for (s = 0; s < 16; ++s) {
        video_slot_rgb(s, &r, &g, &b);
        g_thr[s] = r; g_thg[s] = g; g_thb[s] = b;
    }
}
static u8 nearest_slot(int r, int g, int b)
{
    int s, best = 0;
    long bd = 1L << 28;
    for (s = 0; s < 16; ++s) {
        long dr = r - g_thr[s], dg = g - g_thg[s], db = b - g_thb[s];
        long d  = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = s; }
    }
    return (u8)best;
}

static unsigned rdu16(const unsigned char *p)
{ return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned long rdu32(const unsigned char *p)
{ return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
         ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }

/* Decode a .ICO into ic (32x32 theme-slot pixels).  Returns TRUE on
   success.  Handles 1/4/8-bpp (palette) and 24/32-bpp (truecolor) DIBs,
   the AND transparency mask, and any icon size (scaled to 32x32). */
static bool_t icon_load_ico(const char *path, IconBitmap far *ic)
{
    FILE *f;
    unsigned char dir[6], ent[16], bih[40];
    /* pal and rowbuf stay NEAR on the stack because fread() writes into
       them and libc takes near pointers in the medium model - a far
       buffer there would truncate to DGROUP.  img never touches libc, so
       it moves to far: 1 KB off a frame that was 2910 bytes (35% of the
       8 KB stack, and icon_load runs in a loop from desktop_init), at no
       DGROUP cost.  static is safe - this function is not recursive and
       the picture is consumed before it returns. */
    unsigned char pal[256 * 4];
    unsigned char rowbuf[512];
    u8            map[256];            /* palette index -> theme slot       */
    static u8 far img[32 * 32];        /* decoded native picture (<=32)     */
    int  count, i, best = -1, bestscore = -9999;
    unsigned long best_off = 0;
    int  w, h, bpp, ncol, rowbytes, y, x;

    f = fopen(path, "rb");
    if (f == NULL)
        return FALSE;
    if (fread(dir, 1, 6, f) != 6 || dir[0] || dir[1] || dir[2] != 1) {
        fclose(f);                     /* not "reserved 0, type 1"          */
        return FALSE;
    }
    count = (int)rdu16(dir + 2);
    if (count < 1) { fclose(f); return FALSE; }
    if (count > 32) count = 32;

    /* Pick the entry closest to 32x32 (ties: the one with more data). */
    for (i = 0; i < count; ++i) {
        int ew, score;
        long bytes;
        if (fread(ent, 1, 16, f) != 16) break;
        ew = ent[0] ? ent[0] : 256;
        bytes = (long)rdu32(ent + 8);
        score = -((ew > 32) ? (ew - 32) * 4 : (32 - ew)) + (int)(bytes >> 8);
        if (score > bestscore) {
            bestscore = score;
            best = i;
            best_off = rdu32(ent + 12);
        }
    }
    if (best < 0) { fclose(f); return FALSE; }

    if (fseek(f, (long)best_off, SEEK_SET) != 0 ||
        fread(bih, 1, 40, f) != 40 || rdu32(bih) < 40) {
        fclose(f);
        return FALSE;
    }
    w   = (int)rdu32(bih + 4);
    h   = (int)rdu32(bih + 8) / 2;     /* height counts XOR + AND masks     */
    bpp = (int)rdu16(bih + 14);
    if (rdu32(bih + 16) != 0) { fclose(f); return FALSE; }   /* compressed  */
    if (w < 1 || h < 1 || w > 256 || h > 256) { fclose(f); return FALSE; }
    /* biBitCount comes straight off the disk and was never checked.  Two
       things went wrong with a hostile value: (1) w * bpp is a 16-bit
       multiply, so bpp=1024 wrapped rowbytes NEGATIVE, sailed past the
       "> sizeof(rowbuf)" guard below and turned into a ~62 KB fread into
       a 512-byte STACK buffer; (2) any bpp above 32767 read back negative,
       took the "<= 8" branch here and shifted by a negative count.
       Only the seven legal DIB depths get through now. */
    if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8 &&
        bpp != 16 && bpp != 24 && bpp != 32) {
        fclose(f); return FALSE;
    }

    ncol = (bpp <= 8) ? (1 << bpp) : 0;
    cache_theme();
    if (ncol > 0) {
        if (fread(pal, 1, (unsigned)(ncol * 4), f) != (unsigned)(ncol * 4)) {
            fclose(f);
            return FALSE;
        }
        for (i = 0; i < ncol; ++i)     /* RGBQUAD is B,G,R,0                 */
            map[i] = nearest_slot(pal[i * 4 + 2], pal[i * 4 + 1], pal[i * 4]);
    }

    for (i = 0; i < 32 * 32; ++i)
        img[i] = ICON_TRANSPARENT;

    /* XOR (colour) bitmap: rows bottom-up, each padded to a DWORD. */
    /* long: w and bpp are both bounded now, but 256 * 32 still exceeds a
       16-bit int's range, so size the row in 32-bit arithmetic. */
    rowbytes = (int)((((long)w * bpp + 31L) / 32L) * 4L);
    if (rowbytes < 1 || rowbytes > (int)sizeof(rowbuf)) {
        fclose(f); return FALSE;
    }
    for (y = 0; y < h; ++y) {
        int dy = h - 1 - y;            /* flip: file is bottom-up           */
        /* Read every row so the stream stays aligned (an icon taller than
           32 px still has its top rows on disk); only skip PROCESSING the
           ones that fall outside the 32x32 scratch. */
        if (fread(rowbuf, 1, (unsigned)rowbytes, f) != (unsigned)rowbytes)
            break;
        if (dy >= 32) continue;
        for (x = 0; x < w && x < 32; ++x) {
            u8 v;
            if (bpp == 8) {
                v = map[rowbuf[x]];
            } else if (bpp == 4) {
                unsigned b = rowbuf[x >> 1];
                v = map[(x & 1) ? (b & 0x0F) : (b >> 4)];
            } else if (bpp == 2) {
                unsigned b = rowbuf[x >> 2];
                v = map[(b >> (6 - 2 * (x & 3))) & 3];
            } else if (bpp == 1) {
                unsigned b = rowbuf[x >> 3];
                v = map[(b >> (7 - (x & 7))) & 1];
            } else if (bpp == 16) {
                /* BI_RGB 16bpp is RGB555, little-endian; widen each 5-bit
                   channel to 8 bits (v<<3 | v>>2) before matching.  Both
                   this depth and 2bpp were on the accepted list but had no
                   arm, so they fell into the 32bpp branch and decoded to
                   noise. */
                unsigned p = (unsigned)rowbuf[x*2] |
                             ((unsigned)rowbuf[x*2+1] << 8);
                unsigned r5 = (p >> 10) & 31, g5 = (p >> 5) & 31, b5 = p & 31;
                v = nearest_slot((u8)((r5 << 3) | (r5 >> 2)),
                                 (u8)((g5 << 3) | (g5 >> 2)),
                                 (u8)((b5 << 3) | (b5 >> 2)));
            } else if (bpp == 24) {
                v = nearest_slot(rowbuf[x*3+2], rowbuf[x*3+1], rowbuf[x*3]);
            } else {                   /* 32bpp BGRA                         */
                if (rowbuf[x*4+3] < 128) { img[dy*32+x] = ICON_TRANSPARENT; continue; }
                v = nearest_slot(rowbuf[x*4+2], rowbuf[x*4+1], rowbuf[x*4]);
            }
            img[dy * 32 + x] = v;
        }
    }

    /* AND (transparency) mask: 1bpp, bottom-up, DWORD-padded; 1 = clear. */
    rowbytes = ((w + 31) / 32) * 4;
    if (rowbytes <= (int)sizeof(rowbuf)) {
        for (y = 0; y < h; ++y) {
            int dy = h - 1 - y;
            if (fread(rowbuf, 1, (unsigned)rowbytes, f) != (unsigned)rowbytes)
                break;
            if (dy >= 32) continue;
            for (x = 0; x < w && x < 32; ++x)
                if ((rowbuf[x >> 3] >> (7 - (x & 7))) & 1)
                    img[dy * 32 + x] = ICON_TRANSPARENT;
        }
    }
    fclose(f);

    /* Scale the native w x h picture to the 32x32 IconBitmap (nearest). */
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x) {
            int sx = (w == 32) ? x : (x * w) / 32;
            int sy = (h == 32) ? y : (y * h) / 32;
            if (sx >= 32) sx = 31;
            if (sy >= 32) sy = 31;
            ic->px[y * 32 + x] = img[sy * 32 + sx];
        }
    ic->loaded = TRUE;
    return TRUE;
}

bool_t icon_load(const char *path, IconBitmap far *ic)
{
    FILE *f;
    char  line[96];
    int   w = 0, h = 0, row, i;

    /* Whatever happens below, this slot's pixels are no longer what the
       2x cache may be holding for it.  One call at the top covers the
       success paths, the failure paths and the .ICO branch alike. */
    icon_cache_flush();
    ic->loaded = FALSE;
    if (path == NULL || path[0] == '\0')
        return FALSE;

    /* A real Windows .ICO begins with 00 00 01 00; hand it to the decoder. */
    {
        unsigned char sig[4];
        FILE *pf = fopen(path, "rb");
        if (pf != NULL) {
            int n = (int)fread(sig, 1, 4, pf);
            fclose(pf);
            if (n == 4 && sig[0] == 0 && sig[1] == 0 &&
                sig[2] == 1 && sig[3] == 0)
                return icon_load_ico(path, ic);
        }
    }

    f = fopen(path, "r");
    if (f == NULL)
        return FALSE;

    /* Header: skip comments/blank lines, then read "CICN w h". */
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == ';' || line[0] == '\n' || line[0] == '\r')
            continue;
        if ((line[0] == 'C' || line[0] == 'c') &&
            (line[1] == 'I' || line[1] == 'i')) {
            sscanf(line + 4, "%d %d", &w, &h);
        }
        break;
    }
    if (w != ICON_BM_SIZE || h != ICON_BM_SIZE) {
        fclose(f);
        return FALSE;
    }

    for (i = 0; i < ICON_BM_SIZE * ICON_BM_SIZE; ++i)
        ic->px[i] = ICON_TRANSPARENT;

    row = 0;
    while (row < ICON_BM_SIZE && fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == ';')
            continue;
        for (i = 0; i < ICON_BM_SIZE; ++i) {
            char c = line[i];
            if (c == '\0' || c == '\n' || c == '\r')
                break;
            ic->px[row * ICON_BM_SIZE + i] = hexval(c);
        }
        ++row;
    }
    fclose(f);
    ic->loaded = TRUE;
    return TRUE;
}

/* ---- the 2x path: EPX/Scale2x instead of pixel doubling ---------------
 * Mode 12h shows the same 32x32 art at 64x64.  Plain doubling turns every
 * diagonal into a flight of 2x2 steps, which rectilinear art survives but
 * round art does not - the About bubble's outline came out as a visibly
 * coarse staircase.
 *
 * EPX fills a doubled pixel's corner with a neighbour when the two
 * neighbours meeting at that corner agree and the opposite pair does not.
 * It rounds a diagonal without inventing a colour: every output pixel is
 * a colour already in the source, so the result still lands inside the
 * 16 entries Mode 12h has to spend.
 *
 * It composes into a scratch buffer rather than drawing in place, because
 * EPX turns opaque corners transparent as often as the reverse.  Drawing
 * straight to the screen could only ever ADD pixels - there is no way to
 * un-draw one - so a silhouette would come out fattened on both sides of
 * each step rather than smoothed.  The buffer is far: 4 KB is a sixth of
 * what is left in DGROUP, and this never needs to be near.
 * -------------------------------------------------------------------- */
#define EPX_SIZE (ICON_BM_SIZE * 2)
static u8 far g_epx[EPX_SIZE * EPX_SIZE];

/* Whose pixels g_epx currently holds, so the expansion is not redone for
   an icon already in the buffer.  This matters: the desktop composes
   unselected icons into the scene cache once, but redraws the SELECTED
   cell every frame, which would otherwise put a 1024-pixel EPX pass in
   the frame loop - the exact per-frame cost the scene cache exists to
   remove.
   The pointer alone is not a safe key.  desktop.c swaps IconBitmap
   CONTENTS between slots when icons are rearranged, and icon_load()
   rewrites a slot in place on a config reload; both leave the address
   unchanged while the pixels behind it change.  Hence the flush, called
   by icon_load() itself so every reload is covered without the caller
   having to remember, and by desktop.c across the swap. */
static const IconBitmap far *g_epx_src = (const IconBitmap far *)0;

void icon_cache_flush(void)
{
    g_epx_src = (const IconBitmap far *)0;
}

static void icon_epx(const IconBitmap far *ic)
{
    int r, c;
    for (r = 0; r < ICON_BM_SIZE; ++r) {
        const u8 far *row = ic->px + r * ICON_BM_SIZE;
        u8 far *o0 = g_epx + (r * 2) * EPX_SIZE;
        u8 far *o1 = o0 + EPX_SIZE;
        for (c = 0; c < ICON_BM_SIZE; ++c) {
            u8 p  = row[c];
            u8 up = (r > 0)                 ? row[c - ICON_BM_SIZE] : p;
            u8 dn = (r < ICON_BM_SIZE - 1)  ? row[c + ICON_BM_SIZE] : p;
            u8 lf = (c > 0)                 ? row[c - 1]            : p;
            u8 rt = (c < ICON_BM_SIZE - 1)  ? row[c + 1]            : p;
            u8 e0 = p, e1 = p, e2 = p, e3 = p;
            /* Each EPX rule also requires the two "far" neighbours to
               differ, but under this guard those follow: if lf==up then
               up!=dn already gives lf!=dn, and lf!=rt gives up!=rt.  So
               inside the guard one comparison decides each corner, and
               outside it no corner can change - which is the whole point,
               because that is the flat-area case and it stays cheap. */
            if (up != dn && lf != rt) {
                if (lf == up) e0 = up;
                if (up == rt) e1 = rt;
                if (dn == lf) e2 = lf;
                if (rt == dn) e3 = dn;
            }
            o0[c * 2] = e0; o0[c * 2 + 1] = e1;
            o1[c * 2] = e2; o1[c * 2 + 1] = e3;
        }
    }
}

void icon_draw(const IconBitmap far *ic, int x, int y, int scale)
{
    int r, c, n;
    const u8 far *p;

    if (scale != 1) {
        if (ic != g_epx_src) {
            icon_epx(ic);
            g_epx_src = ic;
        }
        p = g_epx;
        n = EPX_SIZE;
    } else {
        p = ic->px;
        n = ICON_BM_SIZE;
    }

    /* Emit each row as RUNS of equal colour: icon art is mostly flat
       areas, so a row becomes a handful of hline fills instead of up to
       32 (or 64, at 2x) individual clipped pixel calls per row. */
    for (r = 0; r < n; ++r) {
        const u8 far *row = p + r * n;
        c = 0;
        while (c < n) {
            u8 v = row[c];
            int run = 1;
            if (v == ICON_TRANSPARENT) { ++c; continue; }
            while (c + run < n && row[c + run] == v)
                ++run;
            vid_hline(x + c, y + r, run, v);
            c += run;
        }
    }
}

/* Nearest-neighbour blit of the native 32x32 icon into a size x size box.
   Each destination pixel samples the source column/row it maps back to, and
   equal-colour runs collapse into one hline - so a 10 px Start-button badge
   costs a handful of fills, not 100 clipped pixels.  Transparent source
   pixels are skipped, letting the button face show through. */
void icon_draw_box(const IconBitmap far *ic, int x, int y, int size)
{
    int dy, dx;
    if (size < 1) return;
    if (size >= ICON_BM_SIZE * 2) { icon_draw(ic, x, y, 2); return; }
    if (size == ICON_BM_SIZE)     { icon_draw(ic, x, y, 1); return; }
    for (dy = 0; dy < size; ++dy) {
        int sy = (dy * ICON_BM_SIZE) / size;
        const u8 far *row = ic->px + (long)sy * ICON_BM_SIZE;
        dx = 0;
        while (dx < size) {
            u8 v = row[(dx * ICON_BM_SIZE) / size];
            int run = 1;
            if (v == ICON_TRANSPARENT) { ++dx; continue; }
            while (dx + run < size &&
                   row[((dx + run) * ICON_BM_SIZE) / size] == v)
                ++run;
            vid_hline(x + dx, y + dy, run, v);
            dx += run;
        }
    }
}
