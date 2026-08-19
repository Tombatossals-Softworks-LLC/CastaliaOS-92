/* ======================================================================
 * files.c - The Disk Cabinet (file manager) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Phase 2: the Disk Cabinet now owns its whole window client area and
 * draws a toolbar (Up / New / Ren / Copy / Move / Del), a row of drive
 * buttons, the current path, a scrolling file list and a scroll bar.
 * File operations use the modal dialogs in dialog.c.  Drive switching is
 * guarded by a cheap validity probe (INT 21h AH=36h) so selecting an
 * absent drive shows a message instead of hanging; a critical-error
 * handler (see system.c) covers not-ready media on real hardware.
 * ====================================================================== */
#include <i86.h>       /* int86 (drive validity probe)                   */
#include <dos.h>       /* _dos_findfirst / _dos_findnext / _dos_*drive   */
#include <direct.h>    /* chdir / getcwd / mkdir / rmdir                 */
#include <stdio.h>     /* fopen/fread/fwrite, rename, remove, sprintf    */
#include <string.h>
#include "files.h"
#include "video.h"
#include "font.h"
#include "ui.h"
#include "keyboard.h"
#include "dialog.h"
#include "filedlg.h"
#include "config.h"   /* CFG_PATH_LEN - the picker returns absolute paths */

/* 400, not 200.  A DOS directory holding more than two hundred entries
   is ordinary - C:\DOS, a games folder, a download drop - and the ones
   past the cap cannot be reached at all: the path row says "+more" in
   red, which is honest but not much use to someone looking for a file
   that is there.  The table is FAR (22 bytes an entry, so this costs
   4400 bytes of far data and nothing in DGROUP) and the sort is a shell
   sort, so neither memory nor time is the reason it was small. */
#define FILES_MAX 400
#define NAME_LEN  13

/* Metrics scale with the active font so the same layout serves both video
   modes (each equals its old constant at the 8px font). */
#define ROW_H (font_h() + 1)       /* list row height   (9)  */
#define TBH   (font_h() + 5)       /* toolbar height    (13) */
#define DRH   (font_h() + 4)       /* drive row height  (12) */
#define SBW   (font_h() + 3)       /* scroll bar width  (11) */
#define TB_N   7                   /* toolbar buttons (last = sort)  */
#define TB_SORT (TB_N - 1)          /* the sort-mode cycler           */

/* Selectable listing order (the sort button / F3 cycles them).  The
   directories block is always alphabetical; the mode orders the files. */
#define SORT_NAME 0
#define SORT_EXT  1
#define SORT_SIZE 2
#define SORT_DATE 3
#define DRV_N  4                   /* drive buttons (A..D)   */

/* Two views share the window: the Windows-95 "My Computer" root, which
   shows the drives as big icons in a grid, and the folder view, which is
   the classic toolbar + file list.  Opening a drive icon drops into the
   folder view; "Up" from a drive root climbs back to My Computer. */
#define FVIEW_COMPUTER 0
#define FVIEW_FILES    1
#define DRV_ALL        26          /* probe A..Z             */

typedef struct {
    char   name[NAME_LEN];
    u8     is_dir;
    u8     marked;                  /* tagged by Space, for a bulk Delete */
    u16    date, time;              /* FAT wr_date / wr_time, for sorting */
    u32    size;
} FileEntry;

/* far: 400 x 22 = 8800 bytes that are only touched while the Disk Cabinet
   is open.  In DGROUP it would be by far the largest single block in the
   scarcest segment in the program; out here it costs conventional memory
   instead, which there is more of - see the note on FILES_MAX above. */
static FileEntry far g_ent[FILES_MAX];
static bool_t g_full;                    /* the scan hit FILES_MAX        */
static int       g_count   = 0;
static int       g_sel     = 0;
static int       g_scroll  = 0;
static int       g_visible = 1;

static char g_cwd[80]    = "C:\\";
static char g_launch[80] = "";

/* My Computer view state. */
static int  g_view      = FVIEW_FILES;
static char g_drives[DRV_ALL];       /* valid drive letters, e.g. "ACD"   */
static char g_dkind[DRV_ALL];        /* DRV_* per entry, filled at scan   */
static int  g_drive_n   = 0;
static int  g_drive_sel = 0;
static int  g_drive_cols = 1;        /* grid columns from the last draw   */
static Rect g_drvcell[DRV_ALL];      /* hit rectangles from the last draw */

/* Layout rectangles (recomputed from the client rect each draw/click). */
static Rect g_tb[TB_N], g_drv[DRV_ALL], g_list, g_sb_up, g_sb_dn, g_sb_track;
/* How many drive buttons the last compute_layout could fit.  PUBLISHED,
   not recomputed: the draw and the hit test both read it, and this file
   already carries one bug from geometry being worked out twice. */
static int  g_drv_shown = 0;
static const char *TB_LBL[TB_N] =
    { "Up", "New", "Ren", "Copy", "Move", "Del", "Name" };
static const char *SORT_LBL[4] = { "Name", "Ext", "Size", "Date" };
static int g_sort = SORT_NAME;

/* Copy buffer kept out of the stack.  2 KB is plenty (bigger only trims a
   few iterations) and it leaves the shared near-data segment room to grow. */
static char g_copybuf[1024];   /* halved in the DGROUP diet */

/* ---- helpers --------------------------------------------------------- */

static bool_t path_is_root(const char *p)
{
    if (p[0] != '\0' && p[1] == ':') {
        if (p[2] == '\0')
            return TRUE;
        if ((p[2] == '\\' || p[2] == '/') && p[3] == '\0')
            return TRUE;
    }
    return FALSE;
}

/* v0.20: every file the Cabinet lists now activates - executables spawn,
   documents open in their applet, and anything else lands in Hex Peek
   (main.c routes by extension), so is_executable/is_document are gone. */

static const char *ent_name(int i); /* forward: a NEAR copy of a far name */

static bool_t selection_ok(void)
{
    return (g_sel >= 0 && g_sel < g_count &&
            strcmp(ent_name(g_sel), "..") != 0) ? TRUE : FALSE;
}

/* INT 21h AH=36h: returns FFFFh in AX for an invalid drive (no critical
   error for a simply-absent drive letter). dl: 1=A, 2=B, ... */
static bool_t drive_valid(int drive1)
{
    union REGS r;
    r.h.ah = 0x36;
    r.h.dl = (unsigned char)drive1;
    int86(0x21, &r, &r);
    return (r.x.ax != 0xFFFF) ? TRUE : FALSE;
}

/* What kind of drive is this?  A: and B: are floppies by convention and
   were the whole of the old test, so a picture of a diskette sat over the
   words "Local Disk" for every hard disk in the machine, and a real
   floppy was indistinguishable from C:.  DOS will say:

     INT 21h AX=4409h  bit 12 of DX set  -> remote (a network redirector)
     INT 21h AX=4408h  AX=0 removable, 1 fixed

   Both are DOS 3.1+ and both set CF on a drive that cannot answer, in
   which case the letter rule is still a decent guess. */
#define DRV_FLOPPY 0
#define DRV_FIXED  1
#define DRV_REMOTE 2

