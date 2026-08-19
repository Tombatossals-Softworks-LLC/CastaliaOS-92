/* ======================================================================
 * scrap.c - Scrap Box text editor applet for CASTALIA/386 (v0.5)
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "scrap.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "dialog.h"
#include "keyboard.h"
#include "filedlg.h"
#include "textscan.h"  /* text_needle: the same fold Find File uses */

/* FAR, and four times the size.  The document buffer was the single
   largest thing left in DGROUP - 4096 bytes of a 57344-byte segment the
   memory gate had at 93% - and it bought a notepad that could not hold
   a README.  Out in far data it costs DGROUP nothing and holds 16 KB.

   The price is that the medium model passes a plain char * as NEAR, so
   fread, fwrite and memmove cannot see this buffer: the three places
   that used them go through a small near staging buffer or a loop.
   Nothing else changes - the editor indexes g_buf[i], which the
   compiler resolves through the right segment, and the draw hands
   font_draw_char a CHARACTER rather than a pointer into the text, so it
   never had a near-pointer problem to begin with. */
#define SCRAP_MAX   16384
#define SCRAP_LINES 640
#define SCRAP_IO    512            /* the near staging buffer, load/save */

static char far g_buf[SCRAP_MAX];
static int  g_len    = 0;
static int  g_cursor = 0;
static int  g_scroll = 0;
static char g_file[132] = "";      /* full doc_path: cwd (79) + name    */
/* TRUE when the file on disk was longer than the buffer.  The Scrap Box
   is the default handler for .TXT/.DOC/.ME/.LOG/.INI, so opening
   README.TXT from the Disk Cabinet and pressing Save used to write back
   only the first 4095 bytes and destroy the rest.  Same rule the day
   planner follows: never rewrite a file we did not fully read. */
static bool_t g_clipped = FALSE;

/* TRUE while the buffer holds edits that are not on disk.  Without it the
   New button, the Load box and closing the window all threw the user's
   typing away in silence. */
static bool_t g_dirty = FALSE;

/* Ask before discarding unsaved text; TRUE = go ahead. */
static bool_t ok_to_discard(void)
{
    if (!g_dirty)
        return TRUE;
    return (dialog_confirm("Scrap Box", "Discard the unsaved changes",
                           "in this document?") == DLG_YES) ? TRUE : FALSE;
}

void scrap_flush_state(void) { g_dirty = FALSE; }
bool_t scrap_is_dirty(void)  { return g_dirty; }

/* Wrapped-line start indices, recomputed by wrap(). */
static int far g_lstart[SCRAP_LINES];   /* far: DGROUP diet */
static int  g_nlines = 1;
static int  g_cols   = 32;    /* columns that fit; set during draw        */
static int  g_rows   = 8;     /* visible rows; set during draw            */
/* Follow the caret only when the KEYBOARD moved it.  Snapping the view
   to the caret on EVERY draw is what made the scroll arrows below do
   nothing: the click scrolled a line and the very next compose put it
   straight back.  drawer.c carries a comment about the identical bug -
   third module to walk into it, so this one is written down too. */
static bool_t g_follow = TRUE;

/* ---- buffer ---------------------------------------------------------- */
void scrap_new(void)
{
    g_len = 0; g_cursor = 0; g_scroll = 0;
    g_buf[0] = '\0';
    g_clipped = FALSE;
    /* And the FILENAME.  A new document that still remembers where the
       last one came from is a trap: the toolbar went on showing
       C:\DOC.TXT over an empty page, and Save pre-fills the picker with
       g_file - so accepting the suggested name overwrote the document
       you had just finished reading with nothing at all.
       Safe to clear here because scrap_open() calls scrap_new() FIRST
       and sets the name afterwards. */
    g_file[0] = '\0';
}

/* TRUE when it is safe to replace the buffer (public so the Disk
   Cabinet's file association can ask before it throws work away). */
bool_t scrap_ok_to_replace(void) { return ok_to_discard(); }

