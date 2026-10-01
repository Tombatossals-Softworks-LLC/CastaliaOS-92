/* ======================================================================
 * system.c - Machine / DOS information for the System Panel
 * ====================================================================== */
#include <i86.h>
#include <dos.h>
#include <conio.h>     /* inp / outp (the CMOS memory count)             */
#include <stdio.h>
#include <string.h>
#include <direct.h>    /* getcwd (the home-directory capture)            */
#include "castalia.h"
#include "system.h"
#include "video.h"

/* ----------------------------------------------------------------------
 * The home directory.  The Disk Cabinet chdir()s around the machine as
 * the user browses, so anything that opens a data file by a bare name
 * (AGENDA.TXT, CASTALIA.HI, CASTALIA.INI, the gallery) would read or
 * write it wherever the user happens to be standing.  main() captures
 * the start-up directory once; sys_home_path() anchors names to it.
 * -------------------------------------------------------------------- */
static char g_home[68] = "";           /* "C:\CASTALIA"; DOS paths <= 66  */

/* Home is the folder CASTALIA.EXE was loaded from, NOT the current one.
   INSTALL puts Castalia on the PATH, and typed from C:\ the old getcwd()
   capture made C:\ "home": the INI came back as defaults and every save
   (INI, high scores, recent list, CASTRUN.BAT) landed in the root.  DOS
   3+ hands every program its full load path, which the C library passes
   on as argv[0]; the current directory is only the fallback. */
void sys_capture_home(const char *argv0)
{
    int i, cut = -1;
    if (argv0 != NULL && argv0[0] != '\0' && argv0[1] == ':') {
        for (i = 0; argv0[i] != '\0'; ++i)
            if (argv0[i] == '\\' || argv0[i] == '/')
                cut = i;
        if (cut >= 2 && cut < (int)sizeof(g_home) - 1) {
            for (i = 0; i < cut; ++i)
                g_home[i] = argv0[i];
            if (cut == 2)              /* "C:\CASTALIA.EXE" -> "C:\"     */
                g_home[i++] = '\\';
            g_home[i] = '\0';
            return;
        }
    }
    if (getcwd(g_home, (int)sizeof(g_home)) == NULL)
        g_home[0] = '\0';              /* fall back to relative names     */
}

const char *sys_home(void)
{
    return g_home;
}

void sys_home_path(char *out, int cap, const char *name)
{
    int i = 0, j = 0;
    while (g_home[i] != '\0' && i < cap - 2) { out[i] = g_home[i]; ++i; }
    if (i > 0 && out[i - 1] != '\\' && i < cap - 2)
        out[i++] = '\\';
    while (name[j] != '\0' && i < cap - 1)
        out[i++] = name[j++];
    out[i] = '\0';
}

/* ----------------------------------------------------------------------
 * CPU family probe.  The classic EFLAGS dance: if the AC bit (18) cannot
 * be toggled it is a 386; if it can, try the ID bit (21) - when that
 * toggles, CPUID is available and reports the family (4/5/6...), otherwise
 * it is an early 486 without CPUID.  CPUID never executes on a 386 because
 * the AC test returns first, so the .586 block is safe on 386-class iron.
 * Returns 3, 4, 5, 6 ...  (the leading digit of 80x86).
 * -------------------------------------------------------------------- */
extern int cpu_detect(void);
#pragma aux cpu_detect =        \
    ".586"                       \
    "pushfd"                     \
    "pop  eax"                   \
    "mov  ecx, eax"              \
    "xor  eax, 40000h"           \
    "push eax"                   \
    "popfd"                      \
    "pushfd"                     \
    "pop  eax"                   \
    "xor  eax, ecx"              \
    "push ecx"                   \
    "popfd"                      \
    "test eax, 40000h"           \
    "jnz  ac_ok"                 \
    "mov  ax, 3"                 \
    "jmp  cpu_done"              \