static int drive_kind(char letter)
{
    union REGS r;
    int bl = (letter >= 'A' && letter <= 'Z') ? (letter - 'A' + 1) : 0;

    if (bl == 0)
        return DRV_FIXED;

    r.x.ax = 0x4409;
    r.h.bl = (unsigned char)bl;
    r.x.cflag = 0;
    int86(0x21, &r, &r);
    if (!r.x.cflag && (r.x.dx & 0x1000) != 0)
        return DRV_REMOTE;

    r.x.ax = 0x4408;
    r.h.bl = (unsigned char)bl;
    r.x.cflag = 0;
    int86(0x21, &r, &r);
    if (!r.x.cflag)
        return (r.x.ax == 0) ? DRV_FLOPPY : DRV_FIXED;

    return (letter == 'A' || letter == 'B') ? DRV_FLOPPY : DRV_FIXED;
}

/* Free space on the current drive, in KB, via INT 21h AH=36h:
   AX = sectors/cluster, BX = free clusters, CX = bytes/sector. */
/* Cached: this is a DOS FAT/DPB query, and files_draw called it on EVERY
   compose - so dragging the scroll thumb fired one 18 times a second, on
   a floppy or a network redirector exactly the call the user waits for.
   Refreshed by files_rescan() and by a drive switch. */
static unsigned long g_free_kb = 0;
static bool_t        g_free_ok = FALSE;

static unsigned long drive_free_query(void)
{
    union REGS r;
    unsigned long bytes_per_cluster, free_bytes;
    r.h.ah = 0x36;
    r.h.dl = 0;                    /* 0 = default (current) drive          */
    int86(0x21, &r, &r);
    if (r.x.ax == 0xFFFF)
        return 0;
    bytes_per_cluster = (unsigned long)r.x.ax * (unsigned long)r.x.cx;
    free_bytes        = bytes_per_cluster * (unsigned long)r.x.bx;
    return free_bytes / 1024UL;
}

static unsigned long drive_free_kb(void)
{
    if (!g_free_ok) {
        g_free_kb = drive_free_query();
        g_free_ok = TRUE;
    }
    return g_free_kb;
}

/* ---- directory scanning --------------------------------------------- */

static int max_scroll(void);   /* forward: files_rescan clamps with it */

/* far: g_ent lives in FAR_DATA, and in the MEDIUM model a plain
   FileEntry * parameter is NEAR - the callee would store DS-relative
   while reads used the right segment, so a directory scan wrote its
   filenames over DGROUP's string literals.  Silent at -wx. */
static void set_entry(FileEntry far *e, const char *name, u32 size,
                      bool_t is_dir, u16 date, u16 time)
{
    int i = 0;
    while (name[i] != '\0' && i < NAME_LEN - 1) {
        e->name[i] = name[i];
        ++i;
    }
    e->name[i] = '\0';
    e->size   = size;
    e->is_dir = (u8)(is_dir ? 1 : 0);
    e->date   = date;
    e->time   = time;
    /* Marks never survive a rescan.  The table is refilled by index, and
       index 3 after a delete or a chdir is a different file than it was;
       carrying a mark across would tag whatever moved into the slot. */
    e->marked = 0;
}

/* Read the far name directly rather than through ent_name(), whose near
   buffer rotates - this gets called in loops. */
static bool_t is_dotdot(int i)
{
    return (g_ent[i].name[0] == '.' && g_ent[i].name[1] == '.' &&
            g_ent[i].name[2] == '\0') ? TRUE : FALSE;
}

/* How many entries are tagged, and is entry i one of them?  ".." is
   never counted - deleting the parent directory is not what Space on
   the first row should ever have set up. */
static int marked_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_count; ++i)
        if (g_ent[i].marked && !is_dotdot(i))
            ++n;
    return n;
}

static void add_entry(const char *name, u32 size, bool_t is_dir,
                      u16 date, u16 time)
{
    if (g_count >= FILES_MAX)
        return;                 /* only ever called for ".." - see below */
    set_entry(&g_ent[g_count++], name, size, is_dir, date, time);
}


/* Case-insensitive ASCII name compare (FAT find order is unsorted). */
/* A NEAR copy of entry i's name.  g_ent lives in far memory, so
   g_ent[i].name is a far char array: handing it straight to name_cmp,
   strcmp, strcpy, rename or copy_file - all of which take near pointers
   in the medium model - silently truncates the segment and reads DGROUP
   at that offset instead.  (Two rotating buffers so a caller can compare
   or copy two names at once.)

   This used to claim "every such site goes through here".  It did not:
   six sites reached the name through a local `FileEntry far *e` instead
   of the array, which this helper's name never mentions, and two of them
   were rmdir()/remove().  Do NOT restore that sentence - the buffers
   rotate, so any caller holding a name across a modal dialog (which can
   repaint the list, which calls this) must take its own copy. */
static const char *ent_name(int i)
{
    static char buf[2][NAME_LEN];
    static int  turn = 0;
    char *d;
    if (i < 0 || i >= g_count)
        return "";
    turn = 1 - turn;
    d = buf[turn];
    _fstrncpy(d, g_ent[i].name, NAME_LEN - 1);
    d[NAME_LEN - 1] = '\0';
    return d;
}

/* far parameters: callers pass both near buffers (which promote safely)
   and g_ent[i].name, which is far.  A near prototype truncated the
   latter and compared DGROUP garbage - and this drives the sort. */
static int name_cmp(const char far *a, const char far *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb)
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        ++a; ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* The file name's extension (after the last '.'), or "" without one. */
static const char far *name_ext(const char far *n)
{
    const char far *e = "";
    int i;
    for (i = 0; n[i] != '\0'; ++i)
        if (n[i] == '.')
            e = n + i + 1;
    return e;
}

/* Order two FILE entries by the active sort mode (name breaks every
   tie, so the order is total and stable-looking on real FAT). */
static int ent_cmp(const FileEntry far *a, const FileEntry far *b)
{
    int c = 0;
    switch (g_sort) {
    case SORT_EXT:
        c = name_cmp(name_ext(a->name), name_ext(b->name));
        break;
    case SORT_SIZE:                     /* biggest first                  */
        if (a->size != b->size)
            c = (a->size > b->size) ? -1 : 1;
        break;
    case SORT_DATE:                     /* newest first                   */
        if (a->date != b->date)
            c = (a->date > b->date) ? -1 : 1;
        else if (a->time != b->time)
            c = (a->time > b->time) ? -1 : 1;
        break;
    default:
        break;
    }
    return (c != 0) ? c : name_cmp(a->name, b->name);
}

/* Shell-sort g_ent[lo..hi).  Modal ordering applies to the files block
   only; directories stay by name.

   This was an insertion sort moving 22-byte structs: a 200-entry directory
   cost ~20000 struct moves worst case - a fifth of a second of pure copying
   on a 386SX, paid again on every F3 sort-mode change.  Ciura's gaps bring
   that to a few hundred.  Shell sort is not stable, but both comparators
   fall back to the name and a directory cannot hold two identical names,
   so the order is total and stability is not needed. */
static const int SORT_GAPS[8] = { 701, 301, 132, 57, 23, 10, 4, 1 };