void scrap_open(const char *path)
{
    FILE *f;
    int n, i, w;
    /* Copy the path BEFORE the reset.  scrap_new() clears g_file - and
       act_load() hands filedlg() g_file as its buffer and then passes
       THE SAME g_file in here as `path`, so by the time this function
       read it, it was the empty string it had just written.  The Load
       button and F3 did nothing whatever: no error, no file, no change.

       The comment on that clear says it is "safe because scrap_open()
       calls scrap_new() FIRST and sets the name afterwards", which is
       true of every caller that passes its own buffer and false of the
       one that passes g_file.  Fixed here rather than in act_load, so
       the next caller to do the obvious thing is safe too. */
    char want[sizeof(g_file)];
    i = 0;
    if (path != NULL)
        while (path[i] != '\0' && i < (int)sizeof(want) - 1) {
            want[i] = path[i];
            ++i;
        }
    want[i] = '\0';
    scrap_new();                       /* also clears the dirty flag      */
    if (want[0] == '\0')
        return;
    f = fopen(want, "rb");
    if (f == NULL)
        return;
    /* Read through a near buffer, stripping the CRs on the way in: DOS
       files are CRLF, the editor speaks bare LF, and an unstripped CR
       used to render as the unknown-glyph box.  One pass rather than a
       read followed by a compaction, since the copy has to happen
       anyway to cross into far memory.  n doubles as "overflowed". */
    n = 0; w = 0;
    for (;;) {
        char   tmp[SCRAP_IO];
        size_t got = fread(tmp, 1, sizeof(tmp), f);
        if (got == 0)
            break;
        for (i = 0; i < (int)got; ++i) {
            if (tmp[i] == '\r')
                continue;
            if (w >= SCRAP_MAX - 1) { n = 1; break; }
            g_buf[w++] = tmp[i];
        }
        if (n)
            break;
    }
    /* Anything left in the file did not fit. */
    g_clipped = (n || fgetc(f) != EOF) ? TRUE : FALSE;
    fclose(f);
    g_len = w;
    /* Remember where it came from, so Save writes straight back and the
       toolbar shows the name instead of "(untitled)". */
    for (i = 0; want[i] != '\0' && i < (int)sizeof(g_file) - 1; ++i)
        g_file[i] = want[i];
    g_file[i] = '\0';
}

/* TRUE only when the bytes really reached the disk.  This used to return
   void and the caller cleared the dirty flag unconditionally: a write-
   protected floppy or a full disk showed an error box AND marked the
   document saved, so the close prompt never fired and the work was lost
   at the very moment the user had been told something went wrong. */
static bool_t scrap_save(const char *path)
{
    FILE *f;
    if (g_clipped) {
        dialog_message("Save", "This file is longer than the",
                       "Scrap Box holds - not saving.");
        return FALSE;
    }
    /* Text mode: the C library re-expands our bare LFs to DOS CRLF. */
    f = fopen(path, "w");
    if (f == NULL) {
        dialog_message("Save", "Could not write file.", path);
        return FALSE;
    }
    /* Both of these mean the file ON DISK is damaged, and neither said
       so.  fopen("w") truncated it before a byte was written, so the
       version that was there is already gone - and "the disk is full"
       reads like nothing happened, which is the opposite of the truth.
       The document itself is untouched in memory and still dirty, so
       the honest instruction is "save it somewhere else".

       Not removed, unlike a failed copy: a copy has its source intact
       beside it and a truncated binary under a real name is worse than
       none, while most of a text file is most of the user's work. */
    {
        int    off = 0;
        bool_t io_bad = FALSE;
        while (off < g_len) {
            char tmp[SCRAP_IO];
            int  k, run = g_len - off;
            if (run > SCRAP_IO) run = SCRAP_IO;
            for (k = 0; k < run; ++k)
                tmp[k] = g_buf[off + k];
            if (fwrite(tmp, 1, (size_t)run, f) != (size_t)run) {
                io_bad = TRUE;
                break;
            }
            off += run;
        }
        if (io_bad || ferror(f)) {
            fclose(f);
            dialog_message("Save", "Disk full - the file on disk",
                           "is cut short.  Save elsewhere.");
            return FALSE;
        }
    }
    if (fclose(f) != 0) {
        dialog_message("Save", "The file on disk is cut short.",
                       "Save it somewhere else.");
        return FALSE;
    }
    return TRUE;
}

