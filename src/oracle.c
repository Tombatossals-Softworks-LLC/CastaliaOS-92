/* ======================================================================
 * oracle.c - The System Oracle for CASTALIA/386
 * ====================================================================== */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "oracle.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "system.h"
#include "sblaster.h"
#include "opl.h"
#include "keyboard.h"

/* ---- bare-metal helpers (386 inline, mirrors video.c's copy32) ------- */

/* Read / write the full 32-bit EFLAGS from 16-bit real mode. */
extern unsigned long ef_get(void);
#pragma aux ef_get =        \
    ".386"                  \
    "pushfd"                \
    "pop  eax"              \
    "mov  edx, eax"         \
    "shr  edx, 16"          \
    value [dx ax] modify [ax dx];

extern void ef_set(unsigned long f);
#pragma aux ef_set =        \
    ".386"                  \
    "shl   edx, 16"         \
    "movzx eax, ax"         \
    "or    eax, edx"        \
    "push  eax"             \
    "popfd"                 \
    parm [dx ax] modify [ax dx];

/* CPUID, one register at a time (only used after the ID-bit test). */
extern unsigned long cpuid_eax(unsigned long leaf);
#pragma aux cpuid_eax =     \
    ".586"                  \
    "shl   edx, 16"         \
    "movzx eax, ax"         \
    "or    eax, edx"        \
    "cpuid"                 \
    "mov   edx, eax"        \
    "shr   edx, 16"         \
    parm [dx ax] value [dx ax] modify [ax bx cx dx];

extern unsigned long cpuid0_ebx(void);
#pragma aux cpuid0_ebx =    \
    ".586"                  \
    "xor  eax, eax"         \
    "cpuid"                 \
    "mov  eax, ebx"         \
    "mov  edx, eax"         \
    "shr  edx, 16"          \
    value [dx ax] modify [ax bx cx dx];

extern unsigned long cpuid0_edx(void);
#pragma aux cpuid0_edx =    \
    ".586"                  \
    "xor  eax, eax"         \
    "cpuid"                 \
    "mov  eax, edx"         \
    "mov  edx, eax"         \
    "shr  edx, 16"          \
    value [dx ax] modify [ax bx cx dx];

extern unsigned long cpuid0_ecx(void);
#pragma aux cpuid0_ecx =    \
    ".586"                  \
    "xor  eax, eax"         \
    "cpuid"                 \
    "mov  eax, ecx"         \
    "mov  edx, eax"         \
    "shr  edx, 16"          \
    value [dx ax] modify [ax bx cx dx];

/* FPU probe: FNSTSW is a no-op without a coprocessor, so AX keeps the
   sentinel; with one, FNINIT clears the status word's low byte. */
extern unsigned fpu_probe(void);
#pragma aux fpu_probe =     \
    ".387"                  \
    "mov  ax, 5A5Ah"        \
    "fninit"                \
    "fnstsw ax"             \
    value [ax] modify [ax];

/* 386 dword block copy between two segments (the memory benchmark). */
extern void orc_copy32(unsigned dseg, unsigned doff,
                       unsigned sseg, unsigned soff, unsigned ndwords);
#pragma aux orc_copy32 =          \
    ".386"                        \
    "push ds"                     \
    "mov   es, ax"                \
    "mov   ds, dx"                \
    "cld"                         \
    "rep   movsd"                 \
    "pop   ds"                    \
    parm [ax] [di] [dx] [si] [cx] \
    modify [es si di cx];

/* ---- probed facts ----------------------------------------------------- */

#define PG_OVER  0
#define PG_CPU   1
#define PG_MEM   2
#define PG_VIDEO 3
#define PG_DISK  4
#define PG_DOS   5
#define PG_PORTS 6
#define PG_BENCH 7
#define PG_N     8

static const char * const PAGES[PG_N] = {
    "Overview", "CPU", "Memory", "Video",
    "Disks", "DOS/BIOS", "Ports", "Benchmark"
};

static int    g_page   = PG_OVER;
static bool_t g_probed = FALSE;

static char   g_cpu_name[26];
static char   g_vendor[14];
static bool_t g_cpuid;
static int    g_family;
static bool_t g_fpu;
static unsigned long g_loops;          /* spin loops / tick; 0 = unmeasured */

