/* ======================================================================
 * ui.c - Reusable widget drawing for CASTALIA/386
 * ====================================================================== */
#include <string.h>
#include "ui.h"
#include "video.h"
#include "font.h"

/* ---- Geometry -------------------------------------------------------- */
void rect_set(Rect *r, int x, int y, int w, int h)
{
    r->x = x; r->y = y; r->w = w; r->h = h;
}

bool_t rect_contains(const Rect *r, int px, int py)
{
    return (px >= r->x && px < r->x + r->w &&
            py >= r->y && py < r->y + r->h) ? TRUE : FALSE;
}

/* ---- 3D surfaces ----------------------------------------------------- */
void ui_fill_face(int x, int y, int w, int h)
{
    vid_fillrect(x, y, w, h, C_FACE);
}

/* Classic two-pixel raised bevel:
 *   outer  : highlight on top/left, darkest on bottom/right
 *   inner  : face (implicit) on top/left, shadow on bottom/right
 */
void ui_raise(int x, int y, int w, int h)
{
    if (w < 2 || h < 2)
        return;
    vid_hline(x, y, w, C_HILIGHT);                 /* top    (light) */
    vid_vline(x, y, h, C_HILIGHT);                 /* left   (light) */
    vid_hline(x, y + h - 1, w, C_DKGRAY);          /* bottom (dark)  */
    vid_vline(x + w - 1, y, h, C_DKGRAY);          /* right  (dark)  */
    vid_hline(x + 1, y + h - 2, w - 2, C_SHADOW);  /* inner bottom   */
    vid_vline(x + w - 2, y + 1, h - 2, C_SHADOW);  /* inner right    */
}

/* Classic two-pixel sunken bevel, in Windows-95's EDGE_SUNKEN order:
   outer top/left BTNSHADOW, inner top/left 3DDKSHADOW (the darkest grey
   sits INSIDE), outer bottom/right BTNHIGHLIGHT, inner bottom/right
   BTNFACE.  That last line was missing entirely, and because C_HILIGHT
   equals C_WHITE in the default theme, a sunken WHITE client - the Help
   page, the Cabinet list, every edit field - lost its bottom-right edge
   into the fill and read as unframed. */
void ui_sink(int x, int y, int w, int h)
{
    if (w < 2 || h < 2)
        return;
    vid_hline(x, y, w, C_SHADOW);                  /* outer top      */
    vid_vline(x, y, h, C_SHADOW);                  /* outer left     */
    vid_hline(x, y + h - 1, w, C_HILIGHT);         /* outer bottom   */
    vid_vline(x + w - 1, y, h, C_HILIGHT);         /* outer right    */
    if (w < 4 || h < 4)
        return;
    vid_hline(x + 1, y + 1, w - 2, C_DKGRAY);      /* inner top      */
    vid_vline(x + 1, y + 1, h - 2, C_DKGRAY);      /* inner left     */
    vid_hline(x + 1, y + h - 2, w - 2, C_FACE);    /* inner bottom   */
    vid_vline(x + w - 2, y + 1, h - 2, C_FACE);    /* inner right    */
}

/* A soft drop shadow: a 50%-dithered dark band offset down and right of a
   rectangle.  The checkerboard lets the background show through, so it
   reads as a translucent shadow over the desktop or a window below with no
   need for alpha.  Call it BEFORE drawing the element's own frame. */
void ui_shadow(int x, int y, int w, int h)
{
    int s = 3 * ui_scale();
    /* Two dithered bands drawn with the row-wise checkerboard primitive -
       every window and menu casts one of these per repaint, so it must not
       cost a function call per pixel (as the old loop did). */
    vid_dither_rect(x + s, y + h, w,     s, C_BLACK);   /* bottom band */
    vid_dither_rect(x + w, y + s, s, h - s, C_BLACK);   /* right band  */
}

void ui_text_center(int x, int y, int w, const char *s, u8 color)
{
    int tw = font_text_width(s);
    int tx = x + (w - tw) / 2;
    if (tx < x) tx = x;
    font_draw(tx, y, s, color);
}

/* Split an icon caption into the two lines an icon cell has room for.
   Three copies of this used to live in desktop.c, group.c and drawer.c,
   and all three broke a long single word by filling line one and
   orphaning the tail: the Program Drawer really did read "Calculato" /
   "r" and "About" / "Castalia".  Splitting near the MIDDLE instead gives
   "Calcu" / "lator", which is what Windows did and what reads.

   `w` is the usable width in pixels - pass the cell width minus a gutter,
   or 9-character labels butt straight up against the next cell (that is
   why "Scrap Box" and "Calculato" appeared to be one caption).  Anything
   that still will not fit is ellipsised rather than silently cut. */
static bool_t has_space(const char *s)
{
    while (*s != '\0') { if (*s == ' ') return TRUE; ++s; }
    return FALSE;
}

void ui_wrap2(const char *s, int w, int wmax, char *a, char *b, int cap)
{
    int maxch = w / FONT_ADV;
    int hard  = (wmax > w ? wmax : w) / FONT_ADV;
    int len   = (int)strlen(s);
    int brk, start, i, j, lim;

    if (cap < 1)
        return;
    a[0] = '\0';
    b[0] = '\0';
    if (maxch > cap - 1) maxch = cap - 1;
    if (maxch < 1)       maxch = 1;

    if (hard > cap - 1) hard = cap - 1;

    /* A word with no space in it is allowed the FULL cell before it gets
       split; only multi-word captions wrap at the tighter gutter width.
       Wrapping everything at the gutter turned "Minefield" into
       "Minef" / "ield", which is worse than the crowding it cured -
       while "Scrap Box" genuinely does need the gutter, or the gap
       between two captions comes out smaller than the space inside one. */
    if (len <= maxch || (len <= hard && !has_space(s))) {
        for (i = 0; i < len && i < cap - 1; ++i) a[i] = s[i];
        a[i] = '\0';
        return;
    }

    brk = -1;                          /* last space that fits on line one */
    for (i = 0; i < len && i <= maxch; ++i)
        if (s[i] == ' ')
            brk = i;
    if (brk < 0) {
        brk = (len + 1) / 2;           /* one long word: halve it          */
        if (brk > maxch) brk = maxch;
    }

    for (i = 0; i < brk && i < cap - 1; ++i)
        a[i] = s[i];
    a[i] = '\0';

    start = (s[brk] == ' ') ? brk + 1 : brk;
    /* Line two gets the same courtesy line one does: a remainder that is
       a single unbreakable word may use the whole cell before it is cut.
       Without this, "System Inspector" wrapped to "System" / "Inspe..."
       - the gutter is there to stop two captions crowding each other,
       and there is nothing beside line two to crowd. */
    lim = maxch;
    if (!has_space(s + start) && (len - start) <= hard)
        lim = hard;
    for (i = start, j = 0; i < len && j < lim && j < cap - 1; ++i, ++j)
        b[j] = s[i];
    b[j] = '\0';
    if (i < len && j >= 3) {           /* still more text: say so          */
        b[j - 1] = '.';
        b[j - 2] = '.';
        b[j - 3] = '.';
    }
}

/* The Windows dotted keyboard-focus rectangle.  dialog.c had its own
   private copy; the desktop's icon selection wants the same cue. */
void ui_focus_rect(int x, int y, int w, int h)
{
    int i;
    if (w < 2 || h < 2)
        return;
    for (i = 0; i < w; i += 2) {
        vid_pixel(x + i, y, C_BLACK);
        vid_pixel(x + i, y + h - 1, C_BLACK);
    }
    for (i = 0; i < h; i += 2) {
        vid_pixel(x, y + i, C_BLACK);
        vid_pixel(x + w - 1, y + i, C_BLACK);
    }
}

/* A solid arrow head, sized to the button.  The group windows used to put
   the LETTERS "^" and "v" on their scroll buttons, and the v read as a U. */
void ui_arrow(const Rect *r, bool_t up)
{
    int cx = r->x + r->w / 2;
    int cy = r->y + r->h / 2;
    int n  = r->w / 3;
    int i;
    if (n < 3) n = 3;
    for (i = 0; i < n; ++i) {
        int w = up ? (i + 1) : (n - i);
        vid_hline(cx - w, cy - n / 2 + i, w * 2 + 1, C_BLACK);
    }
}

/* The full vertical scrollbar Windows drew: two arrow buttons, a 50%
   checkered trough, and a raised thumb whose length is the visible
   fraction.  The group windows (Toolbox, Arcade) had NO trough and NO
   thumb - just two carets floating in a grey gutter - so nothing told you
   that eleven of the Toolbox's twenty-one items were below the fold.
   A full-height thumb when everything fits is deliberate: an empty
   sunken trough reads as a broken control. */
void ui_vscroll(const Rect *up, const Rect *dn, const Rect *track,
                int top, int visible, int total)
{
    ui_fill_face(up->x, up->y, up->w, up->h);
    ui_raise(up->x, up->y, up->w, up->h);
    ui_arrow(up, TRUE);
    ui_fill_face(dn->x, dn->y, dn->w, dn->h);
    ui_raise(dn->x, dn->y, dn->w, dn->h);
    ui_arrow(dn, FALSE);

    /* Two calls, not one per pixel.  The per-pixel version compiled to a
       far call plus four clip tests plus the mode dispatch for every
       trough pixel - and in Mode 12h a 4-plane read-modify-write on top -
       which is ~8800 far calls to draw one trough on a maximized window,
       repeated on every compose.  vid_dither_rect walks a pointer (whole
       byte masks in the planar path); the checker parity shifts by a
       pixel, which on a checkerboard is invisible. */
    vid_fillrect(track->x, track->y, track->w, track->h, C_FACE);
    vid_dither_rect(track->x, track->y, track->w, track->h, C_WHITE);

    if (track->h > 8 && visible > 0) {
        int hidden = total - visible;
        int th = (total > visible) ? track->h * visible / total : track->h;
        int ty = track->y;
        if (th < 8)          th = 8;
        if (th > track->h)   th = track->h;
        if (hidden > 0)      /* long: track*top overflows an int otherwise */
            ty += (int)((long)(track->h - th) * top / hidden);
        ui_fill_face(track->x, ty, track->w, th);
        ui_raise(track->x, ty, track->w, th);
    }
}

void ui_button(const Rect *r, const char *label, bool_t pressed)
{
    int ty = r->y + (r->h - FONT_H_PX) / 2;
    ui_fill_face(r->x, r->y, r->w, r->h);
    if (pressed) {
        ui_sink(r->x, r->y, r->w, r->h);
        ui_text_center(r->x + 1, ty + 1, r->w, label, C_BLACK);
    } else {
        ui_raise(r->x, r->y, r->w, r->h);
        ui_text_center(r->x, ty, r->w, label, C_BLACK);
    }
}

/* A property-sheet tab.  Drawing tabs as push buttons with the active one
   "pressed" reads as held-down, not as selected - in the About box all
   five looked essentially identical.  A real tab is taller when active
   and merges into the page below it by leaving its bottom edge open; the
   inactive ones sit lower and keep theirs.  Call ui_tab_page_top() to run
   the page's own top edge between them. */