/* ---- wrapping -------------------------------------------------------- */
static void wrap(int cols)
{
    int i, col;
    if (cols < 1) cols = 1;
    g_cols = cols;
    g_nlines = 0;
    g_lstart[g_nlines++] = 0;
    col = 0;
    for (i = 0; i < g_len; ++i) {
        if (g_buf[i] == '\n') {
            if (g_nlines < SCRAP_LINES) g_lstart[g_nlines++] = i + 1;
            col = 0;
        } else {
            ++col;
            if (col >= cols) {
                if (g_nlines < SCRAP_LINES) g_lstart[g_nlines++] = i + 1;
                col = 0;
            }
        }
    }
}

static int line_of(int pos)
{
    int l = 0;
    while (l + 1 < g_nlines && g_lstart[l + 1] <= pos)
        ++l;
    return l;
}

static int line_end(int l)
{
    return (l + 1 < g_nlines) ? g_lstart[l + 1] : g_len;
}

/* Visible length of a line (excludes a trailing newline). */
static int line_len(int l)
{
    int e = line_end(l);
    if (e > g_lstart[l] && g_buf[e - 1] == '\n')
        --e;
    return e - g_lstart[l];
}

/* ---- layout ---------------------------------------------------------- */
#define TB_N 3
static const char *TB_LBL[TB_N] = { "New", "Load", "Save" };

static void tb_rect(const Rect *c, int i, Rect *r)
{
    int bw = font_adv() * 5;
    int bh = font_h() + 5;
    rect_set(r, c->x + i * (bw + 1), c->y, bw, bh);
}

/* The scroll strip down the right of the text.  The Scrap Box was the
   one scrolling view in the shell without one - the Disk Cabinet, the
   Agenda, Find File and the drawer all have it - and at 4 KB you could
   just about live with arrows only.  At 16 KB you cannot, and nothing
   on screen said how much document there was. */
#define SSCROLL_W 12

static void area_rect(const Rect *c, Rect *a)
{
    int top = font_h() + 5 + 1;
    rect_set(a, c->x, c->y + top, c->w - SSCROLL_W, c->h - top);
}

static void sscroll_rects(const Rect *a, Rect *up, Rect *dn, Rect *tr)
{
    int bw = SSCROLL_W;
    rect_set(up, a->x + a->w, a->y, bw, bw);
    rect_set(dn, a->x + a->w, a->y + a->h - bw, bw, bw);
    rect_set(tr, a->x + a->w, a->y + bw, bw, a->h - 2 * bw);
    if (tr->h < 0) tr->h = 0;
}