static unsigned      g_conv_kb;
static unsigned long g_ext_kb;
static bool_t        g_xms, g_ems;
static unsigned      g_ems_free, g_ems_total;    /* 16 KB pages            */
static unsigned      g_dos_para;                 /* largest free DOS block */

static bool_t g_vesa;
static int    g_vesa_hi, g_vesa_lo;
static unsigned long g_vesa_kb;
static u8     g_vmode;

typedef struct { char letter; unsigned long tot_kb, free_kb; } Vol;
static Vol g_vol[4];
static int g_nvol;
static int g_bios_disks;
static bool_t g_geo_ok;
static unsigned g_cyl, g_heads, g_sects;

static u8   g_dos_hi, g_dos_lo;
static char g_boot;
static char g_bdate[10];
static u8   g_model;

static unsigned g_com[4], g_lpt[3];
static bool_t g_mouse;
static int  g_mv_hi, g_mv_lo;

/* Benchmark results (runs completed in the fixed 4-tick window; 0 = not
   yet run).  Six sub-tests stress the integer core, memory bus and the
   video path independently, then fold into one composite Oracle Score. */
static unsigned long g_b_alu, g_b_mul, g_b_mcopy, g_b_mread, g_b_vfill, g_b_vline;
static bool_t        g_bench_done = FALSE;
static volatile unsigned long g_sink;   /* defeats dead-store elimination     */

/* ---- probing ----------------------------------------------------------- */

static void probe_cpu(void)
{
    unsigned long f, f2;

    g_cpuid  = FALSE;
    g_family = 3;
    strcpy(g_vendor, "");

    /* Castalia only boots on a 386+, so the question is: how much more?
       AC (bit 18) toggles on a 486+; ID (bit 21) toggles where CPUID
       exists.  Restore the flags after each experiment. */
    f = ef_get();
    ef_set(f ^ 0x40000UL);             /* flip AC                          */
    f2 = ef_get();
    ef_set(f);
    if (((f ^ f2) & 0x40000UL) != 0) {
        g_family = 4;
        ef_set(f ^ 0x200000UL);        /* flip ID                          */
        f2 = ef_get();
        ef_set(f);
        if (((f ^ f2) & 0x200000UL) != 0)
            g_cpuid = TRUE;
    }

    if (g_cpuid) {
        unsigned long b = cpuid0_ebx(), d = cpuid0_edx(), c = cpuid0_ecx();
        unsigned long sig = cpuid_eax(1UL);
        memcpy(g_vendor + 0, &b, 4);
        memcpy(g_vendor + 4, &d, 4);
        memcpy(g_vendor + 8, &c, 4);
        g_vendor[12] = '\0';
        g_family = (int)((sig >> 8) & 0x0F);
    }

    if      (g_family >= 6) strcpy(g_cpu_name, "686-class (or better)");
    else if (g_family == 5) strcpy(g_cpu_name, "586 / Pentium class");
    else if (g_family == 4) strcpy(g_cpu_name, "80486 class");
    else                    strcpy(g_cpu_name, "80386 (SX or DX)");

    g_fpu = ((fpu_probe() & 0x00FF) == 0) ? TRUE : FALSE;
}

static void probe_mem(void)
{
    union REGS r;

    /* system.c already speaks XMS (it calls the driver's entry point for
       the true above-1MB figure, then falls back to INT 15h) - reuse it. */
    g_conv_kb = system_conventional_kb();
    g_ext_kb  = (unsigned long)system_extended_kb();

    r.w.ax = 0x4300;                   /* XMS driver installed?            */
    int86(0x2F, &r, &r);
    g_xms = (r.h.al == 0x80) ? TRUE : FALSE;

    /* EMS: the EMM's device header carries the "EMMXXXX0" name. */
    g_ems = FALSE;
    g_ems_free = g_ems_total = 0;
    {
        void far *v = _dos_getvect(0x67);
        if (v != (void far *)0) {
            const char far *nm = (const char far *)
                MK_FP(FP_SEG(v), 0x000A);
            if (_fmemcmp(nm, "EMMXXXX0", 8) == 0) {
                g_ems = TRUE;
                r.h.ah = 0x42;
                int86(0x67, &r, &r);
                if (r.h.ah == 0) {
                    g_ems_free  = r.w.bx;
                    g_ems_total = r.w.dx;
                }
            }
        }
    }

    r.h.ah = 0x48;                     /* largest free DOS block           */
    r.w.bx = 0xFFFF;                   /* (asks too much on purpose)       */
    int86(0x21, &r, &r);
    g_dos_para = r.w.bx;
}