void ui_tab(const Rect *r, const char *label, bool_t active)
{
    int x = r->x, w = r->w;
    int y = active ? r->y : r->y + 2;
    int h = active ? r->h : r->h - 2;
    ui_fill_face(x, y, w, h);
    vid_hline(x + 1, y,         w - 2, C_HILIGHT);      /* top             */
    vid_vline(x,     y + 1,     h - 1, C_HILIGHT);      /* left            */
    vid_vline(x + w - 1, y + 1, h - 1, C_DKGRAY);       /* right           */
    vid_pixel(x, y, C_FACE);                            /* soften the corner*/
    if (!active)                                        /* closed bottom   */
        vid_hline(x, y + h - 1, w, C_DKGRAY);
    ui_text_center(x, y + (h - FONT_H_PX) / 2, w, label, C_BLACK);
}

/* The page edge under a tab strip: highlight across, broken where the
   active tab meets it so the two read as one surface. */
void ui_tab_page_top(int x, int y, int w, const Rect *active)
{
    if (active == (const Rect *)0) {
        vid_hline(x, y, w, C_HILIGHT);
        return;
    }
    if (active->x > x)
        vid_hline(x, y, active->x - x, C_HILIGHT);
    if (active->x + active->w < x + w)
        vid_hline(active->x + active->w, y,
                  (x + w) - (active->x + active->w), C_HILIGHT);
}

/* ---- checkbox, radio, group box --------------------------------------
 * The widget vocabulary was push buttons and nothing else: every boolean
 * in the shell was a button captioned "Sound: ON" and every exclusive
 * choice a grid of pressed buttons.  A pressed push button reads as
 * "held down", not as "this is the setting" - these three say what they
 * mean, and they are what a 1995 control panel was made of.
 * -------------------------------------------------------------------- */

/* The box side, tied to the font so it scales with the mode. */
int ui_check_size(void) { return FONT_H_PX; }

void ui_checkbox(int x, int y, bool_t on, const char *label)
{
    int s = ui_check_size();
    vid_fillrect(x, y, s, s, C_WHITE);
    ui_sink(x, y, s, s);
    if (on) {
        /* A tick, not an X: two strokes down-right then up-right. */
        int i;
        for (i = 0; i < s / 3; ++i)
            vid_pixel(x + 2 + i, y + s / 2 + i, C_BLACK);
        for (i = 0; i < s / 2; ++i)
            vid_pixel(x + 2 + s / 3 + i, y + s - 3 - i, C_BLACK);
    }
    if (label != (const char *)0)
        font_draw(x + s + 4, y + (s - FONT_H_PX) / 2, label, C_BLACK);
}

void ui_radio(int x, int y, bool_t on, const char *label)
{
    int s = ui_check_size();
    int cx = x + s / 2, cy = y + s / 2, r = s / 2 - 1;
    if (r < 2) r = 2;
    /* A sunken disc: light on the lower-right arc, dark on the upper-left,
       the way Win95 shaded its radio buttons. */
    vid_fillrect(x + 1, y + 1, s - 2, s - 2, C_WHITE);
    vid_hline(x + 2, y,         s - 4, C_SHADOW);
    vid_hline(x + 2, y + s - 1, s - 4, C_HILIGHT);
    vid_vline(x,         y + 2, s - 4, C_SHADOW);
    vid_vline(x + s - 1, y + 2, s - 4, C_HILIGHT);
    if (on) {
        int d = (r > 3) ? 3 : 2;
        vid_fillrect(cx - d / 2, cy - d / 2, d, d, C_BLACK);
    }
    if (label != (const char *)0)
        font_draw(x + s + 4, y + (s - FONT_H_PX) / 2, label, C_BLACK);
}

void ui_groupbox(int x, int y, int w, int h, const char *title)
{
    int ty = y + FONT_H_PX / 2;
    int tw = (title != (const char *)0) ? font_text_width(title) + 4 : 0;
    /* An etched frame: shadow then highlight, one pixel apart. */
    vid_hline(x + 1,     ty,         w - 1, C_SHADOW);
    vid_hline(x + 1,     ty + 1,     w - 1, C_HILIGHT);
    vid_hline(x + 1,     y + h - 2,  w - 1, C_SHADOW);
    vid_hline(x + 1,     y + h - 1,  w - 1, C_HILIGHT);
    vid_vline(x,         ty,         h - ty + y, C_SHADOW);
    vid_vline(x + 1,     ty + 1,     h - ty + y - 2, C_HILIGHT);
    vid_vline(x + w - 2, ty,         h - ty + y - 1, C_SHADOW);
    vid_vline(x + w - 1, ty,         h - ty + y,     C_HILIGHT);
    if (tw > 0) {
        /* Notch the frame so the caption sits IN the line. */
        ui_fill_face(x + 7, ty, tw, 2);
        font_draw(x + 9, y, title, C_BLACK);
    }
}

/* ---- Procedural icons -----------------------------------------------
 * Drawn at 32x32 logical units, multiplied by g_icon_scale (1 in Mode
 * 13h, 2 in Mode 12h).  Every literal goes through S() so the whole icon
 * scales without re-deriving each shape. */
static int g_icon_scale = 1;
#define S(v) ((v) * g_icon_scale)

void ui_set_scale(int s)   { g_icon_scale = (s < 1) ? 1 : s; }
int  ui_scale(void)        { return g_icon_scale; }
int  ui_icon_size(void)    { return 32 * g_icon_scale; }

static void icon_folder(int x, int y)
{
    vid_fillrect(x + S(5),  y + S(7),  S(11), S(3),  C_DKYELLOW);  /* tab   */
    vid_rect    (x + S(5),  y + S(7),  S(11), S(3),  C_BLACK);
    vid_fillrect(x + S(8),  y + S(9),  S(17), S(3),  C_WHITE);     /* page  */
    vid_rect    (x + S(8),  y + S(9),  S(17), S(3),  C_DKGRAY);
    vid_fillrect(x + S(3),  y + S(11), S(26), S(16), C_YELLOW);    /* flap  */
    vid_rect    (x + S(3),  y + S(11), S(26), S(16), C_BLACK);
    vid_hline   (x + S(4),  y + S(12), S(24), C_CREAM);            /* sheen */
    vid_vline   (x + S(4),  y + S(12), S(13), C_CREAM);            /* lit edge */
    vid_hline   (x + S(4),  y + S(25), S(24), C_DKYELLOW);         /* base  */
}

/* My Computer: a beige CRT sitting on a desktop case, Windows-95 style. */
static void icon_computer(int x, int y)
{
    ui_fill_face(x + S(5),  y + S(2),  S(22), S(16));            /* monitor  */
    vid_rect    (x + S(5),  y + S(2),  S(22), S(16), C_BLACK);
    ui_raise    (x + S(6),  y + S(3),  S(20), S(14));
    vid_fillrect(x + S(8),  y + S(5),  S(16), S(10), C_CYAN);    /* screen   */
    vid_rect    (x + S(8),  y + S(5),  S(16), S(10), C_BLACK);
    vid_fillrect(x + S(9),  y + S(6),  S(14), S(4),  C_LTBLUE);  /* sky      */
    vid_fillrect(x + S(9),  y + S(11), S(14), S(3),  C_GREEN);   /* ground   */
    vid_fillrect(x + S(11), y + S(8),  S(3),  S(4),  C_DKYELLOW);/* keep     */
    vid_pixel   (x + S(12), y + S(7),  C_RED);                   /* keep roof */
    vid_fillrect(x + S(18), y + S(7),  S(3),  S(3),  C_WHITE);   /* cloud    */
    vid_fillrect(x + S(22), y + S(16), S(2),  S(2),  C_GREEN);   /* power LED */
    ui_fill_face(x + S(14), y + S(18), S(4),  S(2));             /* stand    */
    vid_rect    (x + S(14), y + S(18), S(4),  S(2),  C_BLACK);
    ui_fill_face(x + S(3),  y + S(20), S(26), S(7));             /* case     */
    vid_rect    (x + S(3),  y + S(20), S(26), S(7),  C_BLACK);
    ui_raise    (x + S(4),  y + S(21), S(24), S(5));
    vid_hline   (x + S(6),  y + S(22), S(9),  C_SHADOW);         /* slots    */
    vid_hline   (x + S(6),  y + S(24), S(7),  C_SHADOW);
    vid_fillrect(x + S(24), y + S(22), S(2),  S(2),  C_GREEN);   /* drive LED */
    vid_fillrect(x + S(21), y + S(22), S(2),  S(2),  C_RED);
}

static void icon_terminal(int x, int y)
{
    ui_fill_face(x + S(3), y + S(4), S(26), S(19));                /* case   */
    vid_rect    (x + S(3), y + S(4), S(26), S(19), C_BLACK);
    ui_raise    (x + S(4), y + S(5), S(24), S(17));
    vid_fillrect(x + S(6), y + S(7), S(20), S(12), C_BLACK);       /* screen */
    vid_rect    (x + S(6), y + S(7), S(20), S(12), C_DKGRAY);
    font_draw_char(x + S(8), y + S(8), '>', C_GREEN);             /* prompt  */
    vid_fillrect(x + S(13), y + S(9),  S(4),  S(2), C_GREEN);
    vid_fillrect(x + S(8),  y + S(13), S(10), S(1), C_GREEN);      /* output */
    vid_fillrect(x + S(8),  y + S(15), S(6),  S(1), C_GREEN);
    vid_fillrect(x + S(19), y + S(16), S(3),  S(2), C_GREEN);      /* cursor */
    vid_fillrect(x + S(12), y + S(23), S(8),  S(3), C_FACE);       /* neck   */
    vid_rect    (x + S(12), y + S(23), S(8),  S(3), C_BLACK);
    vid_fillrect(x + S(8),  y + S(26), S(16), S(3), C_FACE);       /* base   */
    vid_rect    (x + S(8),  y + S(26), S(16), S(3), C_BLACK);
}

static void icon_system(int x, int y)
{
    int i;
    ui_fill_face(x + S(7), y + S(3), S(18), S(25));               /* tower   */
    vid_rect    (x + S(7), y + S(3), S(18), S(25), C_BLACK);
    ui_raise    (x + S(8), y + S(4), S(16), S(23));
    vid_fillrect(x + S(10), y + S(6), S(12), S(3), C_GREEN);      /* display */
    vid_rect    (x + S(10), y + S(6), S(12), S(3), C_BLACK);
    for (i = 0; i < 4; ++i)                                       /* vents   */
        vid_hline(x + S(10), y + S(11) + i * S(2), S(12), C_SHADOW);
    vid_fillrect(x + S(10), y + S(20), S(3), S(3), C_GREEN);      /* LEDs    */
    vid_rect    (x + S(10), y + S(20), S(3), S(3), C_BLACK);
    vid_fillrect(x + S(15), y + S(20), S(3), S(3), C_YELLOW);
    vid_rect    (x + S(15), y + S(20), S(3), S(3), C_BLACK);
    vid_fillrect(x + S(20), y + S(20), S(3), S(3), C_RED);
    vid_rect    (x + S(20), y + S(20), S(3), S(3), C_BLACK);
}