static void sort_range(int lo, int hi, bool_t modal)
{
    int gi, i, j, gap;
    for (gi = 0; gi < 8; ++gi) {
        gap = SORT_GAPS[gi];
        if (gap >= hi - lo)
            continue;
        for (i = lo + gap; i < hi; ++i) {
            FileEntry tmp = g_ent[i];
            j = i;
            while (j - gap >= lo &&
                   (modal ? (ent_cmp(&g_ent[j - gap], &tmp) > 0)
                          : (name_cmp(g_ent[j - gap].name, tmp.name) > 0))) {
                g_ent[j] = g_ent[j - gap];
                j -= gap;
            }
            g_ent[j] = tmp;
        }
    }
}

void files_rescan(void)
{
    struct find_t ff;
    unsigned res;
    int dir_start, file_start, hi, nfiles, i;

    /* Remember what the cursor was on: a rescan used to throw the user
       back to the top, so deleting three files from a long directory
       meant scrolling back down twice. */
    char keep[NAME_LEN];
    int  keep_scroll = g_scroll;
    int  keep_sel    = g_sel;
    keep[0] = '\0';
    if (g_sel >= 0 && g_sel < g_count)
        strcpy(keep, ent_name(g_sel));

    g_free_ok = FALSE;                 /* re-ask DOS for the free space   */
    g_full   = FALSE;
    g_count  = 0;
    g_sel    = 0;
    g_scroll = 0;
    getcwd(g_cwd, sizeof(g_cwd));
    if (!path_is_root(g_cwd))
        add_entry("..", 0, TRUE, 0, 0);   /* always first, never sorted    */
    dir_start = g_count;

    /* ONE directory walk, not two.  The old code ran _dos_findfirst /
       _dos_findnext over the whole directory once for the folders and again
       for the files, doubling the DOS I/O - which on a floppy or a network
       drive is what the user actually waits for.  Fill directories upward
       from dir_start and files downward from the top of the array, then
       close the gap between them. */
    hi  = FILES_MAX;
    res = _dos_findfirst("*.*", _A_SUBDIR, &ff);
    while (res == 0) {
        bool_t dir = (ff.attrib & _A_SUBDIR) ? TRUE : FALSE;
        if (ff.name[0] != '.') {
            if (g_count < hi)
                set_entry(dir ? &g_ent[g_count++] : &g_ent[--hi],
                          ff.name, (u32)ff.size, dir, ff.wr_date, ff.wr_time);
            else
                /* The two ends met: this directory has more entries than
                   the table holds.  Dropping the rest with no sign of it
                   is the worst option available - the file is missing
                   from a list that still looks complete - so the path row
                   is marked.  (This is the real cap; add_entry's is not,
                   because add_entry is only ever called for "..".) */
                g_full = TRUE;
        }
        res = _dos_findnext(&ff);
    }
    file_start = g_count;
    nfiles     = FILES_MAX - hi;
    for (i = 0; i < nfiles; ++i)          /* slide the files down          */
        g_ent[file_start + i] = g_ent[hi + i];
    g_count = file_start + nfiles;

    sort_range(dir_start, file_start, FALSE);  /* dirs always by name     */
    sort_range(file_start, g_count, TRUE);     /* files by the sort mode  */

    /* Put the cursor back: on the same name if it survived, otherwise at
       the same POSITION.  Matching only by name missed the case this was
       written for - after a delete the remembered entry is precisely the
       one that no longer exists, so the user was dumped back to the top
       anyway. */
    if (keep[0] != '\0') {
        int found = -1;
        for (i = 0; i < g_count; ++i)
            if (name_cmp(ent_name(i), keep) == 0) { found = i; break; }
        g_sel = (found >= 0) ? found : keep_sel;
        if (g_sel >= g_count) g_sel = g_count - 1;
        if (g_sel < 0) g_sel = 0;
        g_scroll = keep_scroll;
        if (g_scroll > max_scroll()) g_scroll = max_scroll();
        if (g_scroll < 0) g_scroll = 0;
    }
}

static void scan_drives(void);   /* forward: the drive row is built from it */

void files_open(const char *path)
{
    g_view = FVIEW_FILES;
    /* The drive row is built from this now, and until it was, opening
       straight into a folder (a Documents entry, F4 from Find, a
       [shortcut] path) left g_drive_n at zero. */
    scan_drives();
    if (path != NULL && path[0] != '\0') {
        if (path[1] == ':') {
            unsigned total;
            unsigned drive = (unsigned)((path[0] | 0x20) - 'a');
            _dos_setdrive(drive + 1, &total);
        }
        chdir(path);
    }
    files_rescan();
}

/* ---- My Computer (drive-icon) view ----------------------------------- */

/* Probe every drive letter and keep the ones DOS says are present. */
static void scan_drives(void)
{
    int d;
    g_drive_n = 0;
    for (d = 1; d <= DRV_ALL; ++d)
        if (drive_valid(d)) {
            char letter = (char)('A' + d - 1);
            /* Once per enumeration, NOT once per compose.  files_draw
               runs on every frame the desktop composes, and asking DOS
               about 26 drives 18 times a second is what the free-space
               cache above exists to stop - the redirector query in
               drive_kind() is exactly the call a network drive is slow
               to answer. */
            g_dkind[g_drive_n]    = (char)drive_kind(letter);
            g_drives[g_drive_n++] = letter;
        }
    if (g_drive_sel >= g_drive_n) g_drive_sel = g_drive_n - 1;
    if (g_drive_sel < 0)          g_drive_sel = 0;
}

void files_open_computer(void)
{
    g_view = FVIEW_COMPUTER;
    scan_drives();
}

/* Enter drive g_drives[i]: switch to it, go to its root, show its files. */
static void enter_drive(int i)
{
    unsigned total;
    int drive1;
    if (i < 0 || i >= g_drive_n)
        return;
    drive1 = g_drives[i] - 'A' + 1;
    if (!drive_valid(drive1)) {
        dialog_message("Drive", "Drive not ready.", NULL);
        return;
    }
    _dos_setdrive((unsigned)drive1, &total);
    chdir("\\");
    g_view = FVIEW_FILES;
    files_rescan();
}

const char *files_cwd(void)            { return g_cwd; }
const char *files_launch_command(void) { return g_launch; }

/* ---- layout ---------------------------------------------------------- */

static void compute_layout(const Rect *c)
{
    int i;
    int bw = (c->w - (TB_N - 1)) / TB_N;
    int dw = font_adv() * 2 + 6;   /* drive button width (18) */
    int ly, lh;

    for (i = 0; i < TB_N; ++i)
        rect_set(&g_tb[i], c->x + i * (bw + 1), c->y, bw, TBH);

    /* One button per drive DOS actually has, not a fixed A: B: C: D:.
       Half of those were dead on a machine with no B:, and E: upwards
       had no button at all - reachable only by going out to My Computer
       and back in.  Capped so the path line keeps ~16 characters. */
    {
        int room = (c->w - font_adv() * 16) / (dw + 1);
        if (room < 1) room = 1;
        g_drv_shown = (g_drive_n > 0) ? g_drive_n : DRV_N;
        if (g_drv_shown > room)   g_drv_shown = room;
        if (g_drv_shown > DRV_ALL) g_drv_shown = DRV_ALL;
    }
    for (i = 0; i < g_drv_shown; ++i)
        rect_set(&g_drv[i], c->x + i * (dw + 1), c->y + TBH + 1, dw, DRH - 1);

    ly = c->y + TBH + 1 + DRH + 1;
    lh = c->h - (TBH + 1 + DRH + 1);
    if (lh < ROW_H + 2)
        lh = ROW_H + 2;

    rect_set(&g_sb_up,    c->x + c->w - SBW, ly,             SBW, SBW);
    rect_set(&g_sb_dn,    c->x + c->w - SBW, ly + lh - SBW,  SBW, SBW);
    rect_set(&g_sb_track, c->x + c->w - SBW, ly + SBW,       SBW, lh - 2 * SBW);
    rect_set(&g_list,     c->x, ly, c->w - SBW - 1, lh);

    g_visible = (g_list.h - 2) / ROW_H;
    if (g_visible < 1)
        g_visible = 1;
}