static void probe_video(void)
{
    union REGS r;
    struct SREGS s;
    unsigned seg = 0;

    r.h.ah = 0x0F;                     /* current BIOS mode                */
    int86(0x10, &r, &r);
    g_vmode = r.h.al;

    g_vesa = FALSE;
    g_vesa_kb = 0;
    if (_dos_allocmem(32, &seg) == 0) {          /* 512-byte info block    */
        u8 far *p = (u8 far *)MK_FP(seg, 0);
        segread(&s);
        s.es   = seg;
        r.w.di = 0;
        r.w.ax = 0x4F00;
        int86x(0x10, &r, &r, &s);
        if (r.w.ax == 0x004F &&
            p[0] == 'V' && p[1] == 'E' && p[2] == 'S' && p[3] == 'A') {
            g_vesa    = TRUE;
            g_vesa_lo = p[4];
            g_vesa_hi = p[5];
            g_vesa_kb = ((unsigned long)*(u16 far *)(p + 0x12)) * 64UL;
        }
        _dos_freemem(seg);
    }
}

static void probe_disks(void)
{
    union REGS r;
    int d;

    g_nvol = 0;
    for (d = 3; d <= 26 && g_nvol < 4; ++d) {    /* C: .. Z:               */
        r.h.ah = 0x36;
        r.h.dl = (u8)d;
        int86(0x21, &r, &r);
        if (r.w.ax != 0xFFFF) {
            /* Cluster bytes, NOT cluster kilobytes: a floppy (and any small
               FAT16 partition) has one 512-byte sector per cluster, so the
               old /1024 truncated to zero and every such volume reported
               "0 KB (0 KB free)".  Multiply by the cluster count first and
               divide once at the end - clusters * bytes still fits 32 bits
               for any DOS volume (2 GB is the ceiling). */
            unsigned long clus_b =
                (unsigned long)r.w.ax * (unsigned long)r.w.cx;
            g_vol[g_nvol].letter  = (char)('A' + d - 1);
            g_vol[g_nvol].tot_kb  = clus_b / 1024UL * (unsigned long)r.w.dx +
                                    clus_b % 1024UL * (unsigned long)r.w.dx / 1024UL;
            g_vol[g_nvol].free_kb = clus_b / 1024UL * (unsigned long)r.w.bx +
                                    clus_b % 1024UL * (unsigned long)r.w.bx / 1024UL;
            ++g_nvol;
        }
    }

    g_bios_disks = *(u8 far *)MK_FP(0x0040, 0x0075);
    g_geo_ok = FALSE;
    if (g_bios_disks > 0) {
        r.h.ah = 0x08;
        r.h.dl = 0x80;
        int86(0x13, &r, &r);
        if (!r.w.cflag) {
            g_heads = (unsigned)r.h.dh + 1;
            g_sects = (unsigned)(r.h.cl & 0x3F);
            g_cyl   = ((unsigned)(r.h.cl & 0xC0) << 2 | r.h.ch) + 1;
            g_geo_ok = TRUE;
        }
    }
}

static void probe_dos(void)
{
    union REGS r;
    const char far *bd = (const char far *)MK_FP(0xF000, 0xFFF5);
    int i;

    r.w.ax = 0x3000;
    int86(0x21, &r, &r);
    g_dos_hi = r.h.al;
    g_dos_lo = r.h.ah;

    r.w.ax = 0x3305;
    int86(0x21, &r, &r);
    g_boot = (char)('A' + r.h.dl - 1);

    for (i = 0; i < 8; ++i)            /* BIOS date lives at F000:FFF5     */
        g_bdate[i] = (bd[i] >= ' ' && bd[i] < 127) ? bd[i] : '?';
    g_bdate[8] = '\0';
    g_model = *(u8 far *)MK_FP(0xF000, 0xFFFE);
}

