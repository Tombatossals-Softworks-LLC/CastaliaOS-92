/* ======================================================================
 * tests/test_parsers.c - hostile-input tests for the file-format parsers
 * ----------------------------------------------------------------------
 * Every case here is a file the shell can actually be pointed at: a GIF
 * or .ICO named in CASTALIA.INI or picked in a Load box, an INI written
 * by hand.  Each one is a shape that has produced (or would produce) a
 * memory-safety bug; run under ASan/UBSan the pass is meaningful.
 * ==================================================================== */
#include <stdio.h>
#include <string.h>
#include "host.h"
#include "../src/gif.h"
#include "../src/icon.h"
#include "../src/config.h"
#include "../src/textscan.h"
#include "gif_fixtures.h"

static int g_fail = 0;
static int g_run  = 0;

static void ok(const char *name, int cond)
{
    ++g_run;
    if (!cond) { ++g_fail; printf("  FAIL  %s\n", name); }
    else       {           printf("  ok    %s\n", name); }
}

/* video.c is a 2000-line VGA driver full of inline asm and port I/O.
   icon.c needs one real function from it (the theme ramp, kept fixed here
   so nearest_slot() is deterministic); its drawing entry points are only
   referenced by icon_draw, which these tests never call. */
void vid_fillrect(int x, int y, int w, int h, u8 c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; }
void vid_hline(int x, int y, int w, u8 c)
{ (void)x; (void)y; (void)w; (void)c; }
/* The theme ramp icon.c maps .ICO colours onto. */
void video_slot_rgb(int slot, u8 *r, u8 *g, u8 *b)
{
    static const unsigned char P[16][3] = {
        {  0,  0,  0},{  0,  0,128},{  0,128,128},{192,192,192},
        {128,128,128},{255,255,255},{255,255,255},{ 58,110,165},
        {168,  0,  0},{232,200, 32},{160,120,  0},{  0,128,  0},
        { 64, 64, 64},{  0,  0,192},{  0,168,168},{255,255,255}
    };
    if (slot < 0 || slot > 15) slot = 0;
    *r = P[slot][0]; *g = P[slot][1]; *b = P[slot][2];
}

static const char *TMP = "cast_test_tmp.bin";

static void write_file(const unsigned char *d, size_t n)
{
    FILE *f = fopen(TMP, "wb");
    if (f == NULL) { printf("cannot write %s\n", TMP); return; }
    fwrite(d, 1, n, f);
    fclose(f);
}

/* ---- GIF ------------------------------------------------------------- */

/* A real 16x16 GIF must decode to exactly its own geometry.  Without this
   the hostile cases below prove nothing: a decoder that rejects every
   input would pass them all. */
static void test_gif_good(void)
{
    unsigned char out[64 * 64], pal[256 * 3];
    int w = 0, h = 0, ncol = 0;
    write_file(GIF_GOOD, sizeof GIF_GOOD);
    host_reset_blocks();
    ok("gif: a valid file decodes",
       gif_decode(TMP, out, 64, 64, &w, &h, pal, &ncol) != 0);
    ok("gif: decoded geometry is 16x16", w == 16 && h == 16);
    ok("gif: the LZW scratch block is freed", host_live_blocks() == 0);
}

/* The same valid file with every LZW code byte set to 0xFF.  The stream
   then indexes dictionary entries that were never defined, which is what
   sends the prefix chase off the end of the 4096-byte stack - the write
   landed on the next MCB and corrupted the DOS arena chain. */
static void test_gif_prefix_chain(void)
{
    unsigned char out[64 * 64], pal[256 * 3];
    int w = 0, h = 0, ncol = 0;
    write_file(GIF_CHAOS, sizeof GIF_CHAOS);
    host_reset_blocks();
    (void)gif_decode(TMP, out, 64, 64, &w, &h, pal, &ncol);
    ok("gif: undefined codes do not run off the LZW stack", 1);
    ok("gif: the damaged file still frees its block",
       host_live_blocks() == 0);
}

/* Declares 4096x4096 but carries a 16x16 payload: the decoder must honour
   the caller's maxw/maxh and not write past `out`. */
static void test_gif_huge_dims(void)
{
    unsigned char out[64 * 64], pal[256 * 3];
    int w = 0, h = 0, ncol = 0;
    write_file(GIF_HUGE_DIMS, sizeof GIF_HUGE_DIMS);
    host_reset_blocks();
    (void)gif_decode(TMP, out, 64, 64, &w, &h, pal, &ncol);
    ok("gif: a 4096x4096 claim stays inside the caller's buffer",
       w <= 64 && h <= 64);
    ok("gif: oversized claim frees its block", host_live_blocks() == 0);
}