static int max_scroll(void)
{
    int m = g_count - g_visible;
    return (m > 0) ? m : 0;
}

/* The scroll thumb's current rectangle (mirrors draw_scrollbar), or an
   empty rect when the list fits and there is no thumb to grab. */
static void thumb_rect(Rect *r)
{
    int th, ty;
    if (g_count <= g_visible || g_sb_track.h <= 8) {
        rect_set(r, 0, 0, 0, 0);
        return;
    }
    th = g_sb_track.h * g_visible / g_count;
    if (th < 8) th = 8;
    ty = g_sb_track.y;
    if (max_scroll() > 0)              /* long: track*scroll tops 16 bits  */
        ty += (int)((long)(g_sb_track.h - th) * g_scroll / max_scroll());
    rect_set(r, g_sb_track.x, ty, g_sb_track.w, th);
}

/* Thumb drag state: begun by a press on the thumb (files_click), fed
   by the main loop while the button stays down, ended on release. */
static bool_t g_thumb      = FALSE;
static int    g_thumb_grab = 0;        /* pointer offset inside the thumb */

bool_t files_thumb_active(void)
{
    return g_thumb;
}

void files_thumb_end(void)
{
    g_thumb = FALSE;
}

bool_t files_thumb_drag(int my)
{
    int th, span, ns;
    if (!g_thumb || g_count <= g_visible)
        return FALSE;
    th = g_sb_track.h * g_visible / g_count;
    if (th < 8) th = 8;
    span = g_sb_track.h - th;
    if (span < 1)
        return FALSE;
    ns = (int)((long)(my - g_thumb_grab - g_sb_track.y) * max_scroll()
               / span);
    if (ns < 0) ns = 0;
    if (ns > max_scroll()) ns = max_scroll();
    if (ns == g_scroll)
        return FALSE;
    g_scroll = ns;
    return TRUE;
}

/* ---- drawing --------------------------------------------------------- */

static void draw_arrow(const Rect *r, bool_t up)
{
    int cx = r->x + r->w / 2;
    int cy = r->y + r->h / 2;
    int n  = r->w / 3;             /* arrow height, scales with the bar    */
    int i;
    if (n < 3) n = 3;
    for (i = 0; i < n; ++i) {
        int w = up ? (i + 1) : (n - i);
        vid_hline(cx - w, cy - n / 2 + i, w * 2 + 1, C_BLACK);
    }
}

static void draw_list(void)
{
    int row, i, y;
    char tmp[20];

    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll > max_scroll()) g_scroll = max_scroll();

    vid_fillrect(g_list.x, g_list.y, g_list.w, g_list.h, C_WHITE);

    {
    int fh     = font_h();
    int msz    = fh - 2;                       /* marker size (6 / 14)     */
    int name_x = g_list.x + fh + 6;            /* name column (14 / 22)    */

    for (row = 0; row < g_visible; ++row) {
        FileEntry far *e;
        int mx, my0, ty;
        u8  txt;
        i = g_scroll + row;
        if (i >= g_count)
            break;
        e = &g_ent[i];
        y   = g_list.y + 1 + row * ROW_H;
        my0 = y + (ROW_H - msz) / 2;            /* marker vertical centre   */
        ty  = y + (ROW_H - fh + 1) / 2;         /* text vertical centre     */

        if (i == g_sel) {
            /* C_TITLE: the shell selects in one blue everywhere. */
            vid_fillrect(g_list.x, y, g_list.w, ROW_H, C_TITLE);
            txt = C_WHITE;
        } else {
            txt = C_BLACK;
        }
        /* A tagged row is named in a colour of its own.  Not a second
           background fill: the cursor row is already filled, and two
           fills would make "tagged" and "where the cursor is" the same
           thing to look at.  Yellow on the blue cursor row, red on the
           white ones - both legible on their own ground. */
        if (e->marked)
            txt = (i == g_sel) ? C_YELLOW : C_RED;

        mx = g_list.x + 3;
        if (e->is_dir) {
            vid_fillrect(mx, my0, fh, msz, C_YELLOW);
            vid_rect(mx, my0, fh, msz, C_DKYELLOW);
        } else {
            vid_fillrect(mx + 1, my0, msz, msz, (i == g_sel) ? C_WHITE : C_CREAM);
            vid_rect(mx + 1, my0, msz, msz, C_SHADOW);
        }

        /* ent_name(), not e->name: font_draw takes a NEAR pointer and
           e->name is far, so the whole list used to be drawn from
           whatever lives at that offset in DGROUP.  The copy is 13
           bytes a row - invisible next to the glyph blitting. */
        font_draw(name_x, ty, ent_name(i), txt);

        if (e->is_dir)
            strcpy(tmp, "<DIR>");
        else
            sprintf(tmp, "%lu", (unsigned long)e->size);
        {
            int tw = font_text_width(tmp);
            int tx = g_list.x + g_list.w - 3 - tw;
            if (tx > name_x + 30)
                font_draw(tx, ty, tmp, txt);
        }
    }
    }
    ui_sink(g_list.x, g_list.y, g_list.w, g_list.h);
}

static void draw_scrollbar(void)
{
    ui_fill_face(g_sb_up.x, g_sb_up.y, SBW, SBW);
    ui_raise(g_sb_up.x, g_sb_up.y, SBW, SBW);
    draw_arrow(&g_sb_up, TRUE);

    ui_fill_face(g_sb_dn.x, g_sb_dn.y, SBW, SBW);
    ui_raise(g_sb_dn.x, g_sb_dn.y, SBW, SBW);
    draw_arrow(&g_sb_dn, FALSE);

    vid_fillrect(g_sb_track.x, g_sb_track.y, g_sb_track.w, g_sb_track.h, C_FACE);
    ui_sink(g_sb_track.x, g_sb_track.y, g_sb_track.w, g_sb_track.h);

    if (g_sb_track.h > 8) {
        /* Windows always drew a thumb - a full-height one when everything
           fits.  Hiding it left an empty sunken trough between two arrows,
           which reads as a broken control. */
        int th = (g_count > g_visible)
                 ? g_sb_track.h * g_visible / g_count : g_sb_track.h;
        int ty;
        if (th < 8) th = 8;
        if (th > g_sb_track.h) th = g_sb_track.h;
        ty = g_sb_track.y;
        if (max_scroll() > 0)          /* long: track*scroll tops 16 bits  */
            ty += (int)((long)(g_sb_track.h - th) * g_scroll / max_scroll());
        ui_fill_face(g_sb_track.x + 1, ty, SBW - 2, th);
        ui_raise(g_sb_track.x + 1, ty, SBW - 2, th);
    }
}