"ac_ok:"                         \
    "pushfd"                     \
    "pop  eax"                   \
    "mov  ecx, eax"              \
    "xor  eax, 200000h"          \
    "push eax"                   \
    "popfd"                      \
    "pushfd"                     \
    "pop  eax"                   \
    "xor  eax, ecx"              \
    "push ecx"                   \
    "popfd"                      \
    "test eax, 200000h"          \
    "jz   no_cpuid"              \
    "mov  eax, 1"                \
    "cpuid"                      \
    "shr  eax, 8"                \
    "and  eax, 0Fh"              \
    "jmp  cpu_done"              \
"no_cpuid:"                      \
    "mov  ax, 4"                 \
"cpu_done:"                      \
    value [ax]                   \
    modify [ax bx cx dx];

/* ----------------------------------------------------------------------
 * DOS critical-error (INT 24h) handler.  Returning _HARDERR_FAIL makes
 * the failing DOS call return an error to us rather than hanging the
 * graphical shell with an "Abort, Retry, Fail?" prompt we cannot show.
 * -------------------------------------------------------------------- */
static int __far crit_handler(unsigned deverr, unsigned errcode,
                              unsigned __far *devhdr)
{
    (void)deverr;
    (void)errcode;
    (void)devhdr;
    return _HARDERR_FAIL;
}

void crit_error_install(void)
{
    _harderr(crit_handler);
}

/* ----------------------------------------------------------------------
 * Ctrl+C / Ctrl+Break.  DOS looks for both inside every console call -
 * the kbhit() and getch() the event loop makes on every pass included -
 * and its default INT 23h handler simply terminates the program: no
 * video_shutdown, the screen left in Mode 13h (black, after a fade), the
 * speaker gate open, buffered saves never flushed.  A handler that just
 * IRETs tells DOS to resume the interrupted call as if nothing happened.
 * INT 23h is one of the vectors DOS restores from the PSP when a program
 * ends, so there is nothing to unhook on any exit path.
 * -------------------------------------------------------------------- */
static void __interrupt __far ctrlc_isr(void)
{
}

void ctrl_break_install(void)
{
    _dos_setvect(0x23, ctrlc_isr);
}

#define SYS_MAX_LINES 16
#define SYS_LINE_LEN  40

static char   g_lines[SYS_MAX_LINES][SYS_LINE_LEN];
static int    g_count = 0;
static bool_t g_mouse = FALSE;

void system_set_mouse(bool_t present)
{
    g_mouse = present;
}

bool_t system_has_mouse(void)
{
    return g_mouse;
}

/* ---- raw probes ------------------------------------------------------ */

static void dos_version(unsigned *major, unsigned *minor)
{
    union REGS r;
    r.x.ax = 0x3000;               /* INT 21h AH=30h get DOS version      */
    int86(0x21, &r, &r);
    *major = r.h.al;
    *minor = r.h.ah;
}

static unsigned conventional_kb(void)
{
    union REGS r;
    int86(0x12, &r, &r);           /* INT 12h -> AX = KB of base memory   */
    return r.x.ax;
}

static unsigned free_conventional_kb(void)
{
    union REGS r;
    r.x.ax = 0x4800;               /* INT 21h AH=48h allocate             */
    r.x.bx = 0xFFFF;               /* ask for an impossible amount        */
    int86(0x21, &r, &r);           /* fails; BX = largest free paragraphs */
    return (unsigned)(r.x.bx / 64);/* 64 paragraphs per KB                */
}

/* ----------------------------------------------------------------------
 * Extended (above-1 MB) memory.
 *
 * The naive probe is INT 15h AH=88h, but it reports 0 the moment an XMS
 * manager (HIMEM.SYS, or the emulator's built-in server) is installed -
 * which is the usual configuration on a real 386 and the only one DOSBox
 * offers.  Trusting AH=88h alone would make Castalia's 1 MB memory gate
 * reject exactly the well-equipped machines it is meant to welcome.
 *
 * So we ask the XMS manager directly (INT 2Fh) and fall back to the BIOS
 * probe only when no manager is present.
 * -------------------------------------------------------------------- */