static void icon_drawer(int x, int y)
{
    vid_fillrect(x + S(7),  y + S(3), S(11), S(4), C_DKYELLOW);    /* tab    */
    vid_rect    (x + S(7),  y + S(3), S(11), S(4), C_BLACK);
    ui_fill_face(x + S(4),  y + S(5), S(24), S(22));               /* cabinet */
    vid_rect    (x + S(4),  y + S(5), S(24), S(22), C_BLACK);
    ui_raise    (x + S(5),  y + S(6), S(22), S(20));
    vid_hline   (x + S(5),  y + S(16), S(22), C_SHADOW);           /* split  */
    vid_hline   (x + S(5),  y + S(17), S(22), C_HILIGHT);
    vid_fillrect(x + S(12), y + S(10), S(8), S(2), C_DKGRAY);      /* handles */
    vid_rect    (x + S(12), y + S(10), S(8), S(2), C_BLACK);
    vid_fillrect(x + S(12), y + S(21), S(8), S(2), C_DKGRAY);
    vid_rect    (x + S(12), y + S(21), S(8), S(2), C_BLACK);
}

static void icon_info(int x, int y)
{
    /* Three stacked fills make an octagonal bubble.  The frame used to be
       one vid_rect around the whole cell, which drew a hard black SQUARE
       over a round icon and left four notches of desktop showing at the
       corners - the "About" icon shipped as a black box.  Trace the disc's
       own silhouette instead: a cap top and bottom, and a black edge down
       the exposed side of each band. */
    vid_fillrect(x + S(9),  y + S(5),  S(14), S(22), C_BLUE);      /* disc   */
    vid_fillrect(x + S(6),  y + S(8),  S(20), S(16), C_BLUE);
    vid_fillrect(x + S(5),  y + S(11), S(22), S(10), C_BLUE);
    vid_hline(x + S(9),  y + S(4),  S(14), C_BLACK);   /* top cap         */
    vid_hline(x + S(9),  y + S(27), S(14), C_BLACK);   /* bottom cap      */
    vid_vline(x + S(8),  y + S(5),  S(3),  C_BLACK);   /* upper shoulders */
    vid_vline(x + S(23), y + S(5),  S(3),  C_BLACK);
    vid_vline(x + S(5),  y + S(8),  S(3),  C_BLACK);
    vid_vline(x + S(26), y + S(8),  S(3),  C_BLACK);
    vid_vline(x + S(4),  y + S(11), S(10), C_BLACK);   /* the waist       */
    vid_vline(x + S(27), y + S(11), S(10), C_BLACK);
    vid_vline(x + S(5),  y + S(21), S(3),  C_BLACK);   /* lower shoulders */
    vid_vline(x + S(26), y + S(21), S(3),  C_BLACK);
    vid_vline(x + S(8),  y + S(24), S(3),  C_BLACK);
    vid_vline(x + S(23), y + S(24), S(3),  C_BLACK);
    /* The sheen used to run the full 20px width on a row where only the
       narrow top band exists, so it spilled onto the desktop. */
    vid_fillrect(x + S(10), y + S(5),  S(12), S(1), C_LTBLUE);     /* sheen  */
    vid_fillrect(x + S(14), y + S(8),  S(4), S(3), C_WHITE);       /* dot    */
    vid_fillrect(x + S(14), y + S(13), S(4), S(10), C_WHITE);      /* stem   */
    vid_hline   (x + S(13), y + S(22), S(6), C_WHITE);             /* base   */
}

static void icon_file(int x, int y)
{
    vid_fillrect(x + S(7), y + S(4), S(18), S(24), C_WHITE);
    vid_rect    (x + S(7), y + S(4), S(18), S(24), C_BLACK);
    vid_fillrect(x + S(7), y + S(4), S(18), S(3), C_TITLE);        /* strip  */
    vid_fillrect(x + S(20), y + S(4), S(5), S(5), C_FACE);         /* corner */
    vid_pixel   (x + S(20), y + S(4), C_BLACK);
    vid_hline   (x + S(10), y + S(12), S(12), C_SHADOW);
    vid_hline   (x + S(10), y + S(15), S(12), C_SHADOW);
    vid_hline   (x + S(10), y + S(18), S(12), C_SHADOW);
    vid_hline   (x + S(10), y + S(21), S(8),  C_SHADOW);
}

static void icon_disk(int x, int y)
{
    vid_fillrect(x + S(4), y + S(4), S(24), S(24), C_BLUE);        /* shell  */
    vid_rect    (x + S(4), y + S(4), S(24), S(24), C_BLACK);
    vid_hline   (x + S(5), y + S(5), S(22), C_LTBLUE);             /* sheen  */
    vid_vline   (x + S(5), y + S(5), S(11), C_LTBLUE);
    vid_fillrect(x + S(9), y + S(4), S(12), S(10), C_FACE);        /* shutter */
    vid_rect    (x + S(9), y + S(4), S(12), S(10), C_BLACK);
    vid_fillrect(x + S(15), y + S(5), S(4), S(8), C_DKGRAY);       /* slide  */
    vid_fillrect(x + S(22), y + S(5), S(3), S(4), C_DKGRAY);       /* notch  */
    vid_fillrect(x + S(8), y + S(17), S(16), S(10), C_WHITE);      /* label  */
    vid_rect    (x + S(8), y + S(17), S(16), S(10), C_BLACK);
    vid_hline   (x + S(10), y + S(20), S(12), C_SHADOW);
    vid_hline   (x + S(10), y + S(23), S(12), C_SHADOW);
}

/* A fixed disk: the beige half-height drive box Win95 drew, seen at a
   slight angle - a dark top deck, a lighter face plate with a seam and a
   red activity light.  Deliberately nothing like icon_disk above: the
   whole point is that A: and C: stop looking identical. */
static void icon_hdd(int x, int y)
{
    vid_fillrect(x + S(3),  y + S(9),  S(26), S(6),  C_DKGRAY);    /* deck   */
    vid_rect    (x + S(3),  y + S(9),  S(26), S(6),  C_BLACK);
    vid_hline   (x + S(4),  y + S(10), S(24), C_FACE);             /* sheen  */
    vid_fillrect(x + S(3),  y + S(15), S(26), S(11), C_FACE);      /* face   */
    vid_rect    (x + S(3),  y + S(15), S(26), S(11), C_BLACK);
    vid_hline   (x + S(4),  y + S(16), S(24), C_WHITE);            /* bevel  */
    vid_hline   (x + S(4),  y + S(20), S(24), C_SHADOW);           /* seam   */
    vid_fillrect(x + S(5),  y + S(22), S(9),  S(2),  C_DKGRAY);    /* vents  */
    vid_fillrect(x + S(24), y + S(22), S(3),  S(2),  C_RED);       /* LED    */
    vid_rect    (x + S(24), y + S(22), S(3),  S(2),  C_BLACK);
}

/* The same drive on the far end of a wire: Win95's network drive was the
   disk with a length of pipe under it. */
static void icon_netdrv(int x, int y)
{
    vid_fillrect(x + S(3),  y + S(6),  S(26), S(5),  C_DKGRAY);    /* deck   */
    vid_rect    (x + S(3),  y + S(6),  S(26), S(5),  C_BLACK);
    vid_fillrect(x + S(3),  y + S(11), S(26), S(9),  C_FACE);      /* face   */
    vid_rect    (x + S(3),  y + S(11), S(26), S(9),  C_BLACK);
    vid_hline   (x + S(4),  y + S(12), S(24), C_WHITE);
    vid_fillrect(x + S(24), y + S(16), S(3),  S(2),  C_RED);       /* LED    */
    vid_fillrect(x + S(14), y + S(20), S(4),  S(4),  C_DKGRAY);    /* stalk  */
    vid_fillrect(x + S(4),  y + S(24), S(24), S(4),  C_LTBLUE);    /* pipe   */
    vid_rect    (x + S(4),  y + S(24), S(24), S(4),  C_BLACK);
    vid_hline   (x + S(5),  y + S(25), S(22), C_WHITE);
}

static void icon_note(int x, int y)
{
    vid_fillrect(x + S(7),  y + S(5),  S(19), S(23), C_WHITE);     /* page   */
    vid_rect    (x + S(7),  y + S(5),  S(19), S(23), C_BLACK);
    vid_fillrect(x + S(8),  y + S(6),  S(17), S(4),  C_RED);       /* header */
    vid_fillrect(x + S(10), y + S(2),  S(2),  S(5),  C_DKGRAY);    /* spiral */
    vid_rect    (x + S(10), y + S(2),  S(2),  S(5),  C_BLACK);
    vid_fillrect(x + S(16), y + S(2),  S(2),  S(5),  C_DKGRAY);
    vid_rect    (x + S(16), y + S(2),  S(2),  S(5),  C_BLACK);
    vid_fillrect(x + S(22), y + S(2),  S(2),  S(5),  C_DKGRAY);
    vid_rect    (x + S(22), y + S(2),  S(2),  S(5),  C_BLACK);
    vid_hline   (x + S(10), y + S(13), S(13), C_LTBLUE);           /* rules  */
    vid_hline   (x + S(10), y + S(17), S(13), C_LTBLUE);
    vid_hline   (x + S(10), y + S(21), S(13), C_LTBLUE);
    vid_hline   (x + S(10), y + S(25), S(9),  C_LTBLUE);
}

static void icon_calc(int x, int y)
{
    int r, c;
    ui_fill_face(x + S(7),  y + S(3), S(18), S(26));              /* body   */
    vid_rect    (x + S(7),  y + S(3), S(18), S(26), C_BLACK);
    ui_raise    (x + S(8),  y + S(4), S(16), S(24));
    vid_fillrect(x + S(10), y + S(6), S(12), S(5), C_GREEN);      /* LCD    */
    vid_rect    (x + S(10), y + S(6), S(12), S(5), C_BLACK);
    for (r = 0; r < 3; ++r)                                       /* keypad */
        for (c = 0; c < 3; ++c) {
            vid_fillrect(x + S(10) + c * S(4), y + S(14) + r * S(5),
                         S(3), S(3), C_DKGRAY);
            vid_pixel(x + S(10) + c * S(4), y + S(14) + r * S(5), C_SHADOW);
        }
}

static void icon_clock(int x, int y)
{
    ui_fill_face(x + S(5),  y + S(5), S(22), S(22));             /* case   */
    vid_rect    (x + S(5),  y + S(5), S(22), S(22), C_BLACK);
    ui_raise    (x + S(6),  y + S(6), S(20), S(20));
    vid_fillrect(x + S(8),  y + S(8), S(16), S(16), C_WHITE);    /* face   */
    vid_rect    (x + S(8),  y + S(8), S(16), S(16), C_BLACK);
    vid_fillrect(x + S(15), y + S(9),  S(2), S(2), C_BLACK);     /* 12     */
    vid_fillrect(x + S(15), y + S(21), S(2), S(2), C_BLACK);     /* 6      */
    vid_fillrect(x + S(9),  y + S(15), S(2), S(2), C_BLACK);     /* 9      */
    vid_fillrect(x + S(21), y + S(15), S(2), S(2), C_BLACK);     /* 3      */
    vid_fillrect(x + S(15), y + S(12), S(2), S(5), C_BLUE);      /* hour   */
    vid_fillrect(x + S(16), y + S(15), S(5), S(2), C_RED);       /* minute */
    vid_fillrect(x + S(15), y + S(15), S(2), S(2), C_BLACK);     /* hub    */
}