/* Lay out the drive-icon grid inside the client rect and remember each
   cell (also used by the click and key handlers). */
static void compute_drive_cells(const Rect *c)
{
    int cellw = font_adv() * 11 + 4;              /* holds "Local Disk"    */
    int cellh = ICON_SIZE + font_h() * 2 + 8;
    int cols  = (c->w - 4) / cellw;
    int i, pad;
    if (cols < 1) cols = 1;
    g_drive_cols = cols;
    pad = (c->w - 4 - cols * cellw) / 2;           /* centre the grid       */
    for (i = 0; i < g_drive_n; ++i) {
        int col = i % cols, row = i / cols;
        rect_set(&g_drvcell[i],
                 c->x + 2 + pad + col * cellw,
                 c->y + 4 + row * cellh, cellw, cellh);
    }
}

/* Two-line drive caption: the type over "(X:)", centred - very Win95.
   "Removable" rather than "Floppy" past B:, because a drive that answers
   "removable" at D: is a cartridge or a CD, not a diskette. */
static void draw_drive_label(int x, int y, int w, char letter, int kind,
                             u8 col)
{
    const char *type = (kind == DRV_REMOTE) ? "Network"
                     : (kind == DRV_FIXED)  ? "Local Disk"
                     : (letter == 'A' || letter == 'B') ? "Floppy"
                                                        : "Removable";
    char paren[8];
    sprintf(paren, "(%c:)", letter);
    ui_text_center(x, y,                 w, type,  col);
    ui_text_center(x, y + font_h() + 1,  w, paren, col);
}

static void draw_computer(const Rect *c)
{
    int i;
    /* C_WHITE, not C_FACE.  My Computer's content area was the same grey
       as the frame around it, so the only thing separating them was the
       sunken bevel - and on a tinted theme the two merged into one flat
       slab.  Windows used the window colour here, as the file list beside
       it already does. */
    vid_fillrect(c->x, c->y, c->w, c->h, C_WHITE);
    ui_sink(c->x, c->y, c->w, c->h);

    compute_drive_cells(c);
    for (i = 0; i < g_drive_n; ++i) {
        Rect *r = &g_drvcell[i];
        int icon_x = r->x + (r->w - ICON_SIZE) / 2;
        int label_y = r->y + ICON_SIZE + 2;
        u8  txt = C_BLACK;
        if (i == g_drive_sel) {                    /* selection band        */
            vid_fillrect(r->x + 2, label_y - 1, r->w - 4,
                         font_h() * 2 + 3, C_TITLE);
            txt = C_WHITE;
        }
        {   int kind = g_dkind[i];
            ui_icon((kind == DRV_REMOTE) ? ICON_NETDRV
                  : (kind == DRV_FIXED)  ? ICON_HDD
                                         : ICON_DISK, icon_x, r->y);
            draw_drive_label(r->x, label_y, r->w, g_drives[i], kind, txt);
        }
    }

    {   char st[24];
        sprintf(st, "%d drive%s", g_drive_n, (g_drive_n == 1) ? "" : "s");
        font_draw(c->x + 5, c->y + c->h - font_h() - 3, st, C_DKGRAY);
    }
}

void files_draw(const Rect *client)
{
    int i;
    unsigned cur = 3;

    if (g_view == FVIEW_COMPUTER) {
        draw_computer(client);
        return;
    }

    compute_layout(client);

    if (g_sel < g_scroll)               g_scroll = g_sel;
    if (g_sel >= g_scroll + g_visible)  g_scroll = g_sel - g_visible + 1;

    /* Toolbar. */
    for (i = 0; i < TB_N; ++i)
        ui_button(&g_tb[i], (i == TB_SORT) ? SORT_LBL[g_sort] : TB_LBL[i],
                  FALSE);

    /* Drive buttons (current drive shown pressed). */
    _dos_getdrive(&cur);
    for (i = 0; i < g_drv_shown; ++i) {
        char lbl[3];
        char letter = (g_drive_n > 0) ? g_drives[i] : (char)('A' + i);
        lbl[0] = letter; lbl[1] = ':'; lbl[2] = '\0';
        ui_button(&g_drv[i], lbl,
                  (cur == (unsigned)(letter - 'A' + 1)) ? TRUE : FALSE);
    }

    /* Path text after the drive buttons, with free space right-aligned. */
    {
        int last  = (g_drv_shown > 0) ? g_drv_shown - 1 : 0;
        int px    = g_drv[last].x + g_drv[last].w + 4;
        int right = client->x + client->w;
        int ty    = client->y + TBH + 3;
        char fs[20];
        unsigned long kb = drive_free_kb();
        int fw, pw;

        if (kb >= 10240UL) sprintf(fs, "%luM free", kb / 1024UL);
        else               sprintf(fs, "%luK free", kb);
        fw = font_text_width(fs);
        font_draw(right - fw - 2, ty, fs, C_DKGRAY);

        pw = (right - px) - fw - 8;        /* leave room for the free text */
        if (pw < 0) pw = 0;
        /* When the scan ran out of table, the path is followed by a
           marker: a truncated listing that looks complete is how you
           conclude a file is gone when it is only unlisted. */
        {   /* How many are tagged, where the eye already goes for
               "where am I" - and only when any are. */
            int nm2 = marked_count();
            if (nm2 > 0) {
                char tg[24];
                sprintf(tg, "%d tagged", nm2);
                fw += font_text_width(tg) + FONT_ADV;
                font_draw(right - fw - 2, ty, tg, C_RED);
            }
        }
        if (g_full) {
            const char *more = " +more";
            int mw = font_text_width(more);
            int cw = pw - mw;
            int used;
            if (cw < 0) cw = 0;
            /* font_draw_n returns CHARACTERS drawn, not pixels. */
            used = font_draw_n(px, ty, g_cwd, cw / FONT_ADV, C_BLACK);
            font_draw(px + used * FONT_ADV, ty, more, C_RED);
        } else {
            font_draw_n(px, ty, g_cwd, pw / FONT_ADV, C_BLACK);
        }
    }

    draw_list();
    draw_scrollbar();
}

/* ---- file operations ------------------------------------------------- */

/* Do these two paths name the same folder?  Case-insensitive, either
   slash, and a trailing separator does not make "C:\SUB\" a different
   place from "C:\SUB" - while "C:\" keeps its slash, being a root. */
static bool_t same_path(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b), i;
    while (la > 1 && (a[la - 1] == '\\' || a[la - 1] == '/') &&
           a[la - 2] != ':') --la;
    while (lb > 1 && (b[lb - 1] == '\\' || b[lb - 1] == '/') &&
           b[lb - 2] != ':') --lb;
    if (la != lb)
        return FALSE;
    for (i = 0; i < la; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca == '/') ca = '\\';
        if (cb == '/') cb = '\\';
        if (ca != cb)
            return FALSE;
    }
    return TRUE;
}