static void probe_ports(void)
{
    const u16 far *bda = (const u16 far *)MK_FP(0x0040, 0x0000);
    union REGS r;
    int i;

    for (i = 0; i < 4; ++i) g_com[i] = bda[i];
    for (i = 0; i < 3; ++i) g_lpt[i] = bda[4 + i];

    r.w.ax = 0;
    int86(0x33, &r, &r);
    g_mouse = (r.w.ax == 0xFFFF) ? TRUE : FALSE;
    g_mv_hi = g_mv_lo = 0;
    if (g_mouse) {
        r.w.ax = 0x24;
        int86(0x33, &r, &r);
        g_mv_hi = r.h.bh;
        g_mv_lo = r.h.bl;
    }
}

/* Spin loops completed in `nticks` BIOS ticks - the CPU speed index.
   The loop body is fixed, so the count is a machine constant. */
static unsigned long spin(int nticks)
{
    unsigned long t0, n = 0;
    t0 = sys_ticks();
    while (sys_ticks() == t0) ;        /* align to a tick edge             */
    t0 = sys_ticks();
    while (sys_ticks() - t0 < (unsigned long)nticks)
        ++n;
    return n / (unsigned long)nticks;
}

static void probe_all(void)
{
    probe_cpu();
    probe_mem();
    probe_video();
    probe_disks();
    probe_dos();
    probe_ports();
    /* The CPU speed index is a timed measurement, so it belongs here with
       every other probe.  It used to run from page_cpu() - i.e. from inside
       a DRAW call - where its 110 ms busy-wait froze the desktop, the mouse
       and the keyboard the first time that page was painted. */
    g_loops = spin(2);
    g_probed = TRUE;
}

/* ---- the benchmark ----------------------------------------------------- */

/* Runs a 386SX/16 completes in the 4-tick window for each sub-test - the
   empirical anchors that turn a raw run count into an "x times a 386SX/16"
   index.  Approximate by design: the datum machine is itself a rough peg. */
#define SCALE_ALU    260UL
#define SCALE_MUL     95UL
#define SCALE_MCOPY  210UL
#define SCALE_MREAD  330UL
#define SCALE_VFILL   45UL
#define SCALE_VLINE   30UL
#define SCALE_SPIN 11600UL             /* spin loops/tick on a 386SX/16    */

/* Spin to the next tick edge, then reset the timer origin.  Every sub-test
   opens with this so each measures the SAME 4-tick wall-clock window. */
static unsigned long bench_align(void)
{
    unsigned long t0 = sys_ticks();
    while (sys_ticks() == t0) ;
    return sys_ticks();
}

/* The video sub-tests paint in ABSOLUTE screen coordinates, so they blast
   the whole top of the desktop - every other window included - and leave
   the back buffer holding colour bands.  A repaint follows the click, but
   only for windows already marked dirty, so anything else stayed corrupt.
   Raise this and let the shell recompose the entire scene. */
static bool_t g_scene_trashed = FALSE;

bool_t oracle_poll_damage(void)
{
    bool_t d = g_scene_trashed;
    g_scene_trashed = FALSE;
    return d;
}