static void icon_paint(int x, int y)
{
    ui_fill_face(x + S(5),  y + S(7),  S(22), S(18));            /* box    */
    vid_rect    (x + S(5),  y + S(7),  S(22), S(18), C_BLACK);
    ui_raise    (x + S(6),  y + S(8),  S(20), S(16));
    vid_fillrect(x + S(8),  y + S(10), S(4), S(4), C_RED);       /* wells  */
    vid_rect    (x + S(8),  y + S(10), S(4), S(4), C_BLACK);
    vid_fillrect(x + S(14), y + S(10), S(4), S(4), C_YELLOW);
    vid_rect    (x + S(14), y + S(10), S(4), S(4), C_BLACK);
    vid_fillrect(x + S(20), y + S(10), S(4), S(4), C_GREEN);
    vid_rect    (x + S(20), y + S(10), S(4), S(4), C_BLACK);
    vid_fillrect(x + S(8),  y + S(17), S(4), S(4), C_BLUE);
    vid_rect    (x + S(8),  y + S(17), S(4), S(4), C_BLACK);
    vid_fillrect(x + S(14), y + S(17), S(4), S(4), C_CYAN);
    vid_rect    (x + S(14), y + S(17), S(4), S(4), C_BLACK);
    vid_fillrect(x + S(20), y + S(17), S(4), S(4), C_WHITE);
    vid_rect    (x + S(20), y + S(17), S(4), S(4), C_BLACK);
}

static void icon_chars(int x, int y)
{
    vid_fillrect  (x + S(5),  y + S(4),  S(22), S(24), C_WHITE);  /* sheet */
    vid_rect      (x + S(5),  y + S(4),  S(22), S(24), C_BLACK);
    vid_vline     (x + S(12), y + S(5),  S(22), C_SHADOW);        /* grid  */
    vid_vline     (x + S(19), y + S(5),  S(22), C_SHADOW);
    vid_hline     (x + S(6),  y + S(12), S(20), C_SHADOW);
    vid_hline     (x + S(6),  y + S(19), S(20), C_SHADOW);
    font_draw_char(x + S(6),  y + S(6),  'A', C_BLACK);
    font_draw_char(x + S(13), y + S(6),  'B', C_BLUE);
    font_draw_char(x + S(6),  y + S(13), 'a', C_RED);
    font_draw_char(x + S(20), y + S(20), '?', C_BLACK);
}

static void icon_music(int x, int y)
{
    vid_fillrect(x + S(9),  y + S(19), S(7),  S(6),  C_BLUE);    /* head 1 */
    vid_rect    (x + S(9),  y + S(19), S(7),  S(6),  C_BLACK);
    vid_fillrect(x + S(20), y + S(16), S(7),  S(6),  C_BLUE);    /* head 2 */
    vid_rect    (x + S(20), y + S(16), S(7),  S(6),  C_BLACK);
    vid_fillrect(x + S(14), y + S(4),  S(2),  S(17), C_BLACK);   /* stem 1 */
    vid_fillrect(x + S(25), y + S(4),  S(2),  S(14), C_BLACK);   /* stem 2 */
    vid_fillrect(x + S(14), y + S(4),  S(13), S(3),  C_BLACK);   /* beam   */
}

static void icon_game(int x, int y)
{
    ui_fill_face(x + S(6),  y + S(6),  S(20), S(20));            /* die    */
    vid_rect    (x + S(6),  y + S(6),  S(20), S(20), C_BLACK);
    ui_raise    (x + S(7),  y + S(7),  S(18), S(18));
    vid_fillrect(x + S(10), y + S(10), S(3), S(3), C_BLUE);      /* pips   */
    vid_fillrect(x + S(19), y + S(10), S(3), S(3), C_BLUE);
    vid_fillrect(x + S(15), y + S(15), S(3), S(3), C_RED);
    vid_fillrect(x + S(10), y + S(19), S(3), S(3), C_BLUE);
    vid_fillrect(x + S(19), y + S(19), S(3), S(3), C_BLUE);
}

static void icon_gauge(int x, int y)
{
    int i;
    ui_fill_face(x + S(4),  y + S(6),  S(24), S(20));          /* meter   */
    vid_rect    (x + S(4),  y + S(6),  S(24), S(20), C_BLACK);
    ui_raise    (x + S(5),  y + S(7),  S(22), S(18));
    vid_fillrect(x + S(7),  y + S(9),  S(18), S(10), C_WHITE); /* dial    */
    vid_rect    (x + S(7),  y + S(9),  S(18), S(10), C_BLACK);
    vid_fillrect(x + S(9),  y + S(16), S(2), S(2), C_GREEN);   /* scale   */
    vid_fillrect(x + S(15), y + S(16), S(2), S(2), C_YELLOW);
    vid_fillrect(x + S(21), y + S(16), S(2), S(2), C_RED);
    for (i = 0; i < S(7); ++i) {                              /* needle  */
        vid_pixel(x + S(16) + i, y + S(17) - i, C_BLACK);
        vid_pixel(x + S(16) + i, y + S(18) - i, C_BLACK);
    }
    vid_fillrect(x + S(15), y + S(16), S(3), S(3), C_BLACK);   /* hub     */
    vid_fillrect(x + S(11), y + S(22), S(10), S(2), C_DKGRAY); /* label   */
}

static void icon_watch(int x, int y)
{
    int i;
    vid_fillrect(x + S(13), y + S(2), S(6),  S(4),  C_DKGRAY);   /* crown  */
    vid_rect    (x + S(13), y + S(2), S(6),  S(4),  C_BLACK);
    ui_fill_face(x + S(6),  y + S(6), S(20), S(21));             /* case   */
    vid_rect    (x + S(6),  y + S(6), S(20), S(21), C_BLACK);
    ui_raise    (x + S(7),  y + S(7), S(18), S(19));
    vid_fillrect(x + S(9),  y + S(9), S(14), S(15), C_WHITE);    /* face   */
    vid_rect    (x + S(9),  y + S(9), S(14), S(15), C_BLACK);
    for (i = 0; i < S(6); ++i) {                                /* sweep  */
        vid_pixel(x + S(16) + i, y + S(16) - i, C_RED);
        vid_pixel(x + S(16) + i, y + S(17) - i, C_RED);
    }
    vid_fillrect(x + S(15), y + S(15), S(3), S(3), C_BLACK);     /* hub    */
}

static void icon_exit(int x, int y)
{
    vid_fillrect(x + S(5),  y + S(4),  S(13), S(24), C_DKYELLOW); /* frame  */
    vid_rect    (x + S(5),  y + S(4),  S(13), S(24), C_BLACK);
    vid_fillrect(x + S(7),  y + S(6),  S(9),  S(20), C_YELLOW);   /* panel  */
    vid_fillrect(x + S(13), y + S(15), S(2),  S(3),  C_BLACK);    /* knob   */
    vid_fillrect(x + S(18), y + S(15), S(6),  S(2),  C_GREEN);    /* shaft  */
    vid_vline   (x + S(23), y + S(13), S(6),  C_GREEN);           /* head   */
    vid_vline   (x + S(24), y + S(14), S(4),  C_GREEN);
    vid_vline   (x + S(25), y + S(15), S(2),  C_GREEN);
}

static void icon_tools(int x, int y)
{
    vid_rect    (x + S(11), y + S(3),  S(10), S(4), C_BLACK);     /* handle */
    vid_hline   (x + S(12), y + S(4),  S(8),  C_DKGRAY);
    vid_fillrect(x + S(6),  y + S(6),  S(3),  S(3), C_SHADOW);    /* wrench */
    vid_pixel   (x + S(6),  y + S(6),  C_HILIGHT);
    vid_fillrect(x + S(8),  y + S(8),  S(10), S(2), C_DKGRAY);
    vid_fillrect(x + S(22), y + S(5),  S(2),  S(6), C_YELLOW);    /* driver */
    vid_rect    (x + S(22), y + S(5),  S(2),  S(6), C_BLACK);
    vid_fillrect(x + S(22), y + S(10), S(2),  S(4), C_DKGRAY);
    vid_fillrect(x + S(3),  y + S(11), S(26), S(15), C_RED);      /* case   */
    vid_rect    (x + S(3),  y + S(11), S(26), S(15), C_BLACK);
    ui_raise    (x + S(4),  y + S(11), S(24), S(6));             /* lid     */
    vid_hline   (x + S(4),  y + S(17), S(24), C_BLACK);           /* seam   */
    vid_fillrect(x + S(12), y + S(15), S(8),  S(4), C_YELLOW);    /* latch  */
    vid_rect    (x + S(12), y + S(15), S(8),  S(4), C_BLACK);
    vid_fillrect(x + S(14), y + S(16), S(4),  S(2), C_DKYELLOW);
}

static void icon_arcade(int x, int y)
{
    vid_fillrect(x + S(5),  y + S(20), S(22), S(8), C_RED);       /* base   */
    vid_rect    (x + S(5),  y + S(20), S(22), S(8), C_BLACK);
    vid_hline   (x + S(6),  y + S(21), S(20), C_YELLOW);          /* sheen  */
    vid_fillrect(x + S(14), y + S(9),  S(4), S(12), C_DKGRAY);    /* shaft  */
    vid_rect    (x + S(14), y + S(9),  S(4), S(12), C_BLACK);
    vid_vline   (x + S(15), y + S(10), S(10), C_HILIGHT);
    vid_fillrect(x + S(11), y + S(4),  S(10), S(7), C_RED);       /* ball   */
    vid_rect    (x + S(11), y + S(4),  S(10), S(7), C_BLACK);
    vid_fillrect(x + S(13), y + S(5),  S(4), S(2), C_WHITE);      /* glint  */
    vid_fillrect(x + S(8),  y + S(23), S(4), S(3), C_GREEN);      /* buttons */
    vid_rect    (x + S(8),  y + S(23), S(4), S(3), C_BLACK);
    vid_fillrect(x + S(20), y + S(23), S(4), S(3), C_YELLOW);
    vid_rect    (x + S(20), y + S(23), S(4), S(3), C_BLACK);
}

static void icon_colors(int x, int y)
{
    vid_fillrect(x + S(5),  y + S(5),  S(11), S(11), C_RED);
    vid_fillrect(x + S(16), y + S(5),  S(11), S(11), C_GREEN);
    vid_fillrect(x + S(5),  y + S(16), S(11), S(11), C_BLUE);
    vid_fillrect(x + S(16), y + S(16), S(11), S(11), C_YELLOW);
    vid_rect    (x + S(5),  y + S(5),  S(22), S(22), C_BLACK);
    vid_vline   (x + S(16), y + S(5),  S(22), C_BLACK);
    vid_hline   (x + S(5),  y + S(16), S(22), C_BLACK);
}

static void icon_demo(int x, int y)
{
    vid_fillrect(x + S(14), y + S(4),  S(4), S(24), C_YELLOW);    /* rays   */
    vid_fillrect(x + S(4),  y + S(14), S(24), S(4), C_YELLOW);
    vid_pixel   (x + S(8),  y + S(8),  C_CYAN);                   /* diag   */
    vid_pixel   (x + S(9),  y + S(9),  C_CYAN);
    vid_pixel   (x + S(23), y + S(8),  C_CYAN);
    vid_pixel   (x + S(22), y + S(9),  C_CYAN);
    vid_pixel   (x + S(8),  y + S(23), C_CYAN);
    vid_pixel   (x + S(9),  y + S(22), C_CYAN);
    vid_pixel   (x + S(23), y + S(23), C_CYAN);
    vid_pixel   (x + S(22), y + S(22), C_CYAN);
    vid_fillrect(x + S(12), y + S(12), S(8), S(8), C_RED);        /* core   */
    vid_rect    (x + S(12), y + S(12), S(8), S(8), C_BLACK);
    vid_fillrect(x + S(14), y + S(14), S(4), S(4), C_WHITE);      /* hot    */
}