static bool_t copy_file(const char *src, const char *dst)
{
    FILE *fi, *fo;
    size_t n;

    /* Copying a file onto ITSELF destroys it: fopen(dst,"wb") truncates
       before the read has started, so the source is empty by the time
       fread sees it.  Measured, not theorised - a bulk copy into the
       folder the files were already in left two 0-byte files.
       Both callers can reach it: the bulk copy keeps each file's own
       name, so choosing the current folder collides on every one, and
       the single-file path collides if the suggested COPY_OF name is
       typed back to the original's. */
    {
        char here[CFG_PATH_LEN];
        int  k = 0, j = 0;
        getcwd(here, (int)sizeof(here));
        k = (int)strlen(here);
        if (k > 0 && here[k - 1] != '\\' && here[k - 1] != '/' &&
            here[k - 1] != ':' && k < (int)sizeof(here) - 1)
            here[k++] = '\\';
        while (src[j] != '\0' && k < (int)sizeof(here) - 1)
            here[k++] = src[j++];
        here[k] = '\0';
        if (same_path(here, dst))
            return FALSE;
    }

    fi = fopen(src, "rb");
    if (fi == NULL)
        return FALSE;
    fo = fopen(dst, "wb");
    if (fo == NULL) { fclose(fi); return FALSE; }
    {
        /* One exit, one cleanup.  The short-write branch used to return
           on the spot, and it is the branch a FULL DISK takes first -
           so the one failure this function exists to survive left a
           truncated file sitting at the destination, under a name
           op_copy may just have talked the user into overwriting.  The
           tidy-up below was right there and unreachable from it.

           A read error also makes fread return 0, so the loop exits the
           normal way and this returned TRUE on a truncated copy; and
           fclose(fo) is where the last buffered chunk actually reaches
           the disk, so a full disk loses the tail there and nowhere
           else. */
        bool_t bad = FALSE;
        for (;;) {
            n = fread(g_copybuf, 1, sizeof(g_copybuf), fi);
            if (n == 0)
                break;
            if (fwrite(g_copybuf, 1, n, fo) != n) { bad = TRUE; break; }
        }
        if (ferror(fi))
            bad = TRUE;
        fclose(fi);
        if (fclose(fo) != 0)
            bad = TRUE;
        if (bad) {
            remove(dst);               /* no half a file left behind        */
            return FALSE;
        }
    }
    return TRUE;
}

static void op_new(void)
{
    char name[NAME_LEN] = "";
    if (dialog_input("New Folder", "Folder name:", name, sizeof(name)) != DLG_OK)
        return;
    if (name[0] == '\0')
        return;
    if (mkdir(name) != 0)
        dialog_message("New Folder", "Could not create", name);
    files_rescan();
}

static void op_rename(void)
{
    char name[NAME_LEN];
    if (!selection_ok())
        return;
    strcpy(name, ent_name(g_sel));
    if (dialog_input("Rename", "New name:", name, sizeof(name)) != DLG_OK)
        return;
    if (name[0] == '\0')
        return;
    if (rename(ent_name(g_sel), name) != 0)
        dialog_message("Rename", "Could not rename.", NULL);
    files_rescan();
}

/* Copy or move every tagged entry into one folder, keeping their names.
   Same discipline as the bulk delete: near copies before libc sees a
   name, no rescan inside the loop, one tally at the end. */
/* dir + '\\' + entry i's name.  One builder, so the collision scan and
   the copy that follows it can never disagree about where a file is
   going. */
static void dest_path(char *out, int cap, const char *dir, int i)
{
    int k = 0, j = 0;
    char nm[NAME_LEN];
    _fstrncpy(nm, g_ent[i].name, NAME_LEN - 1);
    nm[NAME_LEN - 1] = '\0';
    while (dir[k] != '\0' && k < cap - 2) { out[k] = dir[k]; ++k; }
    if (k > 0 && out[k - 1] != '\\' && out[k - 1] != '/' && out[k - 1] != ':')
        out[k++] = '\\';
    while (nm[j] != '\0' && k < cap - 1) out[k++] = nm[j++];
    out[k] = '\0';
}

static void bulk_to_folder(bool_t moving)
{
    char dir[CFG_PATH_LEN], q[48];
    int  n = marked_count(), i, failed = 0, skipped = 0;
    sprintf(q, "%s %d tagged item%s to",
            moving ? "Move" : "Copy", n, (n == 1) ? "" : "s");
    if (!filedlg_folder(q, dir, sizeof(dir)) || dir[0] == '\0')
        return;
    {   /* Caught here too, so the whole operation is refused with a
           sentence rather than reported as N failures afterwards. */
        char here[CFG_PATH_LEN];
        getcwd(here, (int)sizeof(here));
        if (same_path(here, dir)) {
            dialog_message(moving ? "Move" : "Copy",
                           "That is the folder they are",
                           "already in.");
            return;
        }
    }
    /* Anything already there under the same name?  The single-file Copy
       asks before it clobbers; the bulk one was about to do it silently,
       once per collision, which is the same bug with more victims.  Ask
       ONCE for the set - a box per file is not a question, it is a
       queue - and take No as cancelling the whole thing rather than
       half-doing it. */
    {
        int clash = 0;
        for (i = 0; i < g_count; ++i) {
            FILE *ex;
            char dst[CFG_PATH_LEN];
            if (!g_ent[i].marked || is_dotdot(i) || g_ent[i].is_dir)
                continue;
            dest_path(dst, sizeof(dst), dir, i);
            ex = fopen(dst, "rb");
            if (ex != NULL) { fclose(ex); ++clash; }
        }
        if (clash > 0) {
            sprintf(q, "%d file%s there already.", clash,
                    (clash == 1) ? " is" : "s are");
            if (dialog_confirm(moving ? "Move" : "Copy", q,
                               (clash == 1) ? "Replace it?"
                                            : "Replace them?") != DLG_YES)
                return;
        }
    }
    for (i = 0; i < g_count; ++i) {
        char src[NAME_LEN], dst[CFG_PATH_LEN];
        if (!g_ent[i].marked || is_dotdot(i))
            continue;
        if (g_ent[i].is_dir) { ++skipped; continue; }   /* files only */
        _fstrncpy(src, g_ent[i].name, NAME_LEN - 1);
        src[NAME_LEN - 1] = '\0';
        dest_path(dst, sizeof(dst), dir, i);
        /* rename() FAILS on an existing destination rather than
           replacing it, so a move the user has just approved needs the
           old one out of the way first - exactly what single-file Move
           does. */
        if (moving)
            remove(dst);
        if (moving ? (rename(src, dst) != 0) : !copy_file(src, dst))
            ++failed;
    }
    if (failed > 0 || skipped > 0) {
        sprintf(q, "%d failed, %d folder(s) skipped.", failed, skipped);
        dialog_message(moving ? "Move" : "Copy", q,
                       "A move cannot cross drives.");
    }
    files_rescan();
}

