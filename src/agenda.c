/* ======================================================================
 * agenda.c - The Agenda (to-do list) for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "agenda.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "dialog.h"
#include "music.h"
#include "system.h"    /* sys_home_path: pin AGENDA.TXT to the home dir  */

#define AG_MAX  14
#define AG_LEN  34
#define AG_FILE "AGENDA.TXT"

static char   g_text[AG_MAX][AG_LEN];
static bool_t g_done[AG_MAX];
static int    g_n      = 0;
static int    g_sel    = -1;
static bool_t g_loaded = FALSE;

static int  g_top = 0;                 /* first visible row (it scrolls)   */
static int  g_vis = 1;                 /* rows the last draw could show    */

static Rect g_add_b, g_del_b;
/* g_row_h starts at 1, not 0: agenda_click divides by it, and its gate
   (my >= g_row_y0) is `my >= 0` before the first draw - always true.  No
   path reaches it today, but a zero divisor on DOS is INT 0, and that is
   one refactor away rather than impossible. */
static int  g_row_y0 = 0, g_row_h = 1; /* list geometry for the hit test   */
static int  g_box_x1;                  /* right edge of the checkbox zone  */
static Rect g_list_r;                  /* the rows' rectangle, for the bar */
/* Follow the selection only when the KEYBOARD moved it - see drawer.c. */
static bool_t g_follow = FALSE;

#define AGSCROLL_W 12
static void ag_scroll_rects(Rect *up, Rect *dn, Rect *tr)
{
    int bw = AGSCROLL_W;
    rect_set(up, g_list_r.x + g_list_r.w, g_list_r.y, bw, bw);
    rect_set(dn, g_list_r.x + g_list_r.w,
             g_list_r.y + g_list_r.h - bw, bw, bw);
    rect_set(tr, g_list_r.x + g_list_r.w, g_list_r.y + bw,
             bw, g_list_r.h - 2 * bw);
    if (tr->h < 0) tr->h = 0;
}

/* Always next to CASTALIA.EXE - never in whatever directory the Disk
   Cabinet last browsed to (that used to scatter or lose the list). */
static FILE *ag_open(const char *mode)
{
    char p[80];
    sys_home_path(p, (int)sizeof(p), AG_FILE);
    return fopen(p, mode);
}

/* TRUE when the file on disk held more than this table can carry, or a
   line longer than one entry.  Rewriting it from the truncated table
   would DELETE the overflow permanently, so we refuse to write at all
   and say so instead. */
static bool_t g_clipped = FALSE;

/* Set by every ag_save() that does not reach the disk, cleared by every
   one that does.  The window's red banner already reports a clipped
   file, but it is only on screen while the window is - and the Agenda
   can be closed, or buried, long before Shut Down. */
static bool_t g_unsaved = FALSE;

bool_t agenda_unsaved(void) { return g_unsaved; }

static void ag_save(void)
{
    FILE *f;
    int i;
    if (g_clipped) {
        g_unsaved = TRUE;
        return;                        /* never overwrite what we lost     */
    }
    f = ag_open("w");
    if (f == NULL) {
        /* Silence here meant ticking a box LOOKED saved on a write-
           protected or full disk and never was. */
        g_unsaved = TRUE;
        dialog_message("Agenda", "Could not write AGENDA.TXT.",
                       "Disk full, or write-protected.");
        return;
    }
    for (i = 0; i < g_n; ++i)
        fprintf(f, "[%c] %s\n", g_done[i] ? 'x' : ' ', g_text[i]);
    {
        /* NOT "ferror(f) || fclose(f)": || short-circuits, so a write
           error skipped the fclose entirely and leaked the handle - on
           the very error path this check exists for. */
        int bad = ferror(f) ? 1 : 0;
        if (fclose(f) != 0)
            bad = 1;
        g_unsaved = bad ? TRUE : FALSE;
        if (bad)
            dialog_message("Agenda", "AGENDA.TXT was not written",
                           "in full - the disk may be full.");
    }
}

static void ag_load(void)
{
    FILE *f = ag_open("r");
    char line[80];

    g_n = 0;
    g_clipped = FALSE;
    g_unsaved = FALSE;        /* a reload replaces whatever was lost */
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL) {
        char *s = line;
        int   k = 0;
        bool_t done = FALSE;
        while (*s == ' ' || *s == '\t')
            ++s;
        if (s[0] == '[' && s[2] == ']') {          /* "[x] " or "[ ] "     */
            done = (s[1] == 'x' || s[1] == 'X') ? TRUE : FALSE;
            s += 3;
            while (*s == ' ')
                ++s;
        }
        /* Both questions - is there content, and is there room - before
           a byte is written.  The capacity test used to run first, so a
           full 14-item file ending in a blank line was flagged clipped
           and ag_save() became a permanent no-op; moving it after the
           copy then wrote g_text[14], one past the array. */
        if (*s == '\0' || *s == '\n' || *s == '\r')
            continue;                              /* skip blank lines     */
        if (g_n >= AG_MAX) {           /* a real item we cannot hold       */
            g_clipped = TRUE;
            break;
        }
        while (s[k] != '\0' && s[k] != '\n' && s[k] != '\r' &&
               k < AG_LEN - 1) {
            g_text[g_n][k] = s[k];
            ++k;
        }
        if (k == AG_LEN - 1 && s[k] != '\0' && s[k] != '\n' && s[k] != '\r')
            g_clipped = TRUE;          /* a line longer than one entry     */
        g_text[g_n][k] = '\0';
        g_done[g_n] = done;
        ++g_n;
    }
    fclose(f);
}