static void bench_run(void)
{
    unsigned long t0, n;
    unsigned sa = 0, sb = 0;

    /* 1. ALU: a 32-bit LCG mix, 250 rounds per run. */
    n = 0; t0 = bench_align();
    while (sys_ticks() - t0 < 4UL) {
        unsigned long acc = n; int i;
        for (i = 0; i < 250; ++i)
            acc = acc * 69069UL + 1UL;
        g_sink = acc; ++n;
    }
    g_b_alu = n;

    /* 2. MUL/shift/xor: a heavier integer hash, 200 rounds per run. */
    n = 0; t0 = bench_align();
    while (sys_ticks() - t0 < 4UL) {
        unsigned long acc = n; int i;
        for (i = 0; i < 200; ++i) {
            acc = acc * 2654435761UL;
            acc ^= acc >> 13;
            acc += 40503UL;
        }
        g_sink = acc; ++n;
    }
    g_b_mul = n;

    /* 3. MEM copy: 32 KB dword rep-movsd between two DOS blocks. */
    g_b_mcopy = g_b_mread = 0;
    if (_dos_allocmem(2048, &sa) == 0) {
        if (_dos_allocmem(2048, &sb) == 0) {
            n = 0; t0 = bench_align();
            while (sys_ticks() - t0 < 4UL) {
                orc_copy32(sb, 0, sa, 0, 8192);
                ++n;
            }
            g_b_mcopy = n;
            _dos_freemem(sb);
        }
        /* 4. MEM read: sum 8192 far dwords (32 KB read bandwidth). */
        {
            unsigned long far *p = (unsigned long far *)MK_FP(sa, 0);
            n = 0; t0 = bench_align();
            while (sys_ticks() - t0 < 4UL) {
                unsigned long s = 0; unsigned k;
                for (k = 0; k < 8192U; ++k) s += p[k];
                g_sink = s; ++n;
            }
            g_b_mread = n;
        }
        _dos_freemem(sa);
    }

    /* 5. VIDEO fill: full-width solid fills into the back buffer. */
    n = 0; t0 = bench_align();
    while (sys_ticks() - t0 < 4UL) {
        vid_fillrect(0, 0, SCREEN_W, 100, (u8)(n & 0x0F));
        ++n;
    }
    g_b_vfill = n;

    /* 6. VIDEO lines: 100 horizontal spans (the hline raster path). */
    n = 0; t0 = bench_align();
    while (sys_ticks() - t0 < 4UL) {
        int yy;
        for (yy = 0; yy < 100; ++yy)
            vid_hline(0, yy, SCREEN_W, (u8)((n + yy) & 0x0F));
        ++n;
    }
    g_b_vline = n;

    g_bench_done    = TRUE;
    g_scene_trashed = TRUE;   /* the video tests scribbled over everything */
}

/* Fixed-point index (x100) of a raw run count against its 386SX/16 anchor:
   a 386SX/16 scores 100 ("1.00x"). */
static unsigned long bench_index(unsigned long units, unsigned long scale)
{
    return (scale == 0UL) ? 0UL : (units * 100UL / scale);
}

/* The composite Oracle Score: the mean of the six sub-test indices. */
static unsigned long bench_score(void)
{
    unsigned long s = 0;
    s += bench_index(g_b_alu,   SCALE_ALU);
    s += bench_index(g_b_mul,   SCALE_MUL);
    s += bench_index(g_b_mcopy, SCALE_MCOPY);
    s += bench_index(g_b_mread, SCALE_MREAD);
    s += bench_index(g_b_vfill, SCALE_VFILL);
    s += bench_index(g_b_vline, SCALE_VLINE);
    return s / 6UL;
}

/* A machine-class verdict from the composite (386SX/16 == 100). */
static const char *bench_rating(unsigned long score)
{
    if (score == 0)     return "not yet measured";
    if (score < 30)     return "XT / slow-286 class";
    if (score < 80)     return "286 / early-386 class";
    if (score < 170)    return "386-class throughput";
    if (score < 500)    return "486-class throughput";
    if (score < 1800)   return "Pentium-class throughput";
    return "modern silicon (emulated?)";
}

/* ---- drawing ----------------------------------------------------------- */

static int g_ry;                       /* running row y for row()          */

static void row(int x, const char *lab, const char *val)
{
    font_draw(x, g_ry, lab, C_BLACK);
    font_draw(x + font_adv() * 10, g_ry, val, C_TITLE);
    g_ry += font_h() + 2;
}

static void header(const Rect *cl, int x, const char *title)
{
    g_ry = cl->y + 4;
    font_draw(x, g_ry, title, C_TITLE);
    g_ry += font_h() + 2;
    vid_fillrect(x, g_ry, cl->x + cl->w - x - 4, 1, C_SHADOW);
    g_ry += 4;
}

/* Human-readable size.  Callers pass buffers of four different sizes and
   there was no length parameter: a multi-gigabyte volume renders
   "4194304.9 MB" = 13 bytes into the 14-byte buffers at page_disk(), one
   digit short of an overflow.  Cap the magnitude instead of trusting the
   caller - past a terabyte the exact figure is not what the reader wants
   anyway, and every buffer in this file then has room to spare. */
static void kb_str(char *dst, unsigned long kb)
{
    if (kb >= 1048576UL)                            /* >= 1 GB              */
        sprintf(dst, "%lu.%lu GB", kb / 1048576UL,
                (kb % 1048576UL) * 10UL / 1048576UL);
    else if (kb >= 4096UL)
        sprintf(dst, "%lu.%lu MB", kb / 1024UL, (kb % 1024UL) * 10UL / 1024UL);
    else
        sprintf(dst, "%lu KB", kb);
}