/* ---- drawing --------------------------------------------------------- */
void scrap_draw(const Rect *client)
{
    Rect a, r;
    int i, lineh, cols, line, row;

    for (i = 0; i < TB_N; ++i) {
        tb_rect(client, i, &r);
        ui_button(&r, TB_LBL[i], FALSE);
    }
    /* File name to the right of the toolbar.  When the path does not fit
       it is the LAST component that identifies the file, and font_draw_n
       cuts from the right - so "C:\\DOCS\\LETTERS\\NOTES.TXT" appeared as
       "C:\\DOCS\\LETTERS\\NOTE", dropping the only part worth reading.
       Fall back to the base name, which is what the picker shows, what
       dialog.c keeps when it elides, and what the Sketch Pad now puts in
       its title bar. */
    {
        int fx  = client->x + TB_N * (font_adv() * 5 + 1) + 4;
        int max = (client->x + client->w - fx) / FONT_ADV;
        const char *nm = g_file;
        if (g_file[0] != '\0' && (int)strlen(g_file) > max) {
            int i;
            for (i = 0; g_file[i] != '\0'; ++i)
                if (g_file[i] == '\\' || g_file[i] == '/' || g_file[i] == ':')
                    nm = g_file + i + 1;
        }
        font_draw_n(fx, client->y + 3,
                    g_clipped ? "TRUNCATED - will not save"
                              : (g_file[0] ? nm : "(untitled)"),
                    max, g_clipped ? C_RED : C_DKGRAY);
    }

    area_rect(client, &a);
    vid_fillrect(a.x, a.y, a.w, a.h, C_WHITE);
    ui_sink(a.x, a.y, a.w, a.h);

    lineh = font_h() + 1;
    cols  = (a.w - 6) / FONT_ADV;
    g_rows = (a.h - 4) / lineh;
    if (g_rows < 1) g_rows = 1;
    wrap(cols);

    /* Keep the cursor's line in view - when the caret is what moved. */
    if (g_follow) {
        line = line_of(g_cursor);
        if (line < g_scroll) g_scroll = line;
        if (line >= g_scroll + g_rows) g_scroll = line - g_rows + 1;
        g_follow = FALSE;
    }
    /* Clamped every pass either way: the wrap width changes with the
       window, and a narrower window makes more lines out of the same
       text - g_scroll from the wider one can be past the end. */
    if (g_scroll > g_nlines - g_rows) g_scroll = g_nlines - g_rows;
    if (g_scroll < 0) g_scroll = 0;

    for (row = 0; row < g_rows; ++row) {
        int l = g_scroll + row;
        int j, x, y;
        if (l >= g_nlines)
            break;
        x = a.x + 3;
        y = a.y + 2 + row * lineh;
        /* The last tracked line of an overfull buffer can run far past
           the wrap width; bound the row or it paints across the frame. */
        for (j = g_lstart[l];
             j < line_end(l) && j - g_lstart[l] < g_cols; ++j) {
            if (g_buf[j] == '\n')
                break;
            font_draw_char(x, y, g_buf[j], C_BLACK);
            x += FONT_ADV;
        }
    }

    /* Caret. */
    {
        int cl = line_of(g_cursor);
        if (cl >= g_scroll && cl < g_scroll + g_rows) {
            int col = g_cursor - g_lstart[cl];
            int cxp, cyp;
            if (col > g_cols) col = g_cols;
            cxp = a.x + 3 + col * FONT_ADV;
            cyp = a.y + 2 + (cl - g_scroll) * lineh;
            vid_vline(cxp, cyp, font_h(), C_BLACK);
        }
    }

    /* The strip LAST, from the values this pass settled on.  Drawn any
       earlier it reports the previous frame's g_rows and g_nlines - the
       thumb would lag a keystroke behind the text, which is precisely
       the geometry-computed-twice trap files.c carries a comment about. */
    {
        Rect up, dn, tr;
        sscroll_rects(&a, &up, &dn, &tr);
        ui_vscroll(&up, &dn, &tr, g_scroll, g_rows, g_nlines);
    }
}

/* ---- interaction ----------------------------------------------------- */
static void insert_char(char ch)
{
    if (g_len >= SCRAP_MAX - 1)
        return;
    g_dirty = TRUE;
    /* Backwards, and by hand: memmove takes a NEAR pointer and g_buf is
       far, so the offsets alone would have gone over with the wrong
       segment - silently, and at -we. */
    {
        int k;
        for (k = g_len; k > g_cursor; --k)
            g_buf[k] = g_buf[k - 1];
    }
    g_buf[g_cursor] = ch;
    ++g_len;
    ++g_cursor;
}

static void delete_at(int pos)
{
    if (pos < 0 || pos >= g_len)
        return;
    g_dirty = TRUE;      /* an edit made only of Backspace is still an edit */
    {
        int k;
        for (k = pos; k < g_len - 1; ++k)
            g_buf[k] = g_buf[k + 1];
    }
    --g_len;
}

/* New / Load / Save, factored out of the toolbar handler so the keyboard
   can reach them.  They were click-only: without a mouse driver you
   could type a document into the Scrap Box and had no way to SAVE it.
   Every other key in the editor worked; the three that matter did not. */
static void act_new(void)
{
    if (ok_to_discard()) { scrap_new(); g_dirty = FALSE; }
}