static void (__far *g_xms_entry)(void) = 0;
static bool_t g_xms_checked = FALSE;

static void xms_init(void)
{
    union REGS  r;
    struct SREGS s;
    g_xms_checked = TRUE;
    r.x.ax = 0x4300;               /* XMS installation check               */
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80)
        return;                    /* no XMS manager                       */
    segread(&s);
    r.x.ax = 0x4310;               /* get driver entry point -> ES:BX      */
    int86x(0x2F, &r, &r, &s);
    g_xms_entry = (void (__far *)(void))MK_FP(s.es, r.x.bx);
}

/* Total free extended memory in KB via XMS function 08h (DX = total free).
   Referencing the STATIC entry pointer keeps the far call's address
   operand DS-relative, so it is unaffected by the pushes around it. */
static unsigned xms_total_kb(void)
{
    unsigned dx_val = 0;
    if (g_xms_entry == 0)
        return 0;
    _asm {
        push bx
        push cx
        mov  ah, 08h
        call dword ptr [g_xms_entry]
        mov  dx_val, dx            /* DX = total free KB (AX = largest)    */
        pop  cx
        pop  bx
    }
    return dx_val;
}

static unsigned extended_kb(void)
{
    union REGS r;
    if (!g_xms_checked)
        xms_init();
    if (g_xms_entry != 0) {        /* XMS manager present: ask it          */
        unsigned x = xms_total_kb();
        if (x != 0)
            return x;
    }
    r.x.ax = 0x8800;               /* INT 15h AH=88h: bare-BIOS fallback   */
    int86(0x15, &r, &r);
    if (r.x.cflag)                 /* not supported -> report 0            */
        return 0;
    return r.x.ax;                 /* AX = KB above 1 MB                   */
}

/* Extended memory the BIOS counted at POST, from CMOS 30h/31h.  The two
   probes above both answer a different question once HIMEM is loaded:
   AH=88h says 0, and XMS function 08h says how much is FREE.  Adding the
   free figure to base memory is what made a 4 MB 386SX report "Total RAM
   3658 KB" on real hardware.  Interrupts off around the index/data pair,
   since the BIOS RTC handler also drives port 70h. */
static unsigned cmos_ext_kb(void)
{
    unsigned lo, hi;
    _disable();
    outp(0x70, 0x30);
    lo = inp(0x71);
    outp(0x70, 0x31);
    hi = inp(0x71);
    _enable();
    return (hi << 8) | lo;
}

static unsigned installed_ext_kb(void)
{
    unsigned c = cmos_ext_kb(), x = extended_kb();
    return (c >= x) ? c : x;           /* a blank CMOS falls back          */
}

unsigned long system_total_ram_kb(void)
{
    /* (unsigned long): with lots of extended memory the plain 16-bit sum
       wraps at 64 MB and the splash/panel would report nonsense. */
    return (unsigned long)conventional_kb() + (unsigned long)installed_ext_kb();
}

/* ----------------------------------------------------------------------
 * Safe saves.  fopen(path, "w") truncates the real file before a byte of
 * the new one exists, so a full disk, a pulled floppy or a power cut in
 * the middle of a save destroyed the only copy.  Write to a temp name in
 * the same folder instead, push it through to the disk, and only then
 * swap it in.  The swap is two directory operations, not a whole write.
 * -------------------------------------------------------------------- */
void sys_temp_name(char *out, int cap, const char *path)
{
    int n = (int)strlen(path), dot = -1, i;
    for (i = 0; i < n; ++i) {
        if (path[i] == '.')
            dot = i;
        else if (path[i] == '\\' || path[i] == '/' || path[i] == ':')
            dot = -1;                  /* a dot in a folder name is not it */
    }
    if (dot < 0)
        dot = n;
    if (dot > cap - 5)
        dot = cap - 5;
    for (i = 0; i < dot; ++i)
        out[i] = path[i];
    strcpy(out + i, ".TM$");
}