static void page_overview(const Rect *cl, int x)
{
    char b[36], b2[16];

    header(cl, x, "The System Oracle");
    row(x, "Machine", "DOS-compatible PC");
    row(x, "CPU", g_cpu_name);
    row(x, "FPU", g_fpu ? "present" : "absent");
    kb_str(b2, (unsigned long)g_conv_kb + g_ext_kb);
    row(x, "RAM", b2);
    sprintf(b, "VGA, BIOS mode %02Xh", g_vmode);
    row(x, "Video", b);
    sprintf(b, "%d volume(s)", g_nvol);
    row(x, "Disks", b);
    sprintf(b, "DOS %u.%02u", g_dos_hi, g_dos_lo);
    row(x, "System", b);
    row(x, "Mouse", g_mouse ? "driver loaded" : "none");
    if (sb_present() && opl_present()) sprintf(b, "%s +OPL FM", sb_name());
    else if (sb_present())             sprintf(b, "%s (%03X)", sb_name(), sb_base());
    else if (opl_present())            strcpy(b, "AdLib OPL FM (388)");
    else                               strcpy(b, "PC speaker only");
    row(x, "Sound", b);
    if (g_bench_done) {
        unsigned long sc = bench_score();
        sprintf(b, "%lu.%02lux a 386SX/16", sc / 100UL, sc % 100UL);
        row(x, "Bench", b);
    }
    g_ry += 3;
    font_draw(x, g_ry, "Know thyself - and thy PC.", C_DKGRAY);
}

static void page_cpu(const Rect *cl, int x)
{
    char b[34];

    header(cl, x, "Processor");
    row(x, "Class", g_cpu_name);
    row(x, "CPUID", g_cpuid ? "supported" : "not supported");
    if (g_cpuid) {
        row(x, "Vendor", g_vendor);
        sprintf(b, "%d", g_family);
        row(x, "Family", b);
    }
    row(x, "FPU", g_fpu ? "present (FNSTSW ok)" : "absent");
    sprintf(b, "%lu kloops/tick", g_loops / 1000UL);
    row(x, "Spin rate", b);
    sprintf(b, "%lu.%lux a 386SX/16", g_loops * 10UL / SCALE_SPIN / 10UL,
            (g_loops * 10UL / SCALE_SPIN) % 10UL);
    row(x, "Index", b);
    g_ry += 3;
    font_draw(x, g_ry, "Measured against the BIOS", C_DKGRAY);
    g_ry += font_h() + 1;
    font_draw(x, g_ry, "tick - no trust, only proof.", C_DKGRAY);
}

static void page_mem(const Rect *cl, int x)
{
    char b[24];

    header(cl, x, "Memory");
    kb_str(b, (unsigned long)g_conv_kb);
    row(x, "Convent.", b);
    kb_str(b, g_ext_kb);
    row(x, "Extended", b);
    row(x, "XMS drv", g_xms ? "installed" : "absent");
    if (g_ems) {
        sprintf(b, "%u/%u pages free", g_ems_free, g_ems_total);
        row(x, "EMS", b);
    } else {
        row(x, "EMS", "absent");
    }
    kb_str(b, (unsigned long)g_dos_para / 64UL);
    row(x, "DOS free", b);
    kb_str(b, (unsigned long)g_conv_kb + g_ext_kb);
    row(x, "Total", b);
}

static void page_video(const Rect *cl, int x)
{
    char b[30];

    header(cl, x, "Video");
    row(x, "Adapter", "VGA compatible");
    sprintf(b, "%02Xh (%s)", g_vmode,
            (g_vmode == 0x13) ? "320x200x256" :
            (g_vmode == 0x12) ? "640x480x16"  : "other");
    row(x, "Mode", b);
    if (g_vesa) {
        sprintf(b, "%d.%d", g_vesa_hi, g_vesa_lo);
        row(x, "VESA", b);
        kb_str(b, g_vesa_kb);
        row(x, "Vid RAM", b);
    } else {
        row(x, "VESA", "not detected");
    }
    row(x, "Back buf", "system RAM (double-buf)");
    row(x, "Renderer", "386 dword blitter");
}