static void act_load(void)
{
    if (ok_to_discard() &&
        /* Every extension the association table sends HERE, not just
           one of them.  main.c's assoc_app_for routes txt, doc, me, log,
           ini and asc to the Scrap Box, so opening README.ME from the
           Disk Cabinet lands you in an editor whose own Load box then
           refuses to show you another .ME.  Same shape as the
           Gramophone's picker offering CASTALIA.EXE.
           SAVE still offers *.TXT alone: that field is for naming a new
           document, and .TXT is the answer nine times in ten. */
        filedlg("Open a document", "*.TXT;*.DOC;*.ME;*.LOG;*.INI;*.ASC",
                g_file, (int)sizeof(g_file), FALSE)) {
        scrap_open(g_file);
        g_dirty = FALSE;               /* a freshly loaded file is clean   */
    }
}

/* ---- find ------------------------------------------------------------
 * A 4 KB notepad could be read by scrolling.  16 KB cannot, so the
 * document that grew in 0.56 needed the one editor feature that is used
 * more than anything but typing.
 *
 * Case-insensitive, through text_needle() - the same fold Find File
 * uses on a file, so "himem" finds HIMEM in both places and there is
 * one answer in the program to what matching means.
 * -------------------------------------------------------------------- */
#define FIND_LEN 24
static char g_find[FIND_LEN] = "";

static void act_find(bool_t again)
{
    char needle[FIND_LEN];
    int  nlen, span, start, k, i, j;

    if (!again || g_find[0] == '\0') {
        if (dialog_input("Find", "Text to find:", g_find,
                         (int)sizeof(g_find)) != DLG_OK || g_find[0] == '\0')
            return;
    }
    nlen = text_needle(needle, (int)sizeof(needle), g_find);
    span = g_len - nlen + 1;           /* candidate start positions */
    if (nlen <= 0 || span < 1) {
        dialog_message("Find", "Not found.", g_find);
        return;
    }
    /* One PAST the caret, so a repeat advances instead of finding the
       same match again, and wrapping once so the last match leads back
       round to the first. */
    start = g_cursor + 1;
    if (start >= span) start = 0;
    for (k = 0; k < span; ++k) {
        i = start + k;
        if (i >= span) i -= span;      /* k < span, so one subtraction */
        for (j = 0; j < nlen; ++j) {
            char c = g_buf[i + j];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 32);
            if (c != needle[j])
                break;
        }
        if (j == nlen) {
            g_cursor = i;
            g_follow = TRUE;           /* scroll the hit into view */
            return;
        }
    }
    dialog_message("Find", "Not found.", g_find);
}

static void act_save(void)
{
    if (filedlg("Save the document", "*.TXT", g_file,
                (int)sizeof(g_file), TRUE) &&
        scrap_save(g_file))
        g_dirty = FALSE;               /* only when the write succeeded    */
}

bool_t scrap_click(const Rect *client, int mx, int my)
{
    Rect r, a;
    int i, lineh, row, col, l;

    for (i = 0; i < TB_N; ++i) {
        tb_rect(client, i, &r);
        if (rect_contains(&r, mx, my)) {
            if      (i == 0) act_new();
            else if (i == 1) act_load();
            else             act_save();
            return TRUE;
        }
    }

    area_rect(client, &a);
    {   /* The strip first: it lives just outside the text rect. */
        Rect up, dn, tr;
        sscroll_rects(&a, &up, &dn, &tr);
        if (rect_contains(&up, mx, my)) {
            if (g_scroll > 0) { --g_scroll; return TRUE; }
            return FALSE;
        }
        if (rect_contains(&dn, mx, my)) {
            if (g_scroll < g_nlines - g_rows) { ++g_scroll; return TRUE; }
            return FALSE;
        }
        if (rect_contains(&tr, mx, my)) {
            /* Page towards the click, as the Disk Cabinet's track does. */
            int mid = tr.y + tr.h / 2;
            int ns  = g_scroll + ((my < mid) ? -g_rows : g_rows);
            if (ns > g_nlines - g_rows) ns = g_nlines - g_rows;
            if (ns < 0) ns = 0;
            if (ns == g_scroll) return FALSE;
            g_scroll = ns;
            return TRUE;
        }
    }
    if (mx < a.x || mx >= a.x + a.w || my < a.y || my >= a.y + a.h)
        return FALSE;

    lineh = font_h() + 1;
    row = (my - a.y - 2) / lineh;
    col = (mx - a.x - 3 + FONT_ADV / 2) / FONT_ADV;
    if (col < 0) col = 0;
    l = g_scroll + row;
    if (l < 0) l = 0;
    if (l >= g_nlines) l = g_nlines - 1;
    if (col > g_cols) col = g_cols;    /* the overfull last line again     */
    if (col > line_len(l)) col = line_len(l);
    g_cursor = g_lstart[l] + col;
    if (g_cursor > g_len) g_cursor = g_len;
    return TRUE;
}