static void icon_eyes(int x, int y)
{
    vid_fillrect(x + S(4),  y + S(7),  S(11), S(19), C_WHITE);    /* left  */
    vid_rect    (x + S(4),  y + S(7),  S(11), S(19), C_BLACK);
    vid_fillrect(x + S(17), y + S(7),  S(11), S(19), C_WHITE);    /* right */
    vid_rect    (x + S(17), y + S(7),  S(11), S(19), C_BLACK);
    vid_fillrect(x + S(8),  y + S(15), S(4),  S(6),  C_BLUE);     /* pupils*/
    vid_fillrect(x + S(21), y + S(15), S(4),  S(6),  C_BLUE);
    vid_pixel   (x + S(9),  y + S(16), C_WHITE);                  /* glints*/
    vid_pixel   (x + S(22), y + S(16), C_WHITE);
}

/* ---- v0.19: every game and tool gets its OWN face -------------------- */

static void icon_fifteen(int x, int y)
{
    int r, c;
    ui_fill_face(x + S(5), y + S(5), S(22), S(22));
    vid_rect    (x + S(5), y + S(5), S(22), S(22), C_BLACK);
    ui_sink     (x + S(6), y + S(6), S(20), S(20));
    for (r = 0; r < 3; ++r)
        for (c = 0; c < 3; ++c) {
            if (r == 2 && c == 2)
                continue;                          /* the sliding gap      */
            ui_fill_face(x + S(7) + c * S(7), y + S(7) + r * S(7), S(6), S(6));
            vid_rect    (x + S(7) + c * S(7), y + S(7) + r * S(7), S(6), S(6),
                         C_BLACK);
            vid_fillrect(x + S(9) + c * S(7), y + S(9) + r * S(7),
                         S(2), S(2), C_BLUE);
        }
}

static void icon_ttt(int x, int y)
{
    vid_fillrect(x + S(4), y + S(4), S(24), S(24), C_WHITE);
    vid_rect (x + S(4),  y + S(4),  S(24), S(24), C_BLACK);
    vid_vline(x + S(12), y + S(5),  S(22), C_DKGRAY);   /* the grid        */
    vid_vline(x + S(20), y + S(5),  S(22), C_DKGRAY);
    vid_hline(x + S(5),  y + S(12), S(22), C_DKGRAY);
    vid_hline(x + S(5),  y + S(20), S(22), C_DKGRAY);
    font_draw_char(x + S(6),  y + S(5),  'X', C_BLUE);
    font_draw_char(x + S(14), y + S(13), 'O', C_RED);
    font_draw_char(x + S(22), y + S(21), 'X', C_BLUE);
}

static void icon_mine(int x, int y)
{
    vid_fillrect(x + S(10), y + S(10), S(12), S(12), C_BLACK);   /* body   */
    vid_fillrect(x + S(12), y + S(8),  S(8),  S(16), C_BLACK);
    vid_fillrect(x + S(8),  y + S(12), S(16), S(8),  C_BLACK);
    vid_fillrect(x + S(15), y + S(4),  S(2),  S(24), C_BLACK);   /* spikes */
    vid_fillrect(x + S(4),  y + S(15), S(24), S(2),  C_BLACK);
    vid_fillrect(x + S(7),  y + S(7),  S(3),  S(3),  C_BLACK);
    vid_fillrect(x + S(22), y + S(7),  S(3),  S(3),  C_BLACK);
    vid_fillrect(x + S(7),  y + S(22), S(3),  S(3),  C_BLACK);
    vid_fillrect(x + S(22), y + S(22), S(3),  S(3),  C_BLACK);
    vid_fillrect(x + S(12), y + S(12), S(5),  S(5),  C_SHADOW);  /* glint  */
    vid_fillrect(x + S(12), y + S(12), S(3),  S(3),  C_WHITE);
}

static void icon_reversi(int x, int y)
{
    vid_fillrect(x + S(4), y + S(4), S(24), S(24), C_GREEN);     /* board  */
    vid_rect    (x + S(4), y + S(4), S(24), S(24), C_BLACK);
    vid_vline   (x + S(16), y + S(5),  S(22), C_DKGRAY);         /* grid   */
    vid_hline   (x + S(5),  y + S(16), S(22), C_DKGRAY);
    vid_fillrect(x + S(7),  y + S(7),  S(8), S(8), C_BLACK);     /* discs  */
    vid_rect    (x + S(7),  y + S(7),  S(8), S(8), C_DKGRAY);
    vid_fillrect(x + S(17), y + S(7),  S(8), S(8), C_WHITE);
    vid_rect    (x + S(17), y + S(7),  S(8), S(8), C_DKGRAY);
    vid_fillrect(x + S(7),  y + S(17), S(8), S(8), C_WHITE);
    vid_rect    (x + S(7),  y + S(17), S(8), S(8), C_DKGRAY);
    vid_fillrect(x + S(17), y + S(17), S(8), S(8), C_BLACK);
    vid_rect    (x + S(17), y + S(17), S(8), S(8), C_DKGRAY);
}

static void icon_snake(int x, int y)
{
    vid_fillrect(x + S(5),  y + S(6),  S(18), S(4), C_GREEN);    /* coils  */
    vid_fillrect(x + S(19), y + S(6),  S(4),  S(9), C_GREEN);
    vid_fillrect(x + S(9),  y + S(11), S(14), S(4), C_GREEN);
    vid_fillrect(x + S(9),  y + S(11), S(4),  S(9), C_GREEN);
    vid_fillrect(x + S(9),  y + S(16), S(14), S(4), C_GREEN);
    vid_fillrect(x + S(19), y + S(16), S(4),  S(9), C_GREEN);
    vid_fillrect(x + S(5),  y + S(21), S(18), S(4), C_GREEN);    /* head   */
    vid_fillrect(x + S(3),  y + S(20), S(6),  S(6), C_GREEN);
    vid_rect    (x + S(3),  y + S(20), S(6),  S(6), C_BLACK);
    vid_pixel   (x + S(5),  y + S(22), C_BLACK);                 /* eye    */
    vid_fillrect(x + S(1),  y + S(23), S(2),  S(1), C_RED);      /* tongue */
    vid_fillrect(x + S(26), y + S(7),  S(4),  S(4), C_RED);      /* apple  */
    vid_rect    (x + S(26), y + S(7),  S(4),  S(4), C_BLACK);
}

static void icon_breaker(int x, int y)
{
    int r;
    static const u8 brk[3] = { C_RED, C_YELLOW, C_GREEN };
    for (r = 0; r < 3; ++r) {                                   /* bricks */
        int by = y + S(5) + r * S(4);
        vid_fillrect(x + S(4),  by, S(11), S(4), brk[r]);
        vid_rect    (x + S(4),  by, S(11), S(4), C_BLACK);
        vid_fillrect(x + S(17), by, S(11), S(4), brk[r]);
        vid_rect    (x + S(17), by, S(11), S(4), C_BLACK);
    }
    vid_fillrect(x + S(18), y + S(20), S(3),  S(3), C_WHITE);    /* ball   */
    vid_rect    (x + S(18), y + S(20), S(3),  S(3), C_BLACK);
    vid_fillrect(x + S(10), y + S(26), S(12), S(3), C_LTBLUE);   /* paddle */
    vid_rect    (x + S(10), y + S(26), S(12), S(3), C_BLACK);
}

static void icon_echo(int x, int y)
{
    /* The four Simon quadrants, one lit. */
    vid_fillrect(x + S(6),  y + S(6),  S(10), S(10), C_RED);
    vid_fillrect(x + S(17), y + S(6),  S(10), S(10), C_YELLOW);
    vid_fillrect(x + S(6),  y + S(17), S(10), S(10), C_GREEN);
    vid_fillrect(x + S(17), y + S(17), S(10), S(10), C_BLUE);
    vid_rect    (x + S(6),  y + S(6),  S(21), S(21), C_BLACK);
    vid_fillrect(x + S(15), y + S(15), S(3),  S(3),  C_BLACK);   /* hub    */
    vid_fillrect(x + S(19), y + S(8),  S(6),  S(6),  C_WHITE);   /* "lit"  */
}

static void icon_quadrix(int x, int y)
{
    vid_fillrect(x + S(5),  y + S(19), S(6), S(6), C_CYAN);      /* well   */
    vid_rect    (x + S(5),  y + S(19), S(6), S(6), C_BLACK);
    vid_fillrect(x + S(11), y + S(19), S(6), S(6), C_CYAN);
    vid_rect    (x + S(11), y + S(19), S(6), S(6), C_BLACK);
    vid_fillrect(x + S(11), y + S(13), S(6), S(6), C_CYAN);
    vid_rect    (x + S(11), y + S(13), S(6), S(6), C_BLACK);
    vid_fillrect(x + S(17), y + S(19), S(6), S(6), C_CYAN);
    vid_rect    (x + S(17), y + S(19), S(6), S(6), C_BLACK);
    vid_fillrect(x + S(17), y + S(5),  S(6), S(6), C_RED);       /* faller */
    vid_rect    (x + S(17), y + S(5),  S(6), S(6), C_BLACK);
    vid_fillrect(x + S(23), y + S(5),  S(6), S(6), C_RED);
    vid_rect    (x + S(23), y + S(5),  S(6), S(6), C_BLACK);
    vid_hline   (x + S(4),  y + S(26), S(24), C_BLACK);          /* floor  */
}

static void icon_depot(int x, int y)
{
    vid_fillrect(x + S(7),  y + S(9),  S(18), S(16), C_DKYELLOW); /* crate */
    vid_rect    (x + S(7),  y + S(9),  S(18), S(16), C_BLACK);
    vid_hline   (x + S(8),  y + S(16), S(16), C_BLACK);           /* slats */
    vid_vline   (x + S(15), y + S(10), S(14), C_BLACK);
    vid_fillrect(x + S(4),  y + S(26), S(24), S(2), C_SHADOW);    /* bay   */
    vid_fillrect(x + S(12), y + S(4),  S(8),  S(3), C_RED);       /* mark  */
}

static void icon_patience(int x, int y)
{
    vid_fillrect(x + S(12), y + S(7),  S(15), S(21), C_RED);      /* back  */
    vid_rect    (x + S(12), y + S(7),  S(15), S(21), C_BLACK);
    vid_fillrect(x + S(5),  y + S(4),  S(15), S(21), C_WHITE);    /* ace   */
    vid_rect    (x + S(5),  y + S(4),  S(15), S(21), C_BLACK);
    font_draw_char(x + S(6),  y + S(5),  'A', C_BLACK);
    /* A little spade: triangle body over a stem. */
    vid_fillrect(x + S(11), y + S(13), S(3), S(3), C_BLACK);
    vid_fillrect(x + S(10), y + S(15), S(5), S(3), C_BLACK);
    vid_fillrect(x + S(12), y + S(18), S(1), S(3), C_BLACK);
}

static void icon_lightsout(int x, int y)
{
    int r, c;
    vid_fillrect(x + S(4), y + S(4), S(24), S(24), C_DKGRAY);
    vid_rect    (x + S(4), y + S(4), S(24), S(24), C_BLACK);
    for (r = 0; r < 3; ++r)
        for (c = 0; c < 3; ++c) {
            bool_t lit = ((r + c) & 1) ? FALSE : TRUE;
            vid_fillrect(x + S(6) + c * S(7), y + S(6) + r * S(7),
                         S(6), S(6), lit ? C_YELLOW : C_SHADOW);
            vid_rect    (x + S(6) + c * S(7), y + S(6) + r * S(7),
                         S(6), S(6), C_BLACK);
        }
}