static void page_disks(const Rect *cl, int x)
{
    char b[34];
    int i;

    header(cl, x, "Disks");
    for (i = 0; i < g_nvol; ++i) {
        char lab[4];
        char t[14], f[14];
        kb_str(t, g_vol[i].tot_kb);
        kb_str(f, g_vol[i].free_kb);
        sprintf(b, "%s (%s free)", t, f);
        lab[0] = g_vol[i].letter; lab[1] = ':'; lab[2] = '\0';
        row(x, lab, b);
    }
    if (g_nvol == 0)
        row(x, "Volumes", "none responding");
    sprintf(b, "%d", g_bios_disks);
    row(x, "BIOS hd", b);
    if (g_geo_ok) {
        sprintf(b, "%u cyl %u hd %u sec", g_cyl, g_heads, g_sects);
        row(x, "Geometry", b);
    }
}

static void page_dos(const Rect *cl, int x)
{
    char b[30];
    const char *cs = getenv("COMSPEC");

    header(cl, x, "DOS and BIOS");
    sprintf(b, "%u.%02u", g_dos_hi, g_dos_lo);
    row(x, "DOS ver", b);
    b[0] = g_boot; b[1] = ':'; b[2] = '\0';
    row(x, "Boot drv", b);
    row(x, "Shell", (cs != NULL) ? cs : "?");
    row(x, "BIOS date", g_bdate);
    sprintf(b, "%02Xh", g_model);
    row(x, "Model", b);
    row(x, "Host", CAST_NAME " " CAST_VERSION);
}

static void page_ports(const Rect *cl, int x)
{
    char b[26];
    int i;

    header(cl, x, "Ports and Input");
    for (i = 0; i < 4; ++i) {
        char lab[6];
        sprintf(lab, "COM%d", i + 1);
        if (g_com[i]) sprintf(b, "%03Xh", g_com[i]);
        else          strcpy(b, "-");
        row(x, lab, b);
    }
    for (i = 0; i < 3; ++i) {
        char lab[6];
        sprintf(lab, "LPT%d", i + 1);
        if (g_lpt[i]) sprintf(b, "%03Xh", g_lpt[i]);
        else          strcpy(b, "-");
        row(x, lab, b);
    }
    if (g_mouse) sprintf(b, "INT 33h v%d.%02d", g_mv_hi, g_mv_lo);
    else         strcpy(b, "no driver");
    row(x, "Mouse", b);
}

/* ---- the benchmark page ------------------------------------------------ */
static Rect g_run_btn;

/* One result row: the test name, its x-index and a proportional bar. */
static void brow(const Rect *cl, int x, const char *name,
                 unsigned long units, unsigned long scale)
{
    int rx = x + font_adv() * 9;
    int bx = x + font_adv() * 18;
    int bw = cl->x + cl->w - bx - 6;
    unsigned long ix = bench_index(units, scale);
    char v[14];

    font_draw(x, g_ry, name, C_BLACK);
    if (units == 0) {
        font_draw(rx, g_ry, "-", C_DKGRAY);
    } else {
        /* Clamp in 32 BITS, then narrow.  A fast machine (or an emulator)
           reaches a five- or six-digit index, and bw*ix/300 then truncated
           to a garbage - frequently negative - int: the "w > bw" test could
           not see it, "w < 1" forced w = 1, and a machine that should have
           shown a fully-clipped red bar showed a 1-pixel green sliver. */
        unsigned long wl = (unsigned long)bw * ix / 300UL;  /* 3.00x fills  */
        bool_t clip = FALSE;
        int w;
        if (wl > (unsigned long)bw) { wl = (unsigned long)bw; clip = TRUE; }
        w = (int)wl;
        if (w < 1)  w = 1;
        if (ix / 100UL >= 10000UL)
            strcpy(v, ">9999x");
        else
            sprintf(v, "%lu.%02lux", ix / 100UL, ix % 100UL);
        font_draw(rx, g_ry, v, C_TITLE);
        vid_rect    (bx, g_ry + 1, bw, font_h() - 3, C_SHADOW);
        vid_fillrect(bx + 1, g_ry + 2, (w > 2 ? w - 2 : 1), font_h() - 5,
                     clip ? C_RED : C_GREEN);
    }
    g_ry += font_h() + 1;
}