bool_t scrap_key(int key)
{
    /* Every key this function acts on moves the caret or edits the text,
       so every one of them wants the view to follow. */
    g_follow = TRUE;
    if (key >= 32 && key < 127) { insert_char((char)key); return TRUE; }

    switch (key) {
    case 13:                                  /* Enter                     */
        insert_char('\n'); return TRUE;
    case 8:                                    /* Backspace                 */
        if (g_cursor > 0) { --g_cursor; delete_at(g_cursor); }
        return TRUE;
    case KEY_DEL:
        delete_at(g_cursor); return TRUE;
    case KEY_LEFT:
        if (g_cursor > 0) --g_cursor; return TRUE;
    case KEY_RIGHT:
        if (g_cursor < g_len) ++g_cursor; return TRUE;
    case KEY_HOME:
        g_cursor = g_lstart[line_of(g_cursor)]; return TRUE;
    case KEY_END:
        g_cursor = g_lstart[line_of(g_cursor)] + line_len(line_of(g_cursor));
        return TRUE;
    case KEY_UP: {
        int l = line_of(g_cursor);
        int col = g_cursor - g_lstart[l];
        if (l > 0) {
            int pl = l - 1, plen = line_len(pl);
            g_cursor = g_lstart[pl] + (col < plen ? col : plen);
        }
        return TRUE;
    }
    case KEY_DOWN: {
        int l = line_of(g_cursor);
        int col = g_cursor - g_lstart[l];
        if (l + 1 < g_nlines) {
            int nl = l + 1, nlen = line_len(nl);
            g_cursor = g_lstart[nl] + (col < nlen ? col : nlen);
        }
        return TRUE;
    }
    /* A page at a time.  The Scrap Box moved by one line and no more, so
       a document longer than the window could only be crossed by holding
       an arrow key down - in an editor whose own file dialog has had
       PgUp/PgDn since the day it was written.  g_rows is the visible row
       count the draw already publishes, so a page here is exactly the
       page the user is looking at. */
    case KEY_PGUP:
    case KEY_PGDN: {
        int l    = line_of(g_cursor);
        int col  = g_cursor - g_lstart[l];
        int step = (g_rows > 1) ? g_rows - 1 : 1;   /* keep a line of overlap */
        int nl   = (key == KEY_PGUP) ? l - step : l + step;
        int nlen;
        if (nl < 0)          nl = 0;
        if (nl >= g_nlines)  nl = g_nlines - 1;
        if (nl < 0)          return TRUE;           /* empty document        */
        nlen = line_len(nl);
        g_cursor = g_lstart[nl] + (col < nlen ? col : nlen);
        return TRUE;
    }
    /* Top and bottom of the document.  Home/End are the line's ends, so
       these are the two positions a long document otherwise has no key
       for at all. */
    case KEY_CTRL_HOME:
        g_cursor = 0; return TRUE;
    case KEY_CTRL_END:
        g_cursor = g_len; return TRUE;
    /* The toolbar.  F7 is "new" throughout the shell - a folder in the
       Disk Cabinet, a card in the Cardfile - and F2/F3 for Save/Open are
       what a DOS-era editor's keyboard did. */
    case KEY_F7: act_new();  return TRUE;
    case KEY_F3: act_load(); return TRUE;
    case KEY_F2: act_save(); return TRUE;
    /* F5 and F6 are the desktop's cascade and tile when no window claims
       them; the Disk Cabinet already takes both for Copy and Move, so
       this is the established trade rather than a new one. */
    case KEY_F5: act_find(FALSE); return TRUE;
    case KEY_F6: act_find(TRUE);  return TRUE;
    default:
        break;
    }
    return FALSE;
}
