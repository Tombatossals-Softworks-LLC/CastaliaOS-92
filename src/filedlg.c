/* ======================================================================
 * filedlg.c - the modal file picker for Castalia 92
 * ====================================================================== */
#include <i86.h>       /* int86 - the drive-validity probe                */
#include <dos.h>       /* _dos_findfirst / _dos_findnext / _dos_setdrive  */
#include <direct.h>
#include <stdio.h>
#include <string.h>
#include "filedlg.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "mouse.h"
#include "keyboard.h"
#include "music.h"
#include "system.h"    /* sys_idle */
#include "dialog.h"    /* dialog_note_takeover */

/* Entries listed at once.  200 to match the Disk Cabinet: the two show
   the same directories and it should not be possible to see a file in
   one and not the other.  Each entry is 16 bytes of FAR memory, so the
   whole table is 3200 bytes and none of it is DGROUP. */
/* Matched to files.c's FILES_MAX for the same reason: the picker is how
   every Load and Save sees a directory, so a cap here hides files from
   the one place you go looking for them. */
#define FD_MAX   400
#define FD_NAME  14
#define FD_PLEN  80

/* far: 96 entries is ~1.5 KB and DGROUP is the scarce segment.  Names are
   copied NEAR on the way out - font_draw and fopen both take near
   pointers, which is the trap this project has hit four times. */
typedef struct {
    char   name[FD_NAME];
    bool_t is_dir;
} FdEnt;
static FdEnt far g_ent[FD_MAX];
static int  g_n;
static int  g_sel, g_top, g_vis;
static char g_dir[FD_PLEN];        /* the directory being browsed          */
static bool_t g_full;              /* the scan hit FD_MAX and dropped some */
static bool_t g_drv_view;          /* listing DRIVES, not a directory      */
/* Folder mode: the caller wants a DIRECTORY back, not a file in one.
   Multi-file Copy and Move need it (there is no single filename to
   return for a set), and the Gramophone's + FOLDER was still asking the
   user to type a path from memory.  Same dialog, three differences: the
   file pass of the scan is skipped, the name field is not drawn because
   there is no name to give, and OK returns the folder being browsed. */
static bool_t g_dir_mode;
/* The wildcard filter, which may hold SEVERAL patterns separated by
   semicolons - "*.WA?;*.MI?".  DOS matches one 8.3 wildcard per
   findfirst and no single one covers WAV and MIDI, which is why the
   Gramophone ended up with two callers passing two different filters:
   the "play" verb asked for *.WA? and could not reach a .MID at all,
   while its own Eject button asked for *.* and offered CASTALIA.EXE. */
static char g_pat[40];

static Rect g_box, g_list, g_field, g_ok, g_cancel, g_up;
static Rect g_sb_up, g_sb_dn, g_sb_tr;
static int  g_th, g_row_h;

/* ---- listing --------------------------------------------------------- */

static void ent_near(int i, char *out)
{
    if (i < 0 || i >= g_n) { out[0] = '\0'; return; }
    _fstrncpy(out, g_ent[i].name, FD_NAME - 1);
    out[FD_NAME - 1] = '\0';
}

/* The gap sequence files.c sorts with.  See the note at the sort. */
static const int SORT_GAPS[8] = { 701, 301, 132, 57, 23, 10, 4, 1 };

/* By ENTRY, not by index: a shell sort holds one element aside while it
   shuffles the others, so there is no index to name it by.  Both
   parameters are `far` on purpose - a plain FdEnt * is NEAR in the
   medium model and would arrive with the wrong segment, which is the
   trap ci/nearfar.sh exists to catch. */