void agenda_open(void)
{
    g_top = 0;
    if (!g_loaded) {
        ag_load();
        g_loaded = TRUE;
    }
    if (g_sel >= g_n)
        g_sel = g_n - 1;
    /* Nothing was selected until an arrow key moved something, so on a
       freshly opened Agenda the two keys that DO anything - Space to
       tick, Del to remove - were both inert, and so were PgDn and End.
       Every other list in the shell opens on its first row. */
    if (g_sel < 0 && g_n > 0)
        g_sel = 0;
}

static void ag_add(void)
{
    static char buf[AG_LEN] = "";
    if (g_n >= AG_MAX) {
        dialog_message("Agenda", "The list is full.", "Check something off!");
        return;
    }
    buf[0] = '\0';
    if (dialog_input("Agenda", "New entry:", buf, sizeof(buf)) != DLG_OK ||
        buf[0] == '\0')
        return;
    strcpy(g_text[g_n], buf);
    g_done[g_n] = FALSE;
    g_sel = g_n++;
    ag_save();
}

static void ag_delete(void)
{
    int i;
    if (g_sel < 0 || g_sel >= g_n)
        return;
    /* Deleting a file asks; deleting a to-do item did not, from either
       the button or the Del key, and there is no undo. */
    if (dialog_confirm("Agenda", "Delete this item?",
                       g_text[g_sel]) != DLG_YES)
        return;
    for (i = g_sel; i < g_n - 1; ++i) {
        strcpy(g_text[i], g_text[i + 1]);
        g_done[i] = g_done[i + 1];
    }
    --g_n;
    if (g_sel >= g_n)
        g_sel = g_n - 1;
    ag_save();
}

static void ag_toggle(int i)
{
    if (i < 0 || i >= g_n)
        return;
    g_done[i] = g_done[i] ? FALSE : TRUE;
    music_sfx(g_done[i] ? 990 : 550, 1);    /* tick! / oh, not done then  */
    ag_save();
}

void agenda_draw(const Rect *cl)
{
    int i, ndone = 0;
    int bs = font_h();                       /* checkbox side              */
    char hdr[28];

    g_row_h  = font_h() + 4;
    g_row_y0 = cl->y + font_h() + 8;
    g_box_x1 = cl->x + 6 + bs + 3;

    for (i = 0; i < g_n; ++i)
        if (g_done[i])
            ++ndone;
    sprintf(hdr, "Things to do   %d/%d done", ndone, g_n);
    font_draw(cl->x + 4, cl->y + 3, hdr, C_TITLE);
    /* AGENDA.TXT held more than this window can carry.  Say so, and do
       NOT write the file back - rewriting it from the truncated table
       would delete the rest for good. */
    /* ABOVE the button row, not on it: the buttons sit at
       cl->h - font_h() - 8 and this warning was at - font_h() - 2, so the
       one message that says your data is at risk was drawn over them. */
    if (g_clipped)
        ui_text_center(cl->x, cl->y + cl->h - (font_h() + 5) - font_h() - 5,
                       cl->w, "File too long - not saving", C_RED);

    /* Clip to the rows that actually fit above the button strip.  Fourteen
       entries at font_h()+4 need 168px plus chrome in a 172px window, so
       the tail was painted over the "not saving" warning, over Add and
       Delete, and straight past the bottom border.  Moving the warning
       earlier fixed the warning and left this. */
    {
        int avail = (cl->y + cl->h - (font_h() + 5) - 5) - g_row_y0;
        g_vis = avail / g_row_h;
        if (g_clipped)
            g_vis -= 1;                    /* the warning takes a line     */
        if (g_vis < 1) g_vis = 1;
        if (g_vis > AG_MAX) g_vis = AG_MAX;
        if (g_sel >= 0 && g_follow) {      /* keep the selection on screen */
            if (g_sel < g_top)           g_top = g_sel;
            if (g_sel >= g_top + g_vis)  g_top = g_sel - g_vis + 1;
            g_follow = FALSE;
        }
        if (g_top > g_n - g_vis) g_top = g_n - g_vis;
        if (g_top < 0)           g_top = 0;
    }

    /* The rows' rectangle, and a scrollbar beside them when the list is
       longer than the window.  The header counted "4 more" while the only
       thing that could reach them was the keyboard. */
    rect_set(&g_list_r, cl->x + 2, g_row_y0, cl->w - 4 - AGSCROLL_W,
             g_vis * g_row_h);
    if (g_n > g_vis) {
        Rect up, dn, tr;
        ag_scroll_rects(&up, &dn, &tr);
        ui_vscroll(&up, &dn, &tr, g_top, g_vis, g_n);
    }

    for (i = g_top; i < g_n && i < g_top + g_vis; ++i) {
        int ry = g_row_y0 + (i - g_top) * g_row_h;
        int ty = ry + (g_row_h - font_h()) / 2;
        if (i == g_sel)
            vid_fillrect(cl->x + 2, ry, cl->w - 4, g_row_h, C_CYAN);
        vid_fillrect(cl->x + 6, ry + 2, bs, bs, C_WHITE);
        ui_sink(cl->x + 6, ry + 2, bs, bs);
        if (g_done[i]) {
            font_draw_char(cl->x + 7, ry + 2, 'x', C_GREEN);
            font_draw(g_box_x1 + 2, ty, g_text[i], C_DKGRAY);
            vid_hline(g_box_x1 + 2, ty + font_h() / 2,
                      font_text_width(g_text[i]), C_DKGRAY);
        } else {
            font_draw(g_box_x1 + 2, ty, g_text[i], C_BLACK);
        }
    }
    if (g_n == 0)
        font_draw(cl->x + 8, g_row_y0 + 2, "Nothing to do. Bliss.", C_DKGRAY);

    /* Say how many are off-screen.  The list scrolls now rather than
       painting over the buttons, but a silently short list is its own
       kind of lie - the header counts 14 while ten are drawn. */
    if (g_n > g_vis) {
        char more[16];
        int  hidden = g_n - g_vis - g_top;
        if (hidden < 0) hidden = 0;
        if (g_top > 0 && hidden > 0) sprintf(more, "%d more", hidden);
        else if (g_top > 0)          strcpy(more, "end");
        else                         sprintf(more, "%d more", hidden);
        font_draw(cl->x + cl->w - font_text_width(more) - 4, cl->y + 3,
                  more, C_DKGRAY);
    }

    /* Buttons along the bottom edge. */
    {
        int bw = (cl->w - 12) / 2;
        int by = cl->y + cl->h - (font_h() + 5) - 3;
        rect_set(&g_add_b, cl->x + 4, by, bw, font_h() + 5);
        rect_set(&g_del_b, cl->x + 8 + bw, by, bw, font_h() + 5);
        ui_button(&g_add_b, "Add...", FALSE);
        ui_button(&g_del_b, "Delete", FALSE);
    }
}