bool_t sys_commit_file(FILE *f, const char *tmp, const char *path)
{
    bool_t bad = ferror(f) ? TRUE : FALSE;
    if (fflush(f) != 0) {
        bad = TRUE;
    } else {
        /* INT 21h AH=68h (DOS 3.3+): commit the handle, so the data is on
           the disk - not in a write-behind cache - before the old file
           goes.  Older DOS just returns an error; nothing is lost. */
        union REGS r;
        r.h.ah = 0x68;
        r.x.bx = (unsigned)fileno(f);
        int86(0x21, &r, &r);
    }
    if (fclose(f) != 0)
        bad = TRUE;
    if (bad) {
        remove(tmp);                   /* the original was never touched   */
        return FALSE;
    }
    remove(path);                      /* rename() will not replace        */
    return (rename(tmp, path) == 0) ? TRUE : FALSE;
}

/* ---- BIOS tick counter & CPU idle ------------------------------------ */

unsigned long sys_ticks(void)
{
    volatile u16 far *bda = (volatile u16 far *)MK_FP(0x40, 0x6C);
    u16 hi, lo;
    do {                            /* re-read if the ISR ticked mid-read  */
        hi = bda[1];
        lo = bda[0];
    } while (hi != bda[1]);
    return ((unsigned long)hi << 16) | lo;
}

/* sys_idle() is an in-line intrinsic; see the pragma in system.h. */

/* ----------------------------------------------------------------------
 * Sample pacing off PIT channel 0 (left exactly as the BIOS set it).
 * Two things the old per-module pit_wait() got wrong:
 *   - The BIOS runs channel 0 in mode 3 (square wave), where the counter
 *     drops by TWO per input clock.  Counting latched downticks as clocks
 *     waited half of every sample period: the PC-speaker and Covox WAV
 *     paths played at double speed, an octave up.  The mode is taken
 *     from the counter itself - mode 3 with the BIOS's even divisor only
 *     ever shows even values, mode 2 shows odd ones too - which works on
 *     an 8253 without the 8254 read-back command.
 *   - Each wait was measured from its own start, so the port write, the
 *     far read and the keyboard poll between samples were added to every
 *     period - on a 386SX at 11-22 kHz that overhead is the size of the
 *     period itself.  The deadline is now absolute from sys_pit_start().
 * -------------------------------------------------------------------- */
static unsigned      g_pit_step = 0;   /* counter decrements per clock     */
static unsigned      g_pit_prev = 0;
static unsigned long g_pit_el   = 0;   /* input clocks since start         */

static unsigned pit_latch(void)
{
    unsigned lo, hi;
    outp(0x43, 0x00);                  /* latch channel 0                  */
    lo = inp(0x40);
    hi = inp(0x40);
    return (hi << 8) | lo;
}

void sys_pit_start(void)
{
    if (g_pit_step == 0) {
        int i;
        g_pit_step = 2;
        for (i = 0; i < 32; ++i)
            if (pit_latch() & 1)
                g_pit_step = 1;
    }
    g_pit_prev = pit_latch();
    g_pit_el   = 0;
}

void sys_pit_until(unsigned long clocks)
{
    while (g_pit_el < clocks) {
        unsigned now = pit_latch();
        g_pit_el  += (unsigned)(g_pit_prev - now) / g_pit_step;
        g_pit_prev = now;
    }
}

/* ----------------------------------------------------------------------
 * Coprocessor probe: Intel's FNINIT / FNSTSW sequence, not INT 11h.  The
 * equipment-word bit is only what the BIOS setup screen was told, and the
 * Inspector printed it as "80x87 ok" with a green LED.  The no-wait forms
 * never stall on an absent chip: with no coprocessor nothing is stored
 * and AX keeps its 5A5Ah marker (or reads floating-bus FFh); a live one
 * has just been reset, so its status word is 0.  FNSTSW AX is a 287/387
 * form, which is every coprocessor a 386 board can carry.
 * If the MSW's EM bit is set, ESC opcodes trap to INT 7 instead, which a
 * real-mode BIOS does not service - and EM means "no usable FPU" anyway.
 * -------------------------------------------------------------------- */