static void icon_fractal(int x, int y)
{
    vid_fillrect(x + S(4),  y + S(4),  S(24), S(24), C_BLACK);   /* deep   */
    vid_rect    (x + S(4),  y + S(4),  S(24), S(24), C_DKGRAY);
    vid_fillrect(x + S(12), y + S(9),  S(10), S(14), C_BLUE);    /* set    */
    vid_fillrect(x + S(9),  y + S(12), S(16), S(8),  C_BLUE);
    vid_fillrect(x + S(7),  y + S(14), S(4),  S(4),  C_CYAN);    /* bulb   */
    vid_fillrect(x + S(14), y + S(11), S(6),  S(10), C_CYAN);
    vid_fillrect(x + S(16), y + S(13), S(3),  S(6),  C_WHITE);
}

static void icon_cardfile(int x, int y)
{
    vid_fillrect(x + S(6),  y + S(12), S(21), S(15), C_WHITE);   /* card 2 */
    vid_rect    (x + S(6),  y + S(12), S(21), S(15), C_BLACK);
    vid_fillrect(x + S(6),  y + S(9),  S(8),  S(4),  C_CREAM);   /* tab 2  */
    vid_rect    (x + S(6),  y + S(9),  S(8),  S(4),  C_BLACK);
    vid_fillrect(x + S(4),  y + S(8),  S(21), S(15), C_CREAM);   /* card 1 */
    vid_rect    (x + S(4),  y + S(8),  S(21), S(15), C_BLACK);
    vid_fillrect(x + S(16), y + S(5),  S(8),  S(4),  C_WHITE);   /* tab 1  */
    vid_rect    (x + S(16), y + S(5),  S(8),  S(4),  C_BLACK);
    vid_hline   (x + S(7),  y + S(13), S(14), C_LTBLUE);         /* rules  */
    vid_hline   (x + S(7),  y + S(17), S(14), C_LTBLUE);
}

static void icon_bench(int x, int y)
{
    vid_fillrect(x + S(5),  y + S(27), S(22), S(2),  C_BLACK);   /* floor  */
    vid_fillrect(x + S(6),  y + S(19), S(6),  S(7),  C_DKGRAY);  /* bars   */
    vid_rect    (x + S(6),  y + S(19), S(6),  S(7),  C_BLACK);
    vid_fillrect(x + S(13), y + S(13), S(6),  S(13), C_BLUE);
    vid_rect    (x + S(13), y + S(13), S(6),  S(13), C_BLACK);
    vid_fillrect(x + S(20), y + S(6),  S(6),  S(20), C_GREEN);
    vid_rect    (x + S(20), y + S(6),  S(6),  S(20), C_BLACK);
    vid_fillrect(x + S(22), y + S(3),  S(3),  S(3),  C_YELLOW);  /* spark  */
    vid_rect    (x + S(22), y + S(3),  S(3),  S(3),  C_BLACK);
}

static void icon_oracle(int x, int y)
{
    /* The all-seeing eye of Delphi: white almond, ringed iris, dark pupil. */
    vid_fillrect(x + S(8),  y + S(12), S(16), S(8), C_WHITE);    /* almond */
    vid_fillrect(x + S(6),  y + S(14), S(20), S(4), C_WHITE);
    vid_fillrect(x + S(11), y + S(10), S(10), S(12), C_WHITE);
    vid_fillrect(x + S(11), y + S(11), S(10), S(10), C_CYAN);    /* iris   */
    vid_fillrect(x + S(13), y + S(12), S(6),  S(8),  C_BLUE);
    vid_fillrect(x + S(14), y + S(13), S(4),  S(6),  C_BLACK);   /* pupil  */
    vid_pixel   (x + S(14), y + S(13), C_WHITE);                 /* glint  */
    vid_hline   (x + S(8),  y + S(11), S(16), C_BLACK);          /* lids   */
    vid_hline   (x + S(8),  y + S(21), S(16), C_BLACK);
    vid_vline   (x + S(6),  y + S(13), S(5),  C_BLACK);
    vid_vline   (x + S(25), y + S(13), S(5),  C_BLACK);
}

static void icon_settings(int x, int y)
{
    int i;
    static const int TY[3] = { 9, 15, 21 };
    static const int TX[3] = { 9, 19, 13 };
    static const u8  TC[3] = { C_RED, C_GREEN, C_BLUE };
    ui_fill_face(x + S(4), y + S(4), S(24), S(24));
    vid_rect    (x + S(4), y + S(4), S(24), S(24), C_BLACK);
    ui_raise    (x + S(5), y + S(5), S(22), S(22));
    for (i = 0; i < 3; ++i) {
        vid_fillrect(x + S(7),  y + S(TY[i]),     S(18), S(2), C_SHADOW);
        vid_fillrect(x + S(TX[i]), y + S(TY[i]-2), S(4),  S(6), C_DKGRAY);
        vid_rect    (x + S(TX[i]), y + S(TY[i]-2), S(4),  S(6), C_BLACK);
        vid_fillrect(x + S(TX[i]+1), y + S(TY[i]), S(2),  S(2), TC[i]);
    }
}

/* A wall calendar: two rings, a red month band and a grid of days with
   one of them ringed.  The Calendar had been sharing the Agenda's
   clipboard, so the Toolbox showed the same picture twice and a desktop
   icon for either was a coin toss. */
static void icon_calendar(int x, int y)
{
    int r, c;
    vid_fillrect(x + S(11), y + S(1),  S(2), S(5), C_DKGRAY);    /* rings  */
    vid_fillrect(x + S(19), y + S(1),  S(2), S(5), C_DKGRAY);
    vid_fillrect(x + S(4),  y + S(4),  S(24), S(24), C_WHITE);   /* page   */
    vid_rect    (x + S(4),  y + S(4),  S(24), S(24), C_BLACK);
    vid_fillrect(x + S(5),  y + S(5),  S(22), S(6),  C_RED);     /* month  */
    /* Four columns of three, which is as many marks as read as "days"
       rather than as noise at 32 pixels. */
    for (r = 0; r < 3; ++r)
        for (c = 0; c < 4; ++c)
            vid_fillrect(x + S(7 + c * 5), y + S(14 + r * 5),
                         S(3), S(3), C_DKGRAY);
    vid_fillrect(x + S(12), y + S(19), S(3), S(3), C_BLUE);      /* today  */
    vid_rect    (x + S(11), y + S(18), S(5), S(5), C_BLUE);
}

static void icon_agenda(int x, int y)
{
    vid_fillrect(x + S(6),  y + S(4),  S(20), S(24), C_CREAM);   /* board  */
    vid_rect    (x + S(6),  y + S(4),  S(20), S(24), C_BLACK);
    vid_fillrect(x + S(12), y + S(2),  S(8),  S(4),  C_DKGRAY);  /* clip   */
    vid_rect    (x + S(12), y + S(2),  S(8),  S(4),  C_BLACK);
    vid_fillrect(x + S(9),  y + S(10), S(4),  S(4),  C_WHITE);   /* boxes  */
    vid_rect    (x + S(9),  y + S(10), S(4),  S(4),  C_BLACK);
    font_draw_char(x + S(9), y + S(9), 'x', C_GREEN);
    vid_hline   (x + S(15), y + S(12), S(8),  C_DKGRAY);
    vid_fillrect(x + S(9),  y + S(17), S(4),  S(4),  C_WHITE);
    vid_rect    (x + S(9),  y + S(17), S(4),  S(4),  C_BLACK);
    vid_hline   (x + S(15), y + S(19), S(8),  C_DKGRAY);
    vid_fillrect(x + S(9),  y + S(24), S(4),  S(4),  C_WHITE);
    vid_rect    (x + S(9),  y + S(24), S(4),  S(4),  C_BLACK);
    vid_hline   (x + S(15), y + S(26), S(6),  C_DKGRAY);
}

static void icon_peek(int x, int y)
{
    font_draw_char(x + S(4),  y + S(4),  'A', C_DKGRAY);  /* faded bytes  */
    font_draw_char(x + S(12), y + S(4),  '7', C_DKGRAY);
    font_draw_char(x + S(20), y + S(4),  'F', C_DKGRAY);
    font_draw_char(x + S(4),  y + S(13), '0', C_DKGRAY);
    font_draw_char(x + S(4),  y + S(22), 'C', C_DKGRAY);
    /* Magnifier: ring, bright lens with a byte in focus, handle. */
    vid_fillrect(x + S(11), y + S(9),  S(14), S(14), C_LTBLUE);
    vid_rect    (x + S(11), y + S(9),  S(14), S(14), C_BLACK);
    vid_rect    (x + S(12), y + S(10), S(12), S(12), C_BLACK);
    font_draw_char(x + S(14), y + S(12), 'E', C_BLACK);
    vid_fillrect(x + S(23), y + S(21), S(3), S(3), C_BLACK);   /* joint  */
    vid_fillrect(x + S(25), y + S(23), S(3), S(3), C_BLACK);
    vid_fillrect(x + S(27), y + S(25), S(3), S(3), C_BLACK);
}

/* The Gramophone: a skinned player, its black screen full of a green-to-red
   spectrum with peak caps, a transport row beneath - the desktop echo of
   the Winamp-style Gramophone window. */
static void icon_media(int x, int y)
{
    static const int bh[8] = { 3, 7, 4, 8, 5, 9, 6, 4 }; /* bar heights     */
    int i;
    ui_fill_face(x + S(2), y + S(5), S(28), S(22));   /* player body        */
    vid_rect    (x + S(2), y + S(5), S(28), S(22), C_BLACK);
    ui_raise    (x + S(3), y + S(6), S(26), S(20));
    vid_fillrect(x + S(4), y + S(7), S(24), S(12), C_BLACK);   /* screen     */
    vid_rect    (x + S(4), y + S(7), S(24), S(12), C_DKGRAY);
    for (i = 0; i < 8; ++i) {                          /* spectrum bars      */
        int h  = bh[i];
        int bx = x + S(5) + i * S(3);
        int by = y + S(18) - S(h);
        u8  col = (h >= 8) ? C_RED : (h >= 5) ? C_YELLOW : C_GREEN;
        vid_fillrect(bx, by, S(2), S(h), col);
        vid_pixel   (bx, by - S(1), C_WHITE);          /* peak cap           */
    }
    vid_fillrect(x + S(5),  y + S(21), S(3), S(4), C_GREEN);   /* play glyph */
    vid_fillrect(x + S(9),  y + S(21), S(3), S(4), C_DKGRAY);  /* buttons    */
    vid_fillrect(x + S(13), y + S(21), S(3), S(4), C_DKGRAY);
    vid_fillrect(x + S(17), y + S(21), S(3), S(4), C_DKGRAY);
    vid_fillrect(x + S(22), y + S(21), S(6), S(4), C_BLUE);    /* slider well */
    vid_rect    (x + S(22), y + S(21), S(6), S(4), C_BLACK);
    vid_fillrect(x + S(24), y + S(21), S(2), S(4), C_LTBLUE);
}