static void op_copy(void)
{
    /* CFG_PATH_LEN, not the old 40: the picker hands back an absolute
       path, and 40 characters is not enough room for one. */
    char dst[CFG_PATH_LEN];
    if (marked_count() > 0) { bulk_to_folder(FALSE); return; }
    if (!selection_ok() || g_ent[g_sel].is_dir) {
        dialog_message("Copy", "Select a file to copy.", NULL);
        return;
    }
    /* Keep the source's extension.  "COPY_OF." alone meant accepting the
       suggested name turned CASTALIA.EXE into an extension-less COPY_OF,
       which the shell can no longer associate with anything. */
    {
        const char far *ext = name_ext(g_ent[g_sel].name);
        int k = 0;
        strcpy(dst, "COPY_OF.");
        k = (int)strlen(dst);
        while (*ext != '\0' && k < (int)sizeof(dst) - 1)
            dst[k++] = *ext++;
        dst[k] = '\0';
    }
    /* Browse to the destination instead of typing it.  filedlg.h has
       claimed Copy and Move since it was written; they were the two it
       never got wired into, so "Copy to (name/path):" stayed a bare
       field you had to already know the answer to fill in. */
    if (!filedlg("Copy to", "*.*", dst, sizeof(dst), TRUE))
        return;
    if (dst[0] == '\0')
        return;
    /* copy_file opens the destination "wb", which truncates.  Deleting a
       file asks first; copying quietly clobbered whatever was already
       there - and the field is pre-filled, so typing a real name over it
       is the common case, not the rare one. */
    {
        FILE *ex = fopen(dst, "rb");
        if (ex != NULL) {
            fclose(ex);
            if (dialog_confirm("Copy", "Replace the existing file?",
                               dst) != DLG_YES)
                return;
        }
    }
    if (!copy_file(ent_name(g_sel), dst))
        dialog_message("Copy", "Copy failed - the disk may be",
                       "full or write protected.");
    files_rescan();
}

static void op_move(void)
{
    char dst[CFG_PATH_LEN];
    if (marked_count() > 0) { bulk_to_folder(TRUE); return; }
    if (!selection_ok())
        return;
    /* Pre-fill with the current name so moving a file somewhere else is
       "browse to the folder, press Move" - the name only needs touching
       when you also want to rename on the way. */
    strcpy(dst, ent_name(g_sel));
    if (!filedlg("Move to", "*.*", dst, sizeof(dst), TRUE))
        return;
    if (dst[0] == '\0')
        return;
    /* rename() silently replaces nothing - it FAILS on an existing
       destination - but it also happily renames a file onto itself and
       reports success, so the useful check is the one Copy does: warn
       before a clobber that would otherwise just look like a failure. */
    {
        FILE *ex = fopen(dst, "rb");
        if (ex != NULL) {
            fclose(ex);
            if (dialog_confirm("Move", "Replace the existing file?",
                               dst) != DLG_YES)
                return;
            remove(dst);
        }
    }
    if (rename(ent_name(g_sel), dst) != 0)
        dialog_message("Move", "Move failed. DOS cannot move",
                       "a file across drives.");
    files_rescan();
}

static void op_delete(void)
{
    /* A NEAR copy on this frame, not ent_name() and not e->name.
       e->name is FAR: rmdir/remove/dialog_confirm all take near
       pointers, so the segment was dropped and DOS was handed
       DGROUP:<offset of g_ent> - whatever string literal sits there.
       Disassembly of the old code: the is_dir test read "es:0dH[si]"
       with es loaded, then "mov ax,si / call far ptr remove_" passed
       the offset alone.  That deletes the WRONG FILE, after showing
       the user a confirmation naming a different one again.
       A local rather than ent_name() because dialog_confirm runs a
       modal loop that can repaint the list, and the repaint calls
       ent_name() - which would rotate the buffer under our pointer
       between the question and the delete. */
    char   nm[NAME_LEN];
    bool_t isdir;
    int    ok;

    /* Tagged files first: one question for the whole set, not one per
       file.  Everything the single-file path is careful about applies
       here and more so - the name goes into a NEAR local before it
       reaches remove()/rmdir(), and the table is NOT rescanned inside
       the loop, so no index moves under us between the count and the
       delete.  Failures are counted and reported once at the end rather
       than raising a box per file, which on a bad disk would be a
       dialog for every one of two hundred entries. */
    if (marked_count() > 0) {
        int n = marked_count(), i, failed = 0;
        char q[40];
        sprintf(q, "Delete %d tagged item%s?", n, (n == 1) ? "" : "s");
        if (dialog_confirm("Delete", q, "They cannot be brought back.")
            != DLG_YES)
            return;
        for (i = 0; i < g_count; ++i) {
            char  mn[NAME_LEN];
            bool_t md;
            if (!g_ent[i].marked || is_dotdot(i))
                continue;
            _fstrncpy(mn, g_ent[i].name, NAME_LEN - 1);
            mn[NAME_LEN - 1] = '\0';
            md = g_ent[i].is_dir ? TRUE : FALSE;
            if (md ? (rmdir(mn) != 0) : (remove(mn) != 0))
                ++failed;
        }
        if (failed > 0) {
            sprintf(q, "%d could not be deleted.", failed);
            dialog_message("Delete", q,
                           "Folder not empty, or file in use.");
        }
        files_rescan();
        return;
    }

    if (!selection_ok())
        return;
    _fstrncpy(nm, g_ent[g_sel].name, NAME_LEN - 1);
    nm[NAME_LEN - 1] = '\0';
    isdir = g_ent[g_sel].is_dir ? TRUE : FALSE;
    ok = dialog_confirm("Delete",
                        isdir ? "Delete this folder?" : "Delete this file?",
                        nm);
    if (ok != DLG_YES)
        return;
    if (isdir) {
        if (rmdir(nm) != 0)
            dialog_message("Delete", "Folder not empty", "or in use.");
    } else {
        if (remove(nm) != 0)
            dialog_message("Delete", "Could not delete.", NULL);
    }
    files_rescan();
}

static void op_up(void)
{
    getcwd(g_cwd, sizeof(g_cwd));
    if (path_is_root(g_cwd)) {         /* at a drive root: back to My Computer */
        files_open_computer();
        return;
    }
    chdir("..");
    files_rescan();
}

static void run_op(int which)
{
    switch (which) {
    case 0: op_up();     break;
    case 1: op_new();    break;
    case 2: op_rename(); break;
    case 3: op_copy();   break;
    case 4: op_move();   break;
    case 5: op_delete(); break;
    default: break;
    }
}

static void switch_drive(int drive1)
{
    unsigned total;
    if (!drive_valid(drive1)) {
        dialog_message("Drive", "Drive not ready.", NULL);
        return;
    }
    _dos_setdrive((unsigned)drive1, &total);
    chdir("\\");
    files_rescan();
}

/* ---- activation & interaction --------------------------------------- */

static int files_activate(void)
{
    /* Near copy first: chdir() and g_launch are both near, e->name is
       far.  Passing it straight through walked into DGROUP, so a
       double-click changed directory to - or launched - whatever string
       literal happened to sit at that offset. */
    char nm[NAME_LEN];
    if (g_sel < 0 || g_sel >= g_count)
        return FILES_NONE;
    _fstrncpy(nm, g_ent[g_sel].name, NAME_LEN - 1);
    nm[NAME_LEN - 1] = '\0';
    if (g_ent[g_sel].is_dir) {
        chdir(nm);
        files_rescan();
        return FILES_REDRAW;
    }
    /* EVERY file launches now: executables spawn, documents open in
       their applet, and anything else lands in the Hex Peek - a double
       click always shows you something true. */
    strcpy(g_launch, nm);
    return FILES_LAUNCH;
}