extern unsigned read_msw(void);
#pragma aux read_msw = \
    ".386"             \
    "smsw ax"          \
    value [ax];

extern unsigned fpu_probe_sw(void);
#pragma aux fpu_probe_sw = \
    ".387"                 \
    "mov  ax, 5A5Ah"       \
    "fninit"               \
    "fnstsw ax"            \
    value [ax]             \
    modify [ax];

static bool_t coprocessor_present(void)
{
    if (read_msw() & 0x0004)       /* EM: emulate, i.e. no FPU in use     */
        return FALSE;
    return ((fpu_probe_sw() & 0x00FF) == 0) ? TRUE : FALSE;
}

/* ---- public probes (for the System Inspector) ----------------------- */
unsigned system_conventional_kb(void) { return conventional_kb(); }
unsigned system_extended_kb(void)     { return extended_kb(); }
unsigned system_ext_installed_kb(void) { return installed_ext_kb(); }
unsigned system_free_conv_kb(void)    { return free_conventional_kb(); }
bool_t   system_fpu_present(void)     { return coprocessor_present(); }

/* Current-drive free & total space in KB (INT 21h AH=36h). */
void system_disk_kb(unsigned long *freek, unsigned long *totalk)
{
    union REGS r;
    unsigned long bpc;
    r.h.ah = 0x36; r.h.dl = 0;     /* 0 = default (current) drive          */
    int86(0x21, &r, &r);
    if (r.x.ax == 0xFFFF) { *freek = 0; *totalk = 0; return; }
    bpc = (unsigned long)r.x.ax * (unsigned long)r.x.cx;   /* bytes/cluster */
    *freek  = bpc * (unsigned long)r.x.bx / 1024UL;
    *totalk = bpc * (unsigned long)r.x.dx / 1024UL;
}

/* The detected CPU as a bare name (80x86 numbering, no trademarks). */
const char *system_cpu_name(void)
{
    switch (cpu_detect()) {
    case 3:  return "80386";
    case 4:  return "80486";
    case 5:  return "80586-class";
    case 6:  return "80686-class";
    default: return "80x86 (modern)";
    }
}

/* ---- line builder ---------------------------------------------------- */

static void add(const char *s)
{
    if (g_count < SYS_MAX_LINES) {
        int i = 0;
        while (s[i] != '\0' && i < SYS_LINE_LEN - 1) {
            g_lines[g_count][i] = s[i];
            ++i;
        }
        g_lines[g_count][i] = '\0';
        ++g_count;
    }
}

void system_gather(void)
{
    char tmp[SYS_LINE_LEN];
    unsigned maj, min;

    g_count = 0;

    sprintf(tmp, "%s  Version %s", CAST_NAME, CAST_VERSION);
    add(tmp);
    add(CAST_COMPANY);
    add("");

    sprintf(tmp, "Processor:    %s", system_cpu_name());
    add(tmp);

    add(coprocessor_present()
        ? "Coprocessor:  Present"
        : "Coprocessor:  Not installed");

    dos_version(&maj, &min);
    sprintf(tmp, "DOS version:  %u.%02u", maj, min);
    add(tmp);

    sprintf(tmp, "Conventional: %u KB", conventional_kb());
    add(tmp);

    sprintf(tmp, "Free memory:  %u KB", free_conventional_kb());
    add(tmp);

    sprintf(tmp, "Extended:     %u KB", extended_kb());
    add(tmp);
    sprintf(tmp, "Total RAM:    %lu KB", system_total_ram_kb());
    add(tmp);

    add("");
    add(video_is_big()
        ? "Video mode:   640x480x16 (12h)"
        : "Video mode:   320x200x256 (13h)");

    add(g_mouse ? "Mouse:        Detected (INT 33h)"
                : "Mouse:        Not found");
}

int system_line_count(void)
{
    return g_count;
}

const char *system_line(int i)
{
    if (i < 0 || i >= g_count)
        return "";
    return g_lines[i];
}