/* Find File: a manila folder under a big magnifying glass. */
static void icon_find(int x, int y)
{
    vid_fillrect(x + S(4),  y + S(8),  S(9), S(3),  C_DKYELLOW);  /* tab   */
    vid_rect    (x + S(4),  y + S(8),  S(9), S(3),  C_BLACK);
    vid_fillrect(x + S(3),  y + S(11), S(22), S(14), C_YELLOW);   /* flap  */
    vid_rect    (x + S(3),  y + S(11), S(22), S(14), C_BLACK);
    vid_hline   (x + S(4),  y + S(12), S(20), C_CREAM);           /* sheen */
    vid_fillrect(x + S(14), y + S(6),  S(12), S(12), C_LTBLUE);   /* lens  */
    vid_rect    (x + S(14), y + S(6),  S(12), S(12), C_BLACK);
    vid_rect    (x + S(15), y + S(7),  S(10), S(10), C_BLACK);
    vid_pixel   (x + S(17), y + S(9),  C_WHITE);                  /* glint */
    vid_fillrect(x + S(24), y + S(17), S(3), S(3), C_BLACK);      /* grip  */
    vid_fillrect(x + S(26), y + S(19), S(3), S(3), C_BLACK);
    vid_fillrect(x + S(28), y + S(21), S(3), S(3), C_BLACK);
}

/* Picture Show: a framed little landscape - sky, sun and a green hill. */
static void icon_picture(int x, int y)
{
    ui_fill_face(x + S(3),  y + S(5),  S(26), S(22));            /* frame  */
    vid_rect    (x + S(3),  y + S(5),  S(26), S(22), C_BLACK);
    ui_raise    (x + S(4),  y + S(6),  S(24), S(20));
    vid_fillrect(x + S(6),  y + S(8),  S(20), S(16), C_LTBLUE);  /* sky    */
    vid_rect    (x + S(6),  y + S(8),  S(20), S(16), C_BLACK);
    vid_fillrect(x + S(19), y + S(10), S(4),  S(4),  C_YELLOW);  /* sun    */
    vid_fillrect(x + S(7),  y + S(17), S(18), S(6),  C_GREEN);   /* hill   */
    vid_fillrect(x + S(7),  y + S(15), S(8),  S(4),  C_GREEN);
    vid_hline   (x + S(7),  y + S(15), S(8),  C_WHITE);          /* crest  */
}

/* Cinema: a strip of film with sprocket holes and a play triangle. */
static void icon_cinema(int x, int y)
{
    int i;
    ui_fill_face(x + S(4), y + S(4), S(24), S(24));
    vid_rect    (x + S(4), y + S(4), S(24), S(24), C_BLACK);
    ui_raise    (x + S(5), y + S(5), S(22), S(22));
    vid_fillrect(x + S(7), y + S(6), S(18), S(20), C_BLACK);    /* the film  */
    for (i = 0; i < 4; ++i) {                                   /* sprockets */
        vid_fillrect(x + S(8),  y + S(7) + i * S(5), S(2), S(2), C_WHITE);
        vid_fillrect(x + S(22), y + S(7) + i * S(5), S(2), S(2), C_WHITE);
    }
    vid_fillrect(x + S(11), y + S(7),  S(10), S(11), C_BLUE);   /* one frame */
    vid_fillrect(x + S(11), y + S(20), S(10), S(3),  C_DKGRAY);
    vid_fillrect(x + S(13), y + S(9),  S(2), S(7), C_YELLOW);   /* play tri  */
    vid_fillrect(x + S(15), y + S(10), S(2), S(5), C_YELLOW);
    vid_fillrect(x + S(17), y + S(11), S(2), S(3), C_YELLOW);
}

/* Corral: a pale pen split by a growing fence, a ball on each side. */
static void icon_corral(int x, int y)
{
    vid_fillrect(x + S(3),  y + S(5),  S(26), S(22), C_BLACK);   /* pen    */
    vid_rect    (x + S(2),  y + S(4),  S(28), S(24), C_DKGRAY);
    vid_fillrect(x + S(20), y + S(5),  S(9),  S(22), C_FACE);    /* taken  */
    vid_fillrect(x + S(15), y + S(5),  S(3),  S(13), C_RED);     /* arm A  */
    vid_fillrect(x + S(15), y + S(18), S(3),  S(9),  C_CYAN);    /* arm B  */
    vid_fillrect(x + S(7),  y + S(9),  S(5),  S(5),  C_WHITE);   /* balls  */
    vid_fillrect(x + S(9),  y + S(19), S(5),  S(5),  C_WHITE);
    vid_pixel   (x + S(8),  y + S(10), C_CYAN);
    vid_pixel   (x + S(10), y + S(20), C_CYAN);
}

/* Typing Tutor: a beige keyboard, home-row caps, one key lit blue. */
static void icon_typist(int x, int y)
{
    int k;
    vid_fillrect(x + S(2),  y + S(12), S(28), S(14), C_FACE);    /* body   */
    vid_rect    (x + S(2),  y + S(12), S(28), S(14), C_BLACK);
    vid_hline   (x + S(3),  y + S(25), S(26), C_SHADOW);
    for (k = 0; k < 6; ++k)                                      /* keys   */
        vid_fillrect(x + S(4 + k * 4), y + S(15), S(3), S(3),
                     (k == 2) ? C_BLUE : C_WHITE);
    for (k = 0; k < 5; ++k)
        vid_fillrect(x + S(6 + k * 4), y + S(19), S(3), S(3), C_WHITE);
    vid_fillrect(x + S(9),  y + S(22), S(14), S(2), C_WHITE);    /* space  */
    vid_fillrect(x + S(14), y + S(4),  S(4),  S(6), C_CREAM);    /* sheet  */
    vid_rect    (x + S(14), y + S(4),  S(4),  S(6), C_DKGRAY);
}

/* Pong: the black court, two paddles mid-rally and the ball. */
static void icon_pong(int x, int y)
{
    int k;
    vid_fillrect(x + S(3),  y + S(5),  S(26), S(22), C_BLACK);   /* court  */
    vid_rect    (x + S(2),  y + S(4),  S(28), S(24), C_DKGRAY);
    for (k = 0; k < 5; ++k)                                      /* net    */
        vid_fillrect(x + S(15), y + S(6 + k * 4), S(2), S(2), C_SHADOW);
    vid_fillrect(x + S(5),  y + S(9),  S(2), S(8), C_WHITE);     /* player */
    vid_fillrect(x + S(25), y + S(15), S(2), S(8), C_CYAN);      /* house  */
    vid_fillrect(x + S(19), y + S(11), S(3), S(3), C_YELLOW);    /* ball   */
}

/* 2048: the tile board mid-merge - two 2s above, a 4 and a gap below. */
static void icon_2048(int x, int y)
{
    vid_fillrect(x + S(3),  y + S(4),  S(26), S(24), C_SHADOW);  /* board  */
    vid_rect    (x + S(3),  y + S(4),  S(26), S(24), C_BLACK);
    vid_fillrect(x + S(5),  y + S(6),  S(10), S(9),  C_CREAM);   /* "2"    */
    vid_fillrect(x + S(17), y + S(6),  S(10), S(9),  C_CREAM);   /* "2"    */
    vid_fillrect(x + S(5),  y + S(17), S(10), S(9),  C_YELLOW);  /* "4"    */
    vid_fillrect(x + S(17), y + S(17), S(10), S(9),  C_FACE);    /* empty  */
    /* Chunky glyphs: each 2 is a top bar, slash and base; the 4 strokes. */
    vid_hline(x + S(7),  y + S(8),  S(6), C_BLACK);
    vid_pixel(x + S(12), y + S(9),  C_BLACK);
    vid_hline(x + S(8),  y + S(10), S(4), C_BLACK);
    vid_hline(x + S(7),  y + S(12), S(6), C_BLACK);
    vid_hline(x + S(19), y + S(8),  S(6), C_BLACK);
    vid_pixel(x + S(24), y + S(9),  C_BLACK);
    vid_hline(x + S(20), y + S(10), S(4), C_BLACK);
    vid_hline(x + S(19), y + S(12), S(6), C_BLACK);
    vid_vline(x + S(8),  y + S(19), S(3), C_BLACK);
    vid_hline(x + S(8),  y + S(22), S(5), C_BLACK);
    vid_vline(x + S(11), y + S(19), S(6), C_BLACK);
}

void ui_icon(int kind, int x, int y)
{
    switch (kind) {
    case ICON_FOLDER:   icon_folder(x, y);   break;
    case ICON_TERMINAL: icon_terminal(x, y); break;
    case ICON_SYSTEM:   icon_system(x, y);   break;
    case ICON_DRAWER:   icon_drawer(x, y);   break;
    case ICON_INFO:     icon_info(x, y);     break;
    case ICON_DISK:     icon_disk(x, y);     break;
    case ICON_HDD:      icon_hdd(x, y);      break;
    case ICON_NETDRV:   icon_netdrv(x, y);   break;
    case ICON_NOTE:     icon_note(x, y);     break;
    case ICON_CALC:     icon_calc(x, y);     break;
    case ICON_CLOCK:    icon_clock(x, y);    break;
    case ICON_PAINT:    icon_paint(x, y);    break;
    case ICON_CHARS:    icon_chars(x, y);    break;
    case ICON_MUSIC:    icon_music(x, y);    break;
    case ICON_GAME:     icon_game(x, y);     break;
    case ICON_GAUGE:    icon_gauge(x, y);    break;
    case ICON_WATCH:    icon_watch(x, y);    break;
    case ICON_EXIT:     icon_exit(x, y);     break;
    case ICON_TOOLS:    icon_tools(x, y);    break;
    case ICON_ARCADE:   icon_arcade(x, y);   break;
    case ICON_DEMO:     icon_demo(x, y);     break;
    case ICON_COLORS:   icon_colors(x, y);   break;
    case ICON_EYES:     icon_eyes(x, y);     break;
    case ICON_FIFTEEN:  icon_fifteen(x, y);  break;
    case ICON_TTT:      icon_ttt(x, y);      break;
    case ICON_MINE:     icon_mine(x, y);     break;
    case ICON_REVERSI:  icon_reversi(x, y);  break;
    case ICON_SNAKE:    icon_snake(x, y);    break;
    case ICON_BREAKER:  icon_breaker(x, y);  break;
    case ICON_ECHO:     icon_echo(x, y);     break;
    case ICON_QUADRIX:  icon_quadrix(x, y);  break;
    case ICON_DEPOT:    icon_depot(x, y);    break;
    case ICON_PATIENCE: icon_patience(x, y); break;
    case ICON_LIGHTS:   icon_lightsout(x, y); break;
    case ICON_FRACTAL:  icon_fractal(x, y);  break;
    case ICON_CARDFILE: icon_cardfile(x, y); break;
    case ICON_CALENDAR: icon_calendar(x, y); break;
    case ICON_BENCH:    icon_bench(x, y);    break;
    case ICON_ORACLE:   icon_oracle(x, y);   break;
    case ICON_SETTINGS: icon_settings(x, y); break;
    case ICON_AGENDA:   icon_agenda(x, y);   break;
    case ICON_PEEK:     icon_peek(x, y);     break;
    case ICON_MEDIA:    icon_media(x, y);    break;
    case ICON_CINEMA:   icon_cinema(x, y);   break;
    case ICON_COMPUTER: icon_computer(x, y); break;
    case ICON_FIND:     icon_find(x, y);     break;
    case ICON_PICTURE:  icon_picture(x, y); break;
    case ICON_CORRAL:   icon_corral(x, y);   break;
    case ICON_TYPIST:   icon_typist(x, y);   break;
    case ICON_PONG:     icon_pong(x, y);     break;
    case ICON_2048:     icon_2048(x, y);     break;
    case ICON_FILE:
    default:            icon_file(x, y);     break;
    }
}