static int ent_cmp_e(const FdEnt far *a, const FdEnt far *b)
{
    char na[FD_NAME], nb[FD_NAME];
    int  i;
    if (a->is_dir != b->is_dir)               /* folders first, as always  */
        return a->is_dir ? -1 : 1;
    for (i = 0; i < FD_NAME - 1 && a->name[i] != '\0'; ++i) na[i] = a->name[i];
    na[i] = '\0';
    for (i = 0; i < FD_NAME - 1 && b->name[i] != '\0'; ++i) nb[i] = b->name[i];
    nb[i] = '\0';
    for (i = 0; na[i] != '\0' && nb[i] != '\0'; ++i) {
        char ca = na[i], cb = nb[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return (int)ca - (int)cb;
    }
    return (int)(unsigned char)na[i] - (int)(unsigned char)nb[i];
}

/* A name that already carries a drive ("A:GAME.SAV") or starts at a root
   ("\\DOCS\\X.TXT") is a path in its own right; prefixing the browsed
   folder to it yields C:\\HERE\\A:GAME.SAV, which names nothing. */
static bool_t is_absolute(const char *p)
{
    if (p[0] == '\\' || p[0] == '/')
        return TRUE;
    return (p[0] != '\0' && p[1] == ':') ? TRUE : FALSE;
}

/* Is `p` the root of a drive - the point above which only the drive list
   remains?  Matches files.c's path_is_root. */
static bool_t at_root(const char *p)
{
    if (p[0] == '\0' || p[1] != ':')
        return FALSE;
    if (p[2] == '\0')
        return TRUE;
    return ((p[2] == '\\' || p[2] == '/') && p[3] == '\0') ? TRUE : FALSE;
}

/* INT 21h AH=36h on a drive that is not there returns AX=FFFF - the same
   probe the Disk Cabinet uses, so the two agree on which drives exist. */
static bool_t drive_valid(int drive1)
{
    union REGS r;
    r.h.ah = 0x36;
    r.h.dl = (unsigned char)drive1;
    int86(0x21, &r, &r);
    return (r.x.ax != 0xFFFF) ? TRUE : FALSE;
}

static void add_entry(const char *name, bool_t is_dir)
{
    int i = 0;
    if (g_n >= FD_MAX) {
        /* A directory bigger than the table.  Dropping the rest silently
           is the worst of the options: the file you came for is simply
           not in the list and nothing says why.  The foot says so. */
        g_full = TRUE;
        return;
    }
    while (name[i] != '\0' && i < FD_NAME - 1) {
        g_ent[g_n].name[i] = name[i];
        ++i;
    }
    g_ent[g_n].name[i] = '\0';
    g_ent[g_n].is_dir  = is_dir;
    ++g_n;
}

/* Two passes: directories with *.*, then files matching the filter.  One
   find_t is live at a time - DOS keeps the search state in the DTA, and
   interleaving two walks corrupts both. */
/* The drive list, standing in for the level above a drive root the way
   My Computer does elsewhere in the shell.  Without it the picker could
   only ever reach what was under the drive it opened on: the name field
   holds 8.3 and no more, so a path to another drive cannot be typed
   either, and saving a document to a floppy was simply not possible. */
static void scan_drives(void)
{
    int d;
    g_n = 0; g_sel = 0; g_top = 0; g_full = FALSE;
    strcpy(g_dir, "Drives");
    for (d = 1; d <= 26; ++d) {
        if (drive_valid(d)) {
            char nm[3];
            nm[0] = (char)('A' + d - 1); nm[1] = ':'; nm[2] = '\0';
            add_entry(nm, TRUE);
        }
    }
}

static void scan(void)
{
    struct find_t ff;
    unsigned rc;
    int i, j, gi;

    if (g_drv_view) { scan_drives(); return; }

    g_full = FALSE;

    g_n = 0;
    g_sel = 0;
    g_top = 0;

    rc = _dos_findfirst("*.*", _A_SUBDIR | _A_RDONLY | _A_ARCH, &ff);
    while (rc == 0) {
        if ((ff.attrib & _A_SUBDIR) && ff.name[0] != '.')
            add_entry(ff.name, TRUE);
        rc = _dos_findnext(&ff);
    }
    if (!g_dir_mode) {
        /* One walk per semicolon-separated pattern.  The patterns are the
           caller's and are expected not to overlap - nothing dedupes, so
           "*.*;*.TXT" would list the .TXT files twice. */
        int ps = 0;
        while (g_pat[ps] != '\0') {
            char one[14];
            int  k = 0;
            while (g_pat[ps] != '\0' && g_pat[ps] != ';' &&
                   k < (int)sizeof(one) - 1)
                one[k++] = g_pat[ps++];
            one[k] = '\0';
            while (g_pat[ps] == ';')
                ++ps;
            if (k == 0)
                continue;
            rc = _dos_findfirst(one, _A_RDONLY | _A_ARCH, &ff);
            while (rc == 0) {
                if (!(ff.attrib & _A_SUBDIR))
                    add_entry(ff.name, FALSE);
                rc = _dos_findnext(&ff);
            }
        }
    }

    /* A shell sort, as files.c uses.  This was an insertion sort with the
       comment "n is small", and n was 200 when that was written and is
       400 now - my own change, so my own comment to correct.  Measured
       at cycles=1100 (a 386SX/16) the whole open took 1.5 seconds with
       400 entries, most of it the two DOS directory walks, so this was
       not urgent; it is here because an O(n^2) sort under a comment
       claiming otherwise is how the NEXT cap rise goes wrong. */
    for (gi = 0; gi < 8; ++gi) {
        int gap = SORT_GAPS[gi];
        if (gap >= g_n)
            continue;
        for (i = gap; i < g_n; ++i) {
            FdEnt tmp = g_ent[i];
            j = i;
            while (j - gap >= 0 && ent_cmp_e(&g_ent[j - gap], &tmp) > 0) {
                g_ent[j] = g_ent[j - gap];
                j -= gap;
            }
            g_ent[j] = tmp;
        }
    }
    getcwd(g_dir, (int)sizeof(g_dir));
}

/* ---- layout and paint ------------------------------------------------ */

static void layout(void)
{
    int fh = font_h();
    /* Size against the SCREEN, not the font.  40 characters of an
       8-pixel advance is 320: the whole width in Mode 13h and half of it
       in Mode 12h, where the taller font also left the box at 55% of the
       screen height against 73% in Mode 13h.  The picker was showing
       about the same fourteen rows in both modes while Mode 12h had two
       and a half times the pixels to spend.

       The (long) is not decoration.  SCREEN_H is 480 in Mode 12h and
       480 * 73 is 35040, which overflows a signed 16-bit int to -30496
       and makes h come out at -305: the picker then silently does not
       open at all - no dialog, no error, the key appears to do nothing.
       Mode 13h never showed it because 200 * 73 fits.  splash.c's
       fit_scale carries the same warning about SCREEN_W * frac, and I
       walked into it anyway; ci/consistency.sh now checks for it.

       Both figures are unchanged in Mode 13h: 320 x 146. */
    int w  = font_adv() * 40 * ui_scale();
    int h  = (int)((long)SCREEN_H * 73 / 100);
    int bw = font_adv() * 8 + 2, bh = fh + 5;

    g_th    = fh + 3;
    g_row_h = fh + 1;
    if (w > SCREEN_W - 8) w = SCREEN_W - 8;
    if (h > SCREEN_H - 8) h = SCREEN_H - 8;
    rect_set(&g_box, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2, w, h);

    rect_set(&g_field, g_box.x + 6, g_box.y + 2 + g_th + 4,
             g_box.w - 12 - (bw + 6), fh + 5);
    rect_set(&g_up, g_box.x + g_box.w - 6 - bw, g_field.y, bw, fh + 5);

    rect_set(&g_list, g_box.x + 6, g_field.y + g_field.h + 4,
             g_box.w - 12 - 12,
             g_box.h - (g_field.y - g_box.y) - g_field.h - 4 - bh - 12);
    rect_set(&g_sb_up, g_list.x + g_list.w, g_list.y, 12, 12);
    rect_set(&g_sb_dn, g_list.x + g_list.w, g_list.y + g_list.h - 12, 12, 12);
    rect_set(&g_sb_tr, g_list.x + g_list.w, g_list.y + 12, 12,
             g_list.h - 24);
    if (g_sb_tr.h < 0) g_sb_tr.h = 0;

    g_vis = (g_list.h - 4) / g_row_h;
    if (g_vis < 1) g_vis = 1;

    rect_set(&g_ok, g_box.x + g_box.w - 6 - bw * 2 - 6,
             g_box.y + g_box.h - bh - 5, bw, bh);
    rect_set(&g_cancel, g_box.x + g_box.w - 6 - bw,
             g_box.y + g_box.h - bh - 5, bw, bh);
}

static void draw_dlg(const char *title, const char *buf, bool_t saving)
{
    int i, fh = font_h();
    char nm[FD_NAME];

    ui_shadow(g_box.x, g_box.y, g_box.w, g_box.h);
    ui_fill_face(g_box.x, g_box.y, g_box.w, g_box.h);
    ui_raise(g_box.x, g_box.y, g_box.w, g_box.h);
    vid_title_bar(g_box.x + 2, g_box.y + 2, g_box.w - 4, g_th, TRUE);
    font_draw(g_box.x + 6, g_box.y + 2 + (g_th - fh) / 2, title, C_WHITE);

    /* The name field, with the folder being browsed above the list. */
    if (g_dir_mode) {
        /* No field: there is nothing to type.  The row is not left blank
           either - an empty sunken box invites a click that does
           nothing - so it carries the instruction instead. */
        font_draw(g_field.x, g_field.y + 3, "Choose a folder:", C_BLACK);
    } else {
    vid_fillrect(g_field.x, g_field.y, g_field.w, g_field.h, C_WHITE);
    ui_sink(g_field.x, g_field.y, g_field.w, g_field.h);
    {
        int maxch = (g_field.w - 6) / FONT_ADV;
        int len   = (int)strlen(buf);
        const char *show = (len > maxch) ? buf + (len - maxch) : buf;
        font_draw(g_field.x + 3, g_field.y + 3, show, C_BLACK);
        vid_vline(g_field.x + 3 + font_text_width(show), g_field.y + 2,
                  g_field.h - 4, C_BLACK);
    }
    }
    ui_button(&g_up, "Up", FALSE);

    /* The listing. */
    vid_fillrect(g_list.x, g_list.y, g_list.w, g_list.h, C_WHITE);
    ui_sink(g_list.x, g_list.y, g_list.w, g_list.h);
    for (i = 0; i < g_vis && g_top + i < g_n; ++i) {
        int k  = g_top + i;
        int ty = g_list.y + 2 + i * g_row_h;
        u8  fg = C_BLACK;
        ent_near(k, nm);
        if (k == g_sel) {
            vid_fillrect(g_list.x + 1, ty - 1, g_list.w - 2, g_row_h, C_TITLE);
            fg = C_WHITE;
        }
        if (g_ent[k].is_dir) {         /* a folder tab, then the name       */
            vid_fillrect(g_list.x + 3, ty + 1, 4, 2, C_YELLOW);
            vid_fillrect(g_list.x + 3, ty + 3, 7, fh - 4, C_DKYELLOW);
            font_draw(g_list.x + 14, ty, nm, fg);
        } else {
            font_draw(g_list.x + 14, ty, nm, fg);
        }
    }
    if (g_n > g_vis)
        ui_vscroll(&g_sb_up, &g_sb_dn, &g_sb_tr, g_top, g_vis, g_n);

    /* The folder path along the foot, elided from the FRONT so the part
       that identifies it survives - followed by a marker when the scan
       ran out of table and stopped listing.

       The budget is the room before the Save button, not the width of
       the whole box: the old figure was (box.w - 16), which is most of
       forty characters, so a deep path was "elided" to a length that
       then ran underneath both buttons. */
    {
        const char *more = g_full ? " +more" : "";
        int avail = (g_ok.x - 6 - (g_box.x + 6)) / FONT_ADV;
        int maxb  = avail - (int)strlen(more);
        int dl    = (int)strlen(g_dir);
        int endx;
        if (maxb < 4) maxb = 4;
        if (dl <= maxb) {
            font_draw(g_box.x + 6, g_ok.y + 3, g_dir, C_DKGRAY);
            endx = g_box.x + 6 + dl * FONT_ADV;
        } else {
            font_draw(g_box.x + 6, g_ok.y + 3, "..", C_DKGRAY);
            font_draw(g_box.x + 6 + 2 * FONT_ADV, g_ok.y + 3,
                      g_dir + (dl - (maxb - 2)), C_DKGRAY);
            endx = g_box.x + 6 + maxb * FONT_ADV;
        }
        if (g_full)
            font_draw(endx, g_ok.y + 3, more, C_RED);
    }
    ui_button(&g_ok, g_dir_mode ? "Use" : (saving ? "Save" : "Open"), FALSE);
    ui_button(&g_cancel, "Cancel", FALSE);
}

/* ---- the modal loop -------------------------------------------------- */

/* Changing folder clears the name field when OPENING and keeps it when
   SAVING.  Opening, the name refers to a file in the folder just left,
   so carrying it forward would name something that is not there.  Saving,
   the name is the file about to be CREATED and has nothing to do with
   which folder is on screen - and the caller has usually pre-filled it
   (Copy suggests COPY_OF.TXT, Move suggests the file's own name).
   Clearing it unconditionally meant "browse to the destination" threw
   away the suggestion on the way, so arriving in the target folder and
   pressing Save did nothing at all: an empty name is not accepted.
   Enter already behaved this way - it takes a non-empty name over the
   selection - so the two routes into a folder had disagreed. */
static void clear_on_move(char *buf, bool_t saving)
{
    if (!saving)
        buf[0] = '\0';
}

/* Has the user EDITED the name field since arriving in this folder?
   Enter has to choose between "open what I typed" and "go into the
   highlighted folder", and the field alone cannot tell them apart: a
   caller-supplied suggestion (Copy offers COPY_OF.TXT, Move the file's
   own name) looks exactly like something the user typed.  Enter used to
   take any non-empty field as a typed name, so in Save mode - where the
   field is pre-filled before the dialog is even on screen - it accepted
   immediately and there was NO keyboard route into a subfolder at all.
   Tracking the edit separates the two: untouched field, Enter follows
   the highlight; touched, Enter takes the name. */
static bool_t g_typed;

/* Go into list entry k: a drive in drive view, a folder otherwise.  Both
   routes into a folder - Enter and the click - come through here, which
   is what stopped them disagreeing about whether to keep the name. */
static void enter_entry(int k, char *buf, bool_t saving)
{
    char nm[FD_NAME];
    ent_near(k, nm);
    if (g_drv_view) {
        unsigned total;
        int drive1 = nm[0] - 'A' + 1;
        if (!drive_valid(drive1))
            return;                    /* the floppy went away again    */
        _dos_setdrive((unsigned)drive1, &total);
        g_drv_view = FALSE;
    } else {
        chdir(nm);
    }
    scan();
    clear_on_move(buf, saving);
}

/* Go up: out of a folder, or - already at a drive root - out to the
   drive list, the level My Computer occupies elsewhere in the shell. */
static void go_up(char *buf, bool_t saving)
{
    if (g_drv_view)
        return;                        /* nothing above the drive list  */
    if (at_root(g_dir))
        g_drv_view = TRUE;
    else
        chdir("..");
    scan();
    clear_on_move(buf, saving);
}

static void join(char *out, int cap, const char *dir, const char *name)
{
    int n = 0;
    if (is_absolute(name)) {
        while (*name != '\0' && n < cap - 1) out[n++] = *name++;
        out[n] = '\0';
        return;
    }
    while (dir[n] != '\0' && n < cap - 2) { out[n] = dir[n]; ++n; }
    if (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/' && out[n - 1] != ':')
        out[n++] = '\\';
    while (*name != '\0' && n < cap - 1) out[n++] = *name++;
    out[n] = '\0';
}


static bool_t filedlg_run(const char *title, const char *pattern,
                          char *path, int cap, bool_t saving)
{
    char home[FD_PLEN], buf[FD_NAME];
    unsigned home_drive, dtotal;
    bool_t done = FALSE, ok = FALSE, redraw = TRUE;
    int prev_mx, prev_my;
    int i;

    /* Browse without disturbing the caller's directory. */
    getcwd(home, (int)sizeof(home));
    _dos_getdrive(&home_drive);        /* the DRIVE as well as the path  */

    g_pat[0] = '\0';
    if (pattern != NULL && pattern[0] != '\0') {
        for (i = 0; pattern[i] != '\0' && i < (int)sizeof(g_pat) - 1; ++i)
            g_pat[i] = pattern[i];
        g_pat[i] = '\0';
    } else {
        strcpy(g_pat, "*.*");
    }
    buf[0] = '\0';
    if (path != NULL) {                /* seed the field with the name only */
        int n = (int)strlen(path), s = n;
        while (s > 0 && path[s - 1] != '\\' && path[s - 1] != '/' &&
               path[s - 1] != ':')
            --s;
        for (i = 0; path[s + i] != '\0' && i < FD_NAME - 1; ++i)
            buf[i] = path[s + i];
        buf[i] = '\0';
    }

    scan();
    layout();
    dialog_note_takeover();        /* the caller must repaint in full */
    music_sfx(880, 2);
    /* Swallow the press that opened us.  The COUNT is what matters, not
       the button level: see the loop below. */
    (void)mouse_take_lpresses();
    g_typed    = FALSE;            /* a pre-filled name is not an edit  */
    g_drv_view = FALSE;            /* always open on the caller's folder */
    prev_mx = mouse_x();
    prev_my = mouse_y();

    while (!done) {
        int key, mx, my, nkey;
        bool_t downp;

        music_sfx_service();
        mouse_update();
        mx = mouse_x(); my = mouse_y();
        /* The driver's LATCHED press count, not an edge on the polled
           button level.  Polling the level, this loop dropped clicks
           outright: measured A/B under DOSBox with one harness and one
           script, two clicks a second apart on a folder row registered
           as ZERO with the level poll and selected-then-descended with
           the counter.  INT 33h function 5 counts presses in hardware
           so they survive a frame the loop spent elsewhere, which is why
           main() and the rest of the shell have always used it; the
           picker was the one place still reading the level, and it
           drained this same counter on the way out having never read it.

           Worth recording what this is NOT, because the obvious story is
           wrong.  The loop halts in sys_idle(), so the tempting
           explanation is that a click began and ended between two
           samples.  But dialog.c polls the level the same way and does
           not drop the same click - built without its equivalent fix and
           tested with the same harness, a fast click on its OK button
           dismissed it every time.  So the poll interval alone does not
           account for this, and whatever the remaining difference
           between the two loops is, it is not yet identified.  The
           counter is the right thing to read regardless: it cannot miss
           a press, and it is what the rest of the shell reads. */
        downp = (mouse_take_lpresses() > 0) ? TRUE : FALSE;

        /* Drained, like dialog.c and main.c: the picker is where a name
           is TYPED, so a key dropped because the frame was slow is a
           file saved under the wrong name. */
        nkey = 0;
        while (nkey++ < 16 && !done && (key = kb_poll()) != KEY_NONE) {
            redraw = TRUE;
            if (key == KEY_ESC) { done = TRUE; }
            else if (key == KEY_UP   && g_sel > 0)       --g_sel;
            else if (key == KEY_DOWN && g_sel < g_n - 1) ++g_sel;
            else if (key == KEY_PGUP) {
                g_sel -= g_vis; if (g_sel < 0) g_sel = 0;
            } else if (key == KEY_PGDN) {
                g_sel += g_vis; if (g_sel > g_n - 1) g_sel = g_n - 1;
            } else if (key == KEY_HOME) g_sel = 0;
            else if (key == KEY_END)    g_sel = (g_n > 0) ? g_n - 1 : 0;
            else if (key == KEY_BACK) {
                int n = (int)strlen(buf);
                if (n > 0) { buf[n - 1] = '\0'; g_typed = TRUE; }
                else {
                    go_up(buf, saving);
                    g_typed = FALSE;
                }
            } else if (key == KEY_ENTER) {
                if (g_drv_view) {
                    /* "Drives" is not a folder anything can be written
                       into, and it was treated as one: a typed name here
                       composed the path "Drives\\NAME" and failed at
                       fopen with "Could not write file".  You reach this
                       view by backspacing an empty name field one press
                       too far, which is easy to do.  Enter picks the
                       highlighted DRIVE instead, and clear_on_move keeps
                       the typed name for the folder you land in. */
                    if (g_n > 0)
                        enter_entry(g_sel, buf, saving);
                } else if (g_typed && buf[0] != '\0') {   /* an edited name wins  */
                    done = TRUE; ok = TRUE;
                } else if (g_n > 0 && g_ent[g_sel].is_dir) {
                    enter_entry(g_sel, buf, saving);
                    g_typed = FALSE;
                } else if (buf[0] != '\0') {
                    done = TRUE; ok = TRUE;
                } else if (g_n > 0) {
                    ent_near(g_sel, buf);
                    done = TRUE; ok = TRUE;
                }
            } else if (key >= 32 && key < 127 && !g_dir_mode) {
                int n = (int)strlen(buf);
                if (n < FD_NAME - 1) { buf[n] = (char)key; buf[n + 1] = '\0'; }
                g_typed = TRUE;
            }
        }

        if (downp) {
            redraw = TRUE;
            if (rect_contains(&g_cancel, mx, my)) { done = TRUE; }
            else if (rect_contains(&g_ok, mx, my)) {
                if (g_drv_view) {          /* not a folder - see KEY_ENTER   */
                    if (g_n > 0)
                        enter_entry(g_sel, buf, saving);
                } else if (g_dir_mode) {   /* the folder itself is the answer */
                    done = TRUE; ok = TRUE;
                } else {
                    if (buf[0] == '\0' && g_n > 0 && !g_ent[g_sel].is_dir)
                        ent_near(g_sel, buf);
                    if (buf[0] != '\0') { done = TRUE; ok = TRUE; }
                }
            } else if (rect_contains(&g_up, mx, my)) {
                go_up(buf, saving);
                g_typed = FALSE;
            } else if (g_n > g_vis && rect_contains(&g_sb_up, mx, my)) {
                if (g_top > 0) --g_top;
            } else if (g_n > g_vis && rect_contains(&g_sb_dn, mx, my)) {
                if (g_top < g_n - g_vis) ++g_top;
            } else if (rect_contains(&g_list, mx, my) && g_n > 0) {
                int row = (my - (g_list.y + 1)) / g_row_h;
                int k   = g_top + row;
                if (row >= 0 && row < g_vis && k < g_n) {
                    if (k == g_sel) {          /* a second click activates  */
                        if (g_ent[k].is_dir) {
                            enter_entry(k, buf, saving);
                            g_typed = FALSE;
                        } else {
                            char nm[FD_NAME];
                            ent_near(k, nm);
                            strcpy(buf, nm); done = TRUE; ok = TRUE;
                        }
                    } else {
                        g_sel = k;
                        /* Picking a row REPLACES the field, so whatever
                           was typed before is gone and the field is once
                           again the selection's name, not an edit. */
                        if (!g_ent[k].is_dir) { ent_near(k, buf); g_typed = FALSE; }
                    }
                }
            }
        }

        /* Keep the selection in view (the arrows above moved it). */
        if (g_sel < g_top)          g_top = g_sel;
        if (g_sel >= g_top + g_vis) g_top = g_sel - g_vis + 1;
        if (g_top > g_n - g_vis)    g_top = g_n - g_vis;
        if (g_top < 0)              g_top = 0;

        if (redraw) {
            mouse_erase();
            draw_dlg(title, buf, saving);
            vid_blit_rect(g_box.x, g_box.y, g_box.w, g_box.h);
            mouse_draw();
            redraw = FALSE;
            prev_mx = mx; prev_my = my;
        } else if (mx != prev_mx || my != prev_my) {
            mouse_erase();
            mouse_draw();
            prev_mx = mx; prev_my = my;
        }
        if (!done)
            sys_idle();
    }

    if (ok && path != NULL) {
        if (g_dir_mode) {              /* the browsed folder, as it stands  */
            int n = 0;
            while (g_dir[n] != '\0' && n < cap - 1) { path[n] = g_dir[n]; ++n; }
            path[n] = '\0';
        } else {
            join(path, cap, g_dir, buf);
        }
    }
    /* Leave the caller exactly where we found it - drive included.  In
       DOS chdir("C:\\DOCS") sets drive C's current directory and does NOT
       make C the current drive, so restoring the path alone would have
       left the whole shell sitting on whatever drive the user had
       browsed to and then cancelled out of. */
    _dos_setdrive(home_drive, &dtotal);
    chdir(home);

    /* Every mouse_update() above accrued INT 33h press counts the main
       loop would otherwise replay as fresh desktop clicks - the same
       drain dialog.c does on the way out. */
    (void)mouse_take_lpresses();
    (void)mouse_take_rpresses();
    return ok;
}

bool_t filedlg(const char *title, const char *pattern,
               char *path, int cap, bool_t saving)
{
    g_dir_mode = FALSE;
    return filedlg_run(title, pattern, path, cap, saving);
}

bool_t filedlg_folder(const char *title, char *path, int cap)
{
    bool_t r;
    g_dir_mode = TRUE;
    r = filedlg_run(title, "*.*", path, cap, FALSE);
    g_dir_mode = FALSE;            /* never leave the flag set behind us */
    return r;
}