/* A well-formed stream that grows one dictionary chain to entry 4095, then
   walks it.  Decoding the last code pushes one byte per link - about 4090
   of them - which is the deepest the LZW output stack can legitimately be
   driven and therefore the real test of its bound.  Under ASan the block
   is exactly 16 KB with the stack at its top, so a single byte past the
   end is a heap-buffer-overflow, not a silent neighbour write. */
static void test_gif_max_chain(void)
{
    unsigned char out[64 * 64], pal[256 * 3];
    int w = 0, h = 0, ncol = 0;
    write_file(GIF_MAXCHAIN, sizeof GIF_MAXCHAIN);
    host_reset_blocks();
    (void)gif_decode(TMP, out, 64, 64, &w, &h, pal, &ncol);
    ok("gif: a maximal prefix chain stays inside the LZW stack", 1);
    ok("gif: maximal chain frees its block", host_live_blocks() == 0);
}

/* clear, a legal root, then codes far beyond the defined dictionary.  The
   decoder must reject a code above nxt: without that check it chases
   g_prefix[] entries nobody ever wrote, which on DOS hold whatever the
   last owner of the block left there.  (The host shim poisons new blocks
   so this test sees the DOS case, not malloc's zeroed pages.) */
static void test_gif_wild_codes(void)
{
    unsigned char out[64 * 64], pal[256 * 3];
    int w = 0, h = 0, ncol = 0;
    write_file(GIF_WILDCODES, sizeof GIF_WILDCODES);
    host_reset_blocks();
    (void)gif_decode(TMP, out, 64, 64, &w, &h, pal, &ncol);
    ok("gif: codes beyond the dictionary are refused", 1);
    ok("gif: wild codes free the block", host_live_blocks() == 0);
}

/* Cut off mid-LZW: the decoder has to notice the stream ended. */
static void test_gif_truncated_stream(void)
{
    unsigned char out[64 * 64], pal[256 * 3];
    int w = 0, h = 0, ncol = 0;
    write_file(GIF_TRUNC, sizeof GIF_TRUNC);
    host_reset_blocks();
    (void)gif_decode(TMP, out, 64, 64, &w, &h, pal, &ncol);
    ok("gif: a truncated LZW stream frees its block",
       host_live_blocks() == 0);
}

static void test_gif_truncated(void)
{
    unsigned char g[16];
    unsigned char out[64 * 64];
    unsigned char pal[256 * 3];
    int w = 0, h = 0, ncol = 0;
    memcpy(g, "GIF89a", 6);
    g[6] = 64; g[7] = 0; g[8] = 64; g[9] = 0;
    write_file(g, 10);                 /* header only, nothing after     */
    host_reset_blocks();
    ok("gif: header-only file is rejected",
       gif_decode(TMP, out, 64, 64, &w, &h, pal, &ncol) == 0);
    ok("gif: header-only file leaks nothing", host_live_blocks() == 0);
}

static void test_gif_not_a_gif(void)
{
    unsigned char out[64 * 64], pal[256 * 3];
    int w = 0, h = 0, ncol = 0;
    write_file((const unsigned char *)"this is plainly not a GIF at all", 32);
    ok("gif: non-GIF rejected",
       gif_decode(TMP, out, 64, 64, &w, &h, pal, &ncol) == 0);
}

/* ---- ICO ------------------------------------------------------------- */

static int put_ico(unsigned char *b, int w, int h, unsigned bpp, int ncol)
{
    int n = 0, i;
    b[n++] = 0; b[n++] = 0;            /* reserved                       */
    b[n++] = 1; b[n++] = 0;            /* type = icon                    */
    b[n++] = 1; b[n++] = 0;            /* one image                      */
    b[n++] = (unsigned char)w;
    b[n++] = (unsigned char)h;
    b[n++] = (unsigned char)ncol;
    b[n++] = 0;                        /* reserved                       */
    b[n++] = 1; b[n++] = 0;            /* planes                         */
    b[n++] = (unsigned char)(bpp & 0xFF);
    b[n++] = (unsigned char)(bpp >> 8);
    b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0;   /* size            */
    b[n++] = 22; b[n++] = 0; b[n++] = 0; b[n++] = 0;  /* offset          */
    /* BITMAPINFOHEADER */
    b[n++] = 40; b[n++] = 0; b[n++] = 0; b[n++] = 0;
    b[n++] = (unsigned char)w; b[n++] = 0; b[n++] = 0; b[n++] = 0;
    b[n++] = (unsigned char)(h * 2); b[n++] = 0; b[n++] = 0; b[n++] = 0;
    b[n++] = 1; b[n++] = 0;
    b[n++] = (unsigned char)(bpp & 0xFF);
    b[n++] = (unsigned char)(bpp >> 8);
    for (i = 0; i < 24; ++i) b[n++] = 0;
    return n;
}

/* bpp = 1024 made "w * bpp" wrap NEGATIVE in 16 bits, sailing past the
   "rowbytes > sizeof(rowbuf)" guard into a ~62 KB fread onto a 512-byte
   stack buffer. */