int files_click(const Rect *client, int mx, int my, bool_t dbl)
{
    int i, row;

    if (g_view == FVIEW_COMPUTER) {
        compute_drive_cells(client);
        for (i = 0; i < g_drive_n; ++i) {
            if (rect_contains(&g_drvcell[i], mx, my)) {
                if (dbl) enter_drive(i);   /* no pre-select needed */
                else                          g_drive_sel = i;
                return FILES_REDRAW;
            }
        }
        return FILES_NONE;
    }

    compute_layout(client);

    for (i = 0; i < TB_N; ++i) {
        if (rect_contains(&g_tb[i], mx, my)) {
            if (i == TB_SORT) {            /* cycle Name/Ext/Size/Date  */
                g_sort = (g_sort + 1) & 3;
                files_rescan();
            } else {
                run_op(i);
            }
            return FILES_REDRAW;
        }
    }
    for (i = 0; i < g_drv_shown; ++i) {
        if (rect_contains(&g_drv[i], mx, my)) {
            char letter = (g_drive_n > 0) ? g_drives[i] : (char)('A' + i);
            switch_drive(letter - 'A' + 1);
            return FILES_REDRAW;
        }
    }
    if (rect_contains(&g_sb_up, mx, my)) {
        if (g_scroll > 0) --g_scroll;
        return FILES_REDRAW;
    }
    if (rect_contains(&g_sb_dn, mx, my)) {
        if (g_scroll < max_scroll()) ++g_scroll;
        return FILES_REDRAW;
    }
    if (rect_contains(&g_sb_track, mx, my)) {
        Rect th;
        thumb_rect(&th);
        if (rect_contains(&th, mx, my)) {   /* grab the thumb to drag it */
            g_thumb      = TRUE;
            g_thumb_grab = my - th.y;
            return FILES_REDRAW;
        }
        if (my < th.y)                      /* page toward the click     */
            g_scroll -= g_visible;
        else
            g_scroll += g_visible;
        if (g_scroll < 0) g_scroll = 0;
        if (g_scroll > max_scroll()) g_scroll = max_scroll();
        return FILES_REDRAW;
    }

    if (rect_contains(&g_list, mx, my)) {
        row = (my - g_list.y - 1) / ROW_H;
        i = g_scroll + row;
        if (i >= 0 && i < g_count) {
            g_sel = i;
            /* Every other launcher in the shell fires on the first
               double-click; this one used to need the row selected
               first, so you had to double-click twice. */
            if (dbl)
                return files_activate();
            return FILES_REDRAW;
        }
    }
    return FILES_NONE;
}

int files_key(int key)
{
    if (g_view == FVIEW_COMPUTER) {
        switch (key) {
        case KEY_LEFT:                   /* left  */
            if (g_drive_sel > 0) { --g_drive_sel; return FILES_REDRAW; }
            break;
        case KEY_RIGHT:                   /* right */
            if (g_drive_sel < g_drive_n - 1) { ++g_drive_sel; return FILES_REDRAW; }
            break;
        case KEY_UP:                   /* up    */
            if (g_drive_sel - g_drive_cols >= 0) {
                g_drive_sel -= g_drive_cols; return FILES_REDRAW;
            }
            break;
        case KEY_DOWN:                   /* down  */
            if (g_drive_sel + g_drive_cols < g_drive_n) {
                g_drive_sel += g_drive_cols; return FILES_REDRAW;
            }
            break;
        case KEY_ENTER:                           /* enter -> open the drive */
            enter_drive(g_drive_sel);
            return FILES_REDRAW;
        default:
            break;
        }
        return FILES_NONE;
    }

    switch (key) {
    case KEY_UP:                       /* up    */
        if (g_sel > 0) { --g_sel; return FILES_REDRAW; }
        break;
    case KEY_DOWN:                       /* down  */
        if (g_sel < g_count - 1) { ++g_sel; return FILES_REDRAW; }
        break;
    case KEY_PGUP:                       /* pgup  */
        g_sel -= g_visible; if (g_sel < 0) g_sel = 0;
        return FILES_REDRAW;
    case KEY_PGDN:                       /* pgdn  */
        g_sel += g_visible; if (g_sel > g_count - 1) g_sel = g_count - 1;
        return FILES_REDRAW;
    case KEY_ENTER:                               /* enter */
        return files_activate();
    case KEY_BACK:                                /* backspace -> parent */
        op_up();
        return FILES_REDRAW;
    case KEY_DEL:                       /* Del -> delete          */
        op_delete();
        return FILES_REDRAW;
    case KEY_F7:                           /* F7  -> new folder      */
        op_new();
        return FILES_REDRAW;
    case KEY_F2:                       /* F2  -> rename          */
        op_rename();
        return FILES_REDRAW;
    case KEY_F3:                       /* F3  -> cycle the sort  */
        g_sort = (g_sort + 1) & 3;
        files_rescan();
        return FILES_REDRAW;
    case KEY_F5:                       /* F5  -> copy            */
        op_copy();
        return FILES_REDRAW;
    case KEY_F6:                       /* F6  -> move            */
        /* Move was the one toolbar operation with no key, while New,
           Rename, Copy and Delete all had one. */
        op_move();
        return FILES_REDRAW;
    /* Tag the lot, clear the lot, invert.  Tagging fifty files one Space
       at a time is the problem tagging was supposed to solve, and plus,
       minus and star are what the DOS file managers of the era bound
       this to.  None of the three collides with the type-ahead below,
       which claims only letters and digits.  Folders are left alone: the operations that
       read tags either skip directories or refuse them. */
    case '+': case '-': case '*': {
        int i, changed = 0;
        for (i = 0; i < g_count; ++i) {
            u8 was = g_ent[i].marked;
            if (is_dotdot(i) || g_ent[i].is_dir)
                continue;
            g_ent[i].marked = (u8)(key == '+' ? 1 :
                                   key == '-' ? 0 : (was ? 0 : 1));
            if (g_ent[i].marked != was) ++changed;
        }
        if (changed > 0) return FILES_REDRAW;
        break;
    }
    case KEY_SPACE:                      /* tag / untag for a bulk Del */
        if (g_count > 0 && g_sel >= 0 && g_sel < g_count &&
            !is_dotdot(g_sel)) {
            g_ent[g_sel].marked = (u8)(g_ent[g_sel].marked ? 0 : 1);
            /* Step down, as every tagging file manager has: tagging a
               run is then Space Space Space, not Space Down Space Down. */
            if (g_sel < g_count - 1) ++g_sel;
            return FILES_REDRAW;
        }
        break;
    case KEY_HOME:                       /* Home -> first entry    */
        if (g_count > 0) { g_sel = 0; return FILES_REDRAW; }
        break;
    case KEY_END:                       /* End  -> last entry     */
        if (g_count > 0) { g_sel = g_count - 1; return FILES_REDRAW; }
        break;
    default:
        break;
    }

    /* Type-ahead: a letter or digit jumps to the next entry starting with
       it, wrapping.  On a FAT directory of any size this is the
       difference between finding a file and scrolling for it, and every
       file manager of the era had it. */
    if ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
        (key >= '0' && key <= '9')) {
        char want = (char)key;
        int  i;
        if (want >= 'a' && want <= 'z')
            want = (char)(want - 32);
        for (i = 1; i <= g_count; ++i) {
            int  k = (g_sel + i) % g_count;
            char c;
            if (g_count <= 0)
                break;
            c = (char)g_ent[k].name[0];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 32);
            if (c == want) {
                g_sel = k;
                return FILES_REDRAW;
            }
        }
    }
    return FILES_NONE;
}