static void page_bench(const Rect *cl, int x)
{
    unsigned long score = g_bench_done ? bench_score() : 0UL;
    char b[20];

    header(cl, x, "Benchmark  (386SX/16 = 1.00x)");

    font_draw(x, g_ry, "TEST", C_DKGRAY);
    font_draw(x + font_adv() * 9,  g_ry, "INDEX", C_DKGRAY);
    font_draw(x + font_adv() * 18, g_ry, "GRAPH", C_DKGRAY);
    g_ry += font_h() + 1;

    brow(cl, x, "ALU  i32", g_b_alu,   SCALE_ALU);
    brow(cl, x, "HASH mul", g_b_mul,   SCALE_MUL);
    brow(cl, x, "MEM copy", g_b_mcopy, SCALE_MCOPY);
    brow(cl, x, "MEM read", g_b_mread, SCALE_MREAD);
    brow(cl, x, "VID fill", g_b_vfill, SCALE_VFILL);
    brow(cl, x, "VID line", g_b_vline, SCALE_VLINE);

    vid_fillrect(x, g_ry, cl->x + cl->w - x - 6, 1, C_SHADOW);
    g_ry += 3;

    if (g_bench_done) {
        sprintf(b, "%lu.%02lux", score / 100UL, score % 100UL);
        font_draw(x, g_ry, "ORACLE SCORE", C_TITLE);
        font_draw(x + font_adv() * 13, g_ry, b, C_RED);
        g_ry += font_h() + 1;
        font_draw(x, g_ry, bench_rating(score), C_BLACK);
        g_ry += font_h() + 2;
    } else {
        font_draw(x, g_ry, "Six tests, no mercy.", C_DKGRAY);
        g_ry += font_h() + 2;
    }

    rect_set(&g_run_btn, x, g_ry, font_adv() * 15 + 10, font_h() + 5);
    ui_button(&g_run_btn, g_bench_done ? "Run again" : "Run benchmark", FALSE);
}

void oracle_open(void)
{
    g_page = PG_OVER;
    if (!g_probed)
        probe_all();
}

void oracle_draw(const Rect *cl)
{
    int sw = font_adv() * 9 + 8;       /* sidebar width                    */
    int i, x;

    /* Sidebar: the category list. */
    ui_sink(cl->x + 2, cl->y + 2, sw, cl->h - 4);
    for (i = 0; i < PG_N; ++i) {
        int ry = cl->y + 4 + i * (font_h() + 4);
        if (i == g_page) {
            vid_fillrect(cl->x + 3, ry - 1, sw - 2, font_h() + 3, C_BLUE);
            font_draw(cl->x + 6, ry + 1, PAGES[i], C_WHITE);
        } else {
            font_draw(cl->x + 6, ry + 1, PAGES[i], C_BLACK);
        }
    }

    x = cl->x + sw + 8;
    switch (g_page) {
    case PG_OVER:  page_overview(cl, x); break;
    case PG_CPU:   page_cpu(cl, x);      break;
    case PG_MEM:   page_mem(cl, x);      break;
    case PG_VIDEO: page_video(cl, x);    break;
    case PG_DISK:  page_disks(cl, x);    break;
    case PG_DOS:   page_dos(cl, x);      break;
    case PG_PORTS: page_ports(cl, x);    break;
    case PG_BENCH: page_bench(cl, x);    break;
    }
}

bool_t oracle_click(const Rect *cl, int mx, int my)
{
    int sw = font_adv() * 9 + 8;
    int i;

    /* Sidebar rows. */
    if (mx < cl->x + sw + 2) {
        for (i = 0; i < PG_N; ++i) {
            int ry = cl->y + 4 + i * (font_h() + 4);
            if (my >= ry - 1 && my < ry + font_h() + 3 && i != g_page) {
                g_page = i;
                return TRUE;
            }
        }
        return FALSE;
    }
    if (g_page == PG_BENCH && rect_contains(&g_run_btn, mx, my)) {
        bench_run();
        return TRUE;
    }
    return FALSE;
}

bool_t oracle_key(int key)
{
    if (key == KEY_UP && g_page > 0) {
        --g_page;
        return TRUE;
    }
    if (key == KEY_DOWN && g_page < PG_N - 1) {
        ++g_page;
        return TRUE;
    }
    if ((key == KEY_ENTER || key == ' ') && g_page == PG_BENCH) {
        bench_run();
        return TRUE;
    }
    return FALSE;
}