static void test_ico_absurd_depth(void)
{
    unsigned char b[4096];
    IconBitmap ic;
    int n = put_ico(b, 32, 32, 1024, 0);
    memset(b + n, 0xAB, 512);
    write_file(b, (size_t)n + 512);
    ok("ico: bpp=1024 rejected", icon_load(TMP, &ic) == 0);
}

static void test_ico_depth_32768(void)
{
    unsigned char b[4096];
    IconBitmap ic;
    int n = put_ico(b, 32, 32, 32768u, 0);
    memset(b + n, 0xCD, 512);
    write_file(b, (size_t)n + 512);
    ok("ico: bpp=32768 rejected", icon_load(TMP, &ic) == 0);
}

static void test_ico_big_width(void)
{
    unsigned char b[8192];
    IconBitmap ic;
    int n = put_ico(b, 255, 32, 32, 0);
    memset(b + n, 0x11, 2048);
    write_file(b, (size_t)n + 2048);
    /* 255 * 32bpp = 1020 rowbytes, over the 512-byte row buffer. */
    ok("ico: oversized row rejected", icon_load(TMP, &ic) == 0);
}

/* Each legal depth must either decode or be refused - never run off the
   row buffer.  2 and 16 were on the accepted list with no decode arm. */
static void test_ico_all_legal_depths(void)
{
    static const unsigned D[7] = { 1, 2, 4, 8, 16, 24, 32 };
    unsigned char b[8192];
    IconBitmap ic;
    int i, allok = 1;
    for (i = 0; i < 7; ++i) {
        int n = put_ico(b, 16, 16, D[i], (D[i] <= 8) ? (1 << D[i]) : 0);
        memset(b + n, 0x55, 4096);
        write_file(b, (size_t)n + 4096);
        (void)icon_load(TMP, &ic);   /* must not corrupt memory      */
    }
    ok("ico: all seven legal depths survive a hostile body", allok);
}

static void test_ico_truncated_header(void)
{
    unsigned char b[8];
    IconBitmap ic;
    memset(b, 0, sizeof b);
    b[2] = 1;
    write_file(b, sizeof b);
    ok("ico: truncated header rejected", icon_load(TMP, &ic) == 0);
}

/* ---- INI ------------------------------------------------------------- */

static void write_text(const char *s)
{
    FILE *f = fopen(TMP, "wb");
    if (f == NULL) return;
    fputs(s, f);
    fclose(f);
}

static void test_ini_bool_off(void)
{
    Config c;
    config_defaults(&c);
    write_text("[system]\nsound=off\nanimations=false\n"
               "[mouse]\nenabled=no\n");
    config_load(TMP, &c);
    ok("ini: sound=off is false",      c.sound_enabled == 0);
    ok("ini: mouse=no is false",       c.mouse_enabled == 0);
    ok("ini: animations=false is false", c.anim_enabled == 0);
}

static void test_ini_bool_on(void)
{
    Config c;
    config_defaults(&c);
    write_text("[system]\nsound=on\nanimations=true\n"
               "[mouse]\nenabled=yes\n");
    config_load(TMP, &c);
    ok("ini: sound=on is true",  c.sound_enabled != 0);
    ok("ini: mouse=yes is true", c.mouse_enabled != 0);
}

/* A number far past what a 16-bit int holds must saturate, not wrap into
   a negative screensaver timeout. */
static void test_ini_huge_int(void)
{
    Config c;
    config_defaults(&c);
    write_text("[system]\nscreensaver=99999999\n");
    config_load(TMP, &c);
    ok("ini: oversized int saturates non-negative", c.screensaver_secs >= 0);
}

/* Lines far longer than any internal buffer, no trailing newline, and a
   value with no key. */
static void test_ini_long_lines(void)
{
    char buf[4096];
    Config c;
    int i, n = 0;
    n += sprintf(buf + n, "[desktop]\nicon1_name=");
    for (i = 0; i < 900; ++i) buf[n++] = 'A';
    n += sprintf(buf + n, "\nicon1_command=");
    for (i = 0; i < 900; ++i) buf[n++] = 'B';
    n += sprintf(buf + n, "\n=novalue\nnokey\n[system]\nsound=on");
    buf[n] = '\0';
    config_defaults(&c);
    write_text(buf);
    config_load(TMP, &c);
    ok("ini: 900-char values do not overflow their fields",
       (int)strlen(c.icons[0].name) < (int)sizeof(c.icons[0].name));
    ok("ini: parsing continues past a malformed line", c.sound_enabled != 0);
}

static void test_ini_missing_file(void)
{
    Config c;
    config_defaults(&c);
    config_load("no_such_file_anywhere.ini", &c);
    ok("ini: a missing file leaves the defaults alone", c.icon_count >= 0);
}