bool_t agenda_click(const Rect *cl, int mx, int my)
{
    (void)cl;
    if (rect_contains(&g_add_b, mx, my)) {
        ag_add();
        return TRUE;
    }
    if (rect_contains(&g_del_b, mx, my)) {
        ag_delete();
        return TRUE;
    }
    if (g_n > g_vis) {                 /* the scroll arrows                */
        Rect up, dn, tr;
        ag_scroll_rects(&up, &dn, &tr);
        if (rect_contains(&up, mx, my)) {
            if (g_top > 0) --g_top;
            return TRUE;
        }
        if (rect_contains(&dn, mx, my)) {
            if (g_top < g_n - g_vis) ++g_top;
            return TRUE;
        }
    }
    if (my >= g_row_y0) {
        int i = g_top + (my - g_row_y0) / g_row_h;   /* +g_top: it scrolls */
        if (i >= 0 && i < g_n && i < g_top + g_vis) {
            if (mx < g_box_x1)
                ag_toggle(i);          /* the checkbox does the work       */
            g_sel = i;
            return TRUE;
        }
    }
    return FALSE;
}

bool_t agenda_key(int key)
{
    if (key == KEY_UP && g_sel > 0) {
        --g_sel;
        g_follow = TRUE;
        return TRUE;
    }
    if (key == KEY_DOWN && g_sel < g_n - 1) {
        ++g_sel;
        g_follow = TRUE;
        return TRUE;
    }
    if (key == KEY_SPACE && g_sel >= 0) {
        ag_toggle(g_sel);
        return TRUE;
    }
    if (key == KEY_DEL) {
        ag_delete();
        return TRUE;
    }
    /* The list scrolls now, so it needs the keys that drive a list.  Only
       arrow movement could shift it, one row at a time, and there is no
       scrollbar - with fourteen items and ten visible, the last four were
       a long way from reachable. */
    if (key == KEY_PGUP && g_sel > 0) {
        g_sel -= g_vis;
        if (g_sel < 0) g_sel = 0;
        g_follow = TRUE;
        return TRUE;
    }
    if (key == KEY_PGDN && g_sel < g_n - 1) {
        g_sel += g_vis;
        if (g_sel > g_n - 1) g_sel = g_n - 1;
        g_follow = TRUE;
        return TRUE;
    }
    if (key == KEY_HOME && g_n > 0) { g_sel = 0; g_follow = TRUE; return TRUE; }
    if (key == KEY_END  && g_n > 0) { g_sel = g_n - 1; g_follow = TRUE;
                                      return TRUE; }
    if (key == 'a' || key == 'A' || key == KEY_ENTER) {
        ag_add();
        return TRUE;
    }
    return FALSE;
}