/* ---- The Castalia castle logo ---------------------------------------- */

/* A downward-widening filled triangle (a conical tower roof), device px. */
static void roof(int cx, int y, int halfw, int h, u8 col)
{
    int r;
    for (r = 0; r < h; ++r) {
        int w = 1 + (2 * halfw - 1) * r / (h > 1 ? h - 1 : 1);
        vid_hline(cx - w / 2, y + r, w, col);
    }
}

void ui_castle(int x, int y, int u, bool_t flag_up)
{
#define K(v) ((v) * (u))
    int m;
    /* connecting curtain wall */
    vid_fillrect(x + K(4),  y + K(11), K(10), K(9), C_DKYELLOW);
    /* wall battlements (merlons) */
    for (m = 4; m <= 12; m += 2)
        vid_fillrect(x + K(m), y + K(9), K(1), K(2), C_DKYELLOW);
    /* the three towers */
    vid_fillrect(x + K(0),  y + K(7),  K(4), K(13), C_DKYELLOW);   /* left  */
    vid_fillrect(x + K(14), y + K(7),  K(4), K(13), C_DKYELLOW);   /* right */
    vid_fillrect(x + K(6),  y + K(4),  K(6), K(16), C_DKYELLOW);   /* keep  */
    /* sunlit left edges */
    vid_vline(x + K(0),  y + K(7), K(13), C_YELLOW);
    vid_vline(x + K(6),  y + K(4), K(16), C_YELLOW);
    vid_vline(x + K(14), y + K(7), K(13), C_YELLOW);
    /* red conical roofs */
    roof(x + K(2),  y + K(3),  K(3),  K(4), C_RED);                /* left  */
    roof(x + K(16), y + K(3),  K(3),  K(4), C_RED);                /* right */
    roof(x + K(9),  y - K(1),  K(4),  K(5), C_RED);                /* keep  */
    /* gate (arched doorway) */
    vid_fillrect(x + K(7),  y + K(13), K(4), K(7), C_DKGRAY);
    vid_fillrect(x + K(8),  y + K(12), K(2), K(1), C_DKGRAY);
    vid_fillrect(x + K(8),  y + K(15), K(2), K(3), C_BLACK);       /* depth */
    /* lit windows */
    vid_fillrect(x + K(1),  y + K(10), K(2), K(2), C_LTBLUE);
    vid_fillrect(x + K(15), y + K(10), K(2), K(2), C_LTBLUE);
    vid_fillrect(x + K(8),  y + K(7),  K(2), K(2), C_YELLOW);
    /* flagpole + pennant on the keep (waves with flag_up) */
    vid_fillrect(x + K(9),  y - K(6),  (u < 2 ? 1 : u / 2 + 1), K(6), C_DKGRAY);
    if (flag_up)
        vid_fillrect(x + K(10), y - K(6), K(4), K(2), C_RED);
    else
        vid_fillrect(x + K(10), y - K(5), K(4), K(2), C_BLUE);
#undef K
}

/* ---- The Start-button castle -----------------------------------------
 * A crisp, fully black-outlined pixel-art castle, drawn from a fixed 13x10
 * grid so it is razor-sharp and unmistakable even at taskbar size: a tall
 * central keep with battlements and a red pennant, two flanking towers
 * under pointed red roofs, an arched gate and three lit windows.  Every
 * cell is s x s device pixels (s = 1 in Mode 13h, larger for a big logo).
 *
 * Grid legend: '#'=black outline  'S'=stone  'H'=sunlit stone  'R'=red
 *              'W'=lit window     'G'=gateway  '.'=transparent
 */
void ui_start_castle(int x, int y, int s)
{
    static const char grid[10][14] = {
        "......#RRR#..",
        ".....##RR#...",
        "..#.#S#S#.#..",
        ".#R##HHH##R#.",
        "#RRR#HWS#RRR#",
        "#HHH#HWS#HHH#",
        "#HWS#HGS#HWS#",
        "#HWSSGGGSHWS#",
        "#HSSSG#GSHSS#",
        "#HSSSG#GSHSS#"
    };
    int r, c;
    for (r = 0; r < 10; ++r) {
        for (c = 0; c < 13; ++c) {
            u8 col = C_BLACK;
            switch (grid[r][c]) {
            case '#': col = C_BLACK;    break;
            case 'S': col = C_DKYELLOW; break;
            case 'H': col = C_YELLOW;   break;
            case 'R': col = C_RED;      break;
            case 'W': col = C_LTBLUE;   break;
            case 'G': col = C_DKGRAY;   break;
            default:  continue;         /* '.' - transparent                 */
            }
            if (s <= 1)
                vid_pixel(x + c, y + r, col);
            else
                vid_fillrect(x + c * s, y + r * s, s, s, col);
        }
    }
}

/* Lower-case substring test used to choose an icon from a command. */
/* The needle test used to lower-case the WHOLE command into a fresh stack
   buffer on every call - and ui_icon_for_command asks up to ~60 times for
   one icon, which the desktop, the Program Drawer and every group window
   then repeat for each cell on every compose.  Fold the command once into
   g_lc and let each test be a bare strstr. */
static char g_lc[80];

static void lc_set(const char *hay)
{
    int i = 0;
    while (hay[i] != '\0' && i < (int)sizeof(g_lc) - 1) {
        char c = hay[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        g_lc[i] = c;
        ++i;
    }
    g_lc[i] = '\0';
}

static bool_t has(const char *needle)
{
    return (strstr(g_lc, needle) != NULL) ? TRUE : FALSE;
}

/* Resolve the already-folded g_lc.  Split from the public entry point so
   the memo below can wrap it. */
static int icon_for_folded(void)
{
    if (has("fileman") || has("computer") ||
        has("mycomp"))                              return ICON_COMPUTER;
    if (has("cabinet") || has("folder"))   return ICON_FOLDER;
    if (has("find")    || has("search"))   return ICON_FIND;
    if (has("oracle")  || has("probe") ||
        has("aida"))                                return ICON_ORACLE;
    if (has("inspect") || has("sysinfo") ||
        has("system")  || has("panel"))    return ICON_GAUGE;
    if (has("settings")|| has("control"))  return ICON_SETTINGS;
    if (has("bench"))                               return ICON_BENCH;
    if (has("stopwatch")|| has("timer"))   return ICON_WATCH;
    if (has("about")   || has("info") ||
        has("help"))                                return ICON_INFO;
    if (has("drawer")  || has("programs")) return ICON_DRAWER;
    if (has("tool"))                                return ICON_TOOLS;
    if (has("arcade")  || has("games"))    return ICON_ARCADE;
    /* "lights[out]" before the Light-Show "light" catch - and NOT the
       bare substring, because "lightshow" contains "lights" too, so the
       Light Show was being drawn with the Lights Out lamp grid.  The
       ordering comment here was right about the danger and wrong about
       which spelling it caught. */
    if (has("lightsout") || (has("lights") && !has("lightshow")))
        return ICON_LIGHTS;
    if (has("demo")    || has("light") ||
        has("effects"))                             return ICON_DEMO;
    if (has("command") || has("room") ||
        has("prompt")  || has(".bat") ||
        has("run"))                                 return ICON_TERMINAL;
    if (has("picture") || has("gallery") ||
        has("picshow")  || has("slide"))   return ICON_PICTURE;
    if (has("cinema")  || has("movie") ||
        has("flic")    || has(".fli") ||
        has(".flc"))                                return ICON_CINEMA;
    if (has("gram")    || has("media") ||
        has("play")    || has("wav") ||
        has("mid"))                                 return ICON_MEDIA;
    if (has("agenda")  || has("todo"))     return ICON_AGENDA;
    if (has("peek")    || has("hexview"))  return ICON_PEEK;
    if (has("cardfile")|| has("card"))     return ICON_CARDFILE;
    if (has("scrap")   || has("note"))     return ICON_NOTE;
    if (has("color")   || has("palette"))  return ICON_COLORS;
    if (has("fractal") || has("mandel"))   return ICON_FRACTAL;
    if (has("calc"))                                return ICON_CALC;
    if (has("clock"))                               return ICON_CLOCK;
    if (has("paint")   || has("sketch"))   return ICON_PAINT;
    if (has("char"))                                return ICON_CHARS;
    if (has("music")   || has("juke") ||
        has("tunes"))                               return ICON_MUSIC;
    /* strcmp for the short alias, not has(): "cal" as a SUBSTRING matches
       any command with "local" in its path, and these tests run against
       the whole command line including one. */
    if (has("calendar") || strcmp(g_lc, "cal") == 0)
        return ICON_CALENDAR;
    if (has("pong"))                                return ICON_PONG;
    if (has("2048")    || has("merge"))    return ICON_2048;
    if (has("corral")  || has("jezz"))     return ICON_CORRAL;
    if (has("typist")  || has("typing"))   return ICON_TYPIST;
    if (has("puzzle")  || has("fifteen"))  return ICON_FIFTEEN;
    if (has("ttt")     || has("tic"))      return ICON_TTT;
    if (has("mine"))                                return ICON_MINE;
    if (has("reversi") || has("othello"))  return ICON_REVERSI;
    if (has("snake")   || has("serpent"))  return ICON_SNAKE;
    if (has("breaker") || has("blocks") ||
        has("bricks"))                              return ICON_BREAKER;
    if (has("echo")    || has("memory"))   return ICON_ECHO;
    if (has("quadrix") || has("tetra"))    return ICON_QUADRIX;
    if (has("depot")   || has("sokoban"))  return ICON_DEPOT;
    if (has("patience")|| has("solitaire")) return ICON_PATIENCE;
    if (has("eyes"))                                return ICON_EYES;
    if (has("exit")    || has("quit"))     return ICON_EXIT;
    if (has("disk")    || has("drive"))    return ICON_DISK;
    return ICON_FILE;
}

/* A repeat ask costs a hash and one strcmp instead of ~60 strstr probes.
   The desktop hides behind its scene cache, but the Program Drawer and the
   14-cell Arcade group re-resolve every cell on every compose - some 800
   folds and scans per repaint.  The mapping is a pure function of the
   command text, so the memo never needs invalidating. */
#define ICM_N   16
#define ICM_KEY 16
static char g_icm_key[ICM_N][ICM_KEY];
static int  g_icm_id[ICM_N];

int ui_icon_for_command(const char *command)
{
    unsigned h = 0;
    int slot, n, id;
    if (command == NULL || command[0] == '\0')
        return ICON_FILE;
    lc_set(command);
    for (n = 0; g_lc[n] != '\0'; ++n)
        h = h * 31U + (unsigned char)g_lc[n];
    slot = (int)(h & (ICM_N - 1));
    if (n < ICM_KEY && g_icm_key[slot][0] != '\0' &&
        strcmp(g_icm_key[slot], g_lc) == 0)
        return g_icm_id[slot];
    id = icon_for_folded();
    if (n < ICM_KEY) {                 /* longer verbs just miss every time */
        strcpy(g_icm_key[slot], g_lc);
        g_icm_id[slot] = id;
    }
    return id;
}