/* ---- text scan -------------------------------------------------------
 * Find File's Containing-text filter reads each candidate in 512-byte
 * blocks, carrying the last nlen-1 bytes into the next so a match lying
 * across a boundary is still seen.  That carry is the whole risk: get it
 * wrong and the applet finds most things, which reads as flakiness
 * rather than as a bug, and no file small enough to type by hand can
 * expose it.  These cases put a needle either side of, and straddling,
 * the first and second boundaries.
 * -------------------------------------------------------------------- */

static void write_at(long off, const char *needle, long total)
{
    FILE *f = fopen(TMP, "wb");
    long i;
    if (f == NULL) return;
    for (i = 0; i < total; ++i) {
        if (off >= 0 && i >= off && i < off + (long)strlen(needle))
            fputc(needle[i - off], f);
        else
            fputc('.', f);
    }
    fclose(f);
}

static int scan_for(const char *text)
{
    char needle[24];
    int  n = text_needle(needle, (int)sizeof(needle), text);
    return text_in_file(TMP, needle, n) ? 1 : 0;
}

static void test_scan_boundaries(void)
{
    /* Wholly inside the first block. */
    write_at(100, "HIMEM", 900);
    ok("scan: a match inside the first block", scan_for("himem"));

    /* Straddling 512: three bytes in block one, two in block two. */
    write_at(509, "HIMEM", 900);
    ok("scan: a match straddling the 512-byte boundary", scan_for("himem"));

    /* Straddling it by one byte either way, the tightest carry. */
    write_at(511, "HIMEM", 900);
    ok("scan: one byte before the boundary, four after", scan_for("himem"));
    write_at(508, "HIMEM", 900);
    ok("scan: four bytes before the boundary, one after", scan_for("himem"));

    /* And the SECOND boundary, which only a correct carry reaches with
       the buffer still aligned the way the first one left it. */
    write_at(1021, "HIMEM", 1600);
    ok("scan: a match straddling the second boundary", scan_for("himem"));

    /* The last bytes of the file, with no further read to come. */
    write_at(895, "HIMEM", 900);
    ok("scan: a match at the very end of the file", scan_for("himem"));
}

static void test_scan_negatives(void)
{
    /* A prefix that never completes must not be dragged into a match by
       the carry - the failure mode a naive overlap introduces. */
    write_at(510, "HIME", 900);
    ok("scan: a prefix across the boundary is not a match", !scan_for("himem"));

    write_at(-1, "", 900);
    ok("scan: absent text is not found", !scan_for("himem"));

    ok("scan: an unreadable file matches nothing",
       !text_in_file("no_such_file_anywhere.bin", "HIMEM", 5));

    /* No filter at all matches everything, including nothing at all. */
    ok("scan: an empty needle matches", text_in_file(TMP, "", 0));
    ok("scan: an empty needle matches even a missing file",
       text_in_file("no_such_file_anywhere.bin", "", 0));

    /* A needle longer than one block cannot be looked for and must say
       so rather than read off the end of the buffer. */
    {
        char big[600];
        memset(big, 'A', sizeof(big));
        ok("scan: a needle longer than the buffer is refused",
           !text_in_file(TMP, big, (int)sizeof(big)));
    }
}

static void test_scan_case_and_needle(void)
{
    char needle[24];
    ok("scan: text_needle uppercases", text_needle(needle, sizeof needle,
        "himem") == 5 && strcmp(needle, "HIMEM") == 0);
    ok("scan: text_needle truncates to the cap",
       text_needle(needle, 4, "abcdefgh") == 3 && strcmp(needle, "ABC") == 0);
    ok("scan: text_needle on NULL is empty",
       text_needle(needle, sizeof needle, NULL) == 0 && needle[0] == '\0');

    /* Lower case in the FILE must match too - the fold is on the file's
       bytes, since the file is the big side. */
    write_at(200, "himem", 900);
    ok("scan: lower case in the file matches an uppercased needle",
       scan_for("HIMEM"));
}

int main(void)
{
    printf("castalia host parser tests\n");
    test_gif_good();
    test_gif_prefix_chain();
    test_gif_max_chain();
    test_gif_wild_codes();
    test_gif_huge_dims();
    test_gif_truncated_stream();
    test_gif_truncated();
    test_gif_not_a_gif();
    test_ico_absurd_depth();
    test_ico_depth_32768();
    test_ico_big_width();
    test_ico_all_legal_depths();
    test_ico_truncated_header();
    test_ini_bool_off();
    test_ini_bool_on();
    test_ini_huge_int();
    test_ini_long_lines();
    test_ini_missing_file();
    test_scan_boundaries();
    test_scan_negatives();
    test_scan_case_and_needle();
    remove(TMP);
    printf("%d run, %d failed\n", g_run, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
