/* ======================================================================
 * paint.c - Sketch Pad (pixel / icon editor) applet for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "paint.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "dialog.h"
#include "icon.h"
#include "keyboard.h"
#include "filedlg.h"
#include "window.h"   /* wm_set_title: the drawing name goes in the bar */

#define CANVAS  32
#define PAINT_TRANS 0xFF

/* far: 1 KB off DGROUP; every use is a plain subscript. */
static u8 far g_canvas[CANVAS * CANVAS];
static u8   g_color = C_BLACK;     /* current colour, or PAINT_TRANS       */
static bool_t g_active = FALSE;    /* a canvas stroke is in progress       */
static char g_file[132] = "";      /* full doc_path: cwd (79) + name    */

/* Unsaved-canvas guard: New, Load and closing the window used to wipe the
   drawing without a word. */
static bool_t g_dirty = FALSE;

static bool_t ok_to_discard(void)
{
    if (!g_dirty)
        return TRUE;
    return (dialog_confirm("Sketch Pad", "Discard the unsaved changes",
                           "to this drawing?") == DLG_YES) ? TRUE : FALSE;
}

void   paint_flush_state(void) { g_dirty = FALSE; }
bool_t paint_is_dirty(void)    { return g_dirty; }

/* Tools: freehand pen, flood fill, two-click straight line. */
#define TOOL_PEN  0
#define TOOL_FILL 1
#define TOOL_LINE 2
static int g_tool = TOOL_PEN;
static int g_ax = -1, g_ay = -1;   /* the line tool's anchor, or -1        */

/* The keyboard cursor.  The Sketch Pad was the ONLY applet in the shell
   with no key handler at all - its g_app row carried 0 - in a product
   whose README claims full keyboard operation, and F1 had to apologise
   for it.  -1 = the cursor has not been shown yet; the first arrow key
   parks it in the middle. */
static int g_kx = -1, g_ky = -1;

/* Geometry, recomputed each draw/click from the client rectangle. */
static Rect g_canv;                /* on-screen canvas rectangle           */
static int  g_zoom = 1;
static Rect g_sw[17];              /* 16 colour swatches + transparent     */
static Rect g_tb[6];               /* New/Load/Save + Pen/Fill/Line        */

static const char *TB_LBL[6] =
    { "New", "Load", "Save", "Pen", "Fill", "Line" };
static const char  HEX[16] = {
    '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
};

void paint_reset(void)
{
    int i;
    for (i = 0; i < CANVAS * CANVAS; ++i)
        g_canvas[i] = PAINT_TRANS;
    g_color  = C_BLACK;
    g_active = FALSE;
    g_tool   = TOOL_PEN;
    g_kx = g_ky = -1;
    g_ax = g_ay = -1;
    /* And the filename - same trap as the Scrap Box.  Save pre-fills the
       picker from g_file, so a blank canvas that still remembered the
       last drawing offered to write itself over it. */
    g_file[0] = '\0';
}

/* ---- layout ---------------------------------------------------------- */
static void layout(const Rect *c)
{
    int tbh = font_h() + 5;
    int palh = font_h() + 8;
    int bw, sw, i, ax, ay, aw, ah, side;

    /* Toolbar buttons across the top (files, then the three tools). */
    bw = font_adv() * 4 + 3;
    for (i = 0; i < 6; ++i)
        rect_set(&g_tb[i], c->x + i * (bw + 1), c->y, bw, tbh);

    /* Palette swatches along the bottom (16 colours + transparent). */
    sw = (c->w - 2) / 17;
    for (i = 0; i < 17; ++i)
        rect_set(&g_sw[i], c->x + 2 + i * sw, c->y + c->h - palh + 1,
                 sw - 1, palh - 2);

    /* Square canvas in the space between, centred. */
    ax = c->x + 2;
    ay = c->y + tbh + 2;
    aw = c->w - 4;
    ah = c->h - tbh - palh - 4;
    side = (aw < ah) ? aw : ah;
    g_zoom = side / CANVAS;
    if (g_zoom < 1) g_zoom = 1;
    side = g_zoom * CANVAS;
    rect_set(&g_canv, ax + (aw - side) / 2, ay, side, side);
}

/* ---- drawing --------------------------------------------------------- */
static void draw_cell(int cx, int cy)
{
    u8 v = g_canvas[cy * CANVAS + cx];
    int x = g_canv.x + cx * g_zoom;
    int y = g_canv.y + cy * g_zoom;
    if (v == PAINT_TRANS)
        /* C_FACE against C_WHITE, not C_HILIGHT: in the classic palette
           C_HILIGHT and C_WHITE are BOTH pure white, so the transparency
           checker was invisible and an empty canvas looked like a solid
           white one - you could not tell erased from painted-white. */
        vid_fillrect(x, y, g_zoom, g_zoom,
                     ((cx + cy) & 1) ? C_FACE : C_WHITE);      /* checker   */
    else
        vid_fillrect(x, y, g_zoom, g_zoom, v);
}

void paint_draw(const Rect *client)
{
    int i, cx, cy;

    layout(client);

    for (i = 0; i < 6; ++i)            /* the active tool shows pressed in  */
        ui_button(&g_tb[i], TB_LBL[i],
                  (i == 3 + g_tool) ? TRUE : FALSE);

    /* Current-colour indicator next to the toolbar. */
    {
        int ix = g_tb[5].x + g_tb[5].w + 4;
        int iy = client->y;
        vid_fillrect(ix, iy, font_h() + 5, font_h() + 5,
                     (g_color == PAINT_TRANS) ? C_WHITE : g_color);
        ui_sink(ix, iy, font_h() + 5, font_h() + 5);
    }

    /* Canvas. */
    for (cy = 0; cy < CANVAS; ++cy)
        for (cx = 0; cx < CANVAS; ++cx)
            draw_cell(cx, cy);
    if (g_zoom >= 5) {                 /* grid lines for big zoom          */
        for (i = 0; i <= CANVAS; ++i) {
            vid_vline(g_canv.x + i * g_zoom, g_canv.y, g_canv.h, C_DKGRAY);
            vid_hline(g_canv.x, g_canv.y + i * g_zoom, g_canv.w, C_DKGRAY);
        }
    }
    /* The keyboard cursor: a white box inside a black one, so it reads on
       every one of the sixteen colours and on the transparency checker. */
    if (g_kx >= 0) {
        int kx = g_canv.x + g_kx * g_zoom;
        int ky = g_canv.y + g_ky * g_zoom;
        int kz = (g_zoom < 3) ? 3 : g_zoom;
        vid_rect(kx - 1, ky - 1, kz + 2, kz + 2, C_BLACK);
        vid_rect(kx,     ky,     kz,     kz,     C_WHITE);
    }
    /* The line tool's pending anchor, so a half-drawn line is visible. */
    if (g_ax >= 0) {
        int axp = g_canv.x + g_ax * g_zoom;
        int ayp = g_canv.y + g_ay * g_zoom;
        vid_rect(axp, ayp, (g_zoom < 3) ? 3 : g_zoom,
                 (g_zoom < 3) ? 3 : g_zoom, C_RED);
    }
    ui_sink(g_canv.x - 1, g_canv.y - 1, g_canv.w + 2, g_canv.h + 2);

    /* Palette. */
    for (i = 0; i < 17; ++i) {
        if (i < 16)
            vid_fillrect(g_sw[i].x, g_sw[i].y, g_sw[i].w, g_sw[i].h, (u8)i);
        else {                          /* the transparent / eraser swatch  */
            vid_fillrect(g_sw[i].x, g_sw[i].y, g_sw[i].w, g_sw[i].h, C_WHITE);
            font_draw_char(g_sw[i].x + (g_sw[i].w - 5) / 2,
                           g_sw[i].y + (g_sw[i].h - font_h()) / 2,
                           'T', C_RED);
        }
        ui_sink(g_sw[i].x, g_sw[i].y, g_sw[i].w, g_sw[i].h);
        /* Every chip gets an outline: slot 3 IS C_FACE, so against the
           toolbar behind it that chip was invisible - it read as a gap in
           the palette rather than a colour you could pick. */
        vid_rect(g_sw[i].x, g_sw[i].y, g_sw[i].w, g_sw[i].h, C_DKGRAY);
        /* Mark the selection with a black ring INSIDE a white one.  A
           plain black rectangle was invisible on the black chip - the one
           colour a sketch pad is most likely to be left on. */
        if ((i < 16 && g_color == (u8)i) || (i == 16 && g_color == PAINT_TRANS)) {
            vid_rect(g_sw[i].x - 2, g_sw[i].y - 2, g_sw[i].w + 4,
                     g_sw[i].h + 4, C_WHITE);
            vid_rect(g_sw[i].x - 1, g_sw[i].y - 1, g_sw[i].w + 2,
                     g_sw[i].h + 2, C_BLACK);
        }
    }
}

static void flood_fill(int cx, int cy);
static void draw_line_cells(int x0, int y0, int x1, int y1);

/* Apply the current tool at the keyboard cursor - the same three
   behaviours paint_click gives the mouse, so the two can never drift. */
static bool_t apply_at(int cx, int cy)
{
    if (g_tool == TOOL_FILL) {
        flood_fill(cx, cy);
        return TRUE;
    }
    if (g_tool == TOOL_LINE) {
        if (g_ax < 0) {
            g_ax = cx; g_ay = cy;
            g_canvas[cy * CANVAS + cx] = g_color;
        } else {
            draw_line_cells(g_ax, g_ay, cx, cy);
            g_ax = g_ay = -1;
        }
        g_dirty = TRUE;
        return TRUE;
    }
    g_canvas[cy * CANVAS + cx] = g_color;      /* the pen */
    g_dirty = TRUE;
    return TRUE;
}

/* Arrows move a cursor, Space/Enter applies the tool, Tab cycles the
   tool, [ and ] walk the palette, X takes the eraser, Del clears the
   cell.  Shift is not available to us (the BIOS folds it into the scan
   code we already use), so drawing a run is Space-arrow-Space rather
   than a held modifier - which is how DOS pixel editors did it anyway. */
/* Defined below, beside the file helpers they need - the same shape
   card.c uses for card_flush(). */
static void act_new(void);
/* "Sketch Pad - NAME.ICN", or plain "Sketch Pad" for an unsaved canvas.
   The BASE name only: a full path does not fit a 200-pixel title bar,
   and the last component is the part that identifies the file - the
   same choice dialog.c makes when it elides a path from the front. */
static void retitle(void)
{
    char t[40];
    int  i, k = 0, base = 0;
    const char *pre = "Sketch Pad";
    while (pre[k] != '\0') { t[k] = pre[k]; ++k; }
    for (i = 0; g_file[i] != '\0'; ++i)
        if (g_file[i] == '\\' || g_file[i] == '/' || g_file[i] == ':')
            base = i + 1;
    if (g_file[base] != '\0') {
        t[k++] = ' '; t[k++] = '-'; t[k++] = ' ';
        for (i = base; g_file[i] != '\0' && k < (int)sizeof(t) - 1; ++i)
            t[k++] = g_file[i];
    }
    t[k] = '\0';
    wm_set_title(WIN_PAINT, t);
}

static void act_load(void);
static void act_save(void);

bool_t paint_key(int key)
{
    if (g_kx < 0 && (key == KEY_LEFT || key == KEY_RIGHT ||
                     key == KEY_UP   || key == KEY_DOWN  ||
                     key == KEY_SPACE || key == KEY_ENTER)) {
        g_kx = g_ky = CANVAS / 2;      /* first key parks it in the middle */
        return TRUE;
    }
    switch (key) {
    case KEY_LEFT:  if (g_kx > 0)          { --g_kx; return TRUE; } return FALSE;
    case KEY_RIGHT: if (g_kx < CANVAS - 1) { ++g_kx; return TRUE; } return FALSE;
    case KEY_UP:    if (g_ky > 0)          { --g_ky; return TRUE; } return FALSE;
    case KEY_DOWN:  if (g_ky < CANVAS - 1) { ++g_ky; return TRUE; } return FALSE;
    case KEY_HOME:  g_kx = g_ky = 0;                    return TRUE;
    case KEY_END:   g_kx = g_ky = CANVAS - 1;           return TRUE;
    case KEY_SPACE:
    case KEY_ENTER: return apply_at(g_kx, g_ky);
    case KEY_TAB:
        g_tool = (g_tool + 1) % 3;
        g_ax = g_ay = -1;              /* a tool change drops the anchor   */
        return TRUE;
    case KEY_DEL:
        g_canvas[g_ky * CANVAS + g_kx] = PAINT_TRANS;
        g_dirty = TRUE;
        return TRUE;
    case KEY_ESC:
        if (g_ax >= 0) { g_ax = g_ay = -1; return TRUE; }  /* drop anchor  */
        return FALSE;
    default:
        break;
    }
    if (key == '[') {                  /* previous colour                  */
        g_color = (g_color == PAINT_TRANS) ? 15
                : (u8)((g_color + 15) & 15);
        return TRUE;
    }
    if (key == ']') {                  /* next colour                      */
        g_color = (g_color == PAINT_TRANS) ? 0
                : (u8)((g_color + 1) & 15);
        return TRUE;
    }
    if (key == 'x' || key == 'X') {    /* the eraser                       */
        g_color = PAINT_TRANS;
        return TRUE;
    }
    if (key >= '0' && key <= '9') {    /* colours 0..9 straight off a digit */
        g_color = (u8)(key - '0');
        return TRUE;
    }
    /* The toolbar.  Same three keys as the Scrap Box, because they are
       the same three buttons and the two editors should not disagree. */
    if (key == KEY_F7) { act_new();  return TRUE; }
    if (key == KEY_F3) { act_load(); return TRUE; }
    if (key == KEY_F2) { act_save(); return TRUE; }
    return FALSE;
}

/* ---- files ----------------------------------------------------------- */
/* TRUE only when the bytes really reached the disk - see the same note in
   scrap.c.  A write-protected floppy used to show an error AND mark the
   drawing saved, so closing the window never offered to keep it. */
static bool_t paint_save(const char *path)
{
    FILE *f;
    int x, y;
    f = fopen(path, "w");
    if (f == NULL) {
        dialog_message("Save", "Could not write file.", path);
        return FALSE;
    }
    fprintf(f, "; CASTALIA/386 icon\nCICN 32 32\n");
    for (y = 0; y < CANVAS; ++y) {
        char line[CANVAS + 1];
        for (x = 0; x < CANVAS; ++x) {
            u8 v = g_canvas[y * CANVAS + x];
            line[x] = (v == PAINT_TRANS) ? '.' : HEX[v & 15];
        }
        line[CANVAS] = '\0';
        fprintf(f, "%s\n", line);
    }
    /* As in scrap.c: fopen("w") truncated the file before the first row
       went out, so a half-written .ICN is a file that will not load, and
       "the disk is full" did not say that had happened.  The canvas is
       still in memory and still dirty. */
    if (ferror(f)) {
        fclose(f);
        dialog_message("Save", "Disk full - the file on disk",
                       "is cut short.  Save elsewhere.");
        return FALSE;
    }
    if (fclose(f) != 0) {
        dialog_message("Save", "The file on disk is cut short.",
                       "Save it somewhere else.");
        return FALSE;
    }
    return TRUE;
}

static void paint_load(const char *path)
{
    IconBitmap ic;
    int i;
    if (!icon_load(path, &ic)) {
        dialog_message("Load", "Not a 32x32 .ICN file.", path);
        return;
    }
    for (i = 0; i < CANVAS * CANVAS; ++i)
        g_canvas[i] = ic.px[i];
}

/* Open a .ICN straight from a path (the Disk Cabinet's association):
   remember it as the working file and load it onto the easel. */
bool_t paint_ok_to_replace(void) { return ok_to_discard(); }

void paint_open_file(const char *path)
{
    int i = 0;
    while (path[i] != '\0' && i < (int)sizeof(g_file) - 1) {
        g_file[i] = path[i];
        ++i;
    }
    g_file[i] = '\0';
    paint_load(g_file);
    g_dirty = FALSE;                   /* a freshly loaded file is clean  */
}

/* ---- interaction ----------------------------------------------------- */
static Rect g_last_cell;              /* screen rect of the cell just painted */

static bool_t paint_at(int mx, int my)
{
    int cx, cy;
    if (mx < g_canv.x || mx >= g_canv.x + g_canv.w ||
        my < g_canv.y || my >= g_canv.y + g_canv.h)
        return FALSE;
    cx = (mx - g_canv.x) / g_zoom;
    cy = (my - g_canv.y) / g_zoom;
    if (cx < 0 || cx >= CANVAS || cy < 0 || cy >= CANVAS)
        return FALSE;
    g_canvas[cy * CANVAS + cx] = g_color;
    g_dirty = TRUE;
    draw_cell(cx, cy);                 /* paint straight into the back buffer */
    rect_set(&g_last_cell, g_canv.x + cx * g_zoom, g_canv.y + cy * g_zoom,
             g_zoom, g_zoom);
    return TRUE;
}

/* Screen rectangle of the last cell paint_at() touched (for a tiny blit). */
void paint_drag_rect(Rect *r) { *r = g_last_cell; }

/* Flood fill: replace the clicked cell's colour region with g_color.  A
   sweep-until-stable propagation over the 1 KB canvas - no recursion and
   no stack, so it cannot overflow anything, and a 386 chews the worst
   case (a few thousand cell visits) without blinking. */
#define FILL_MARK 0xFE
static void flood_fill(int cx, int cy)
{
    u8  target = g_canvas[cy * CANVAS + cx];
    int changed, x, y, i;
    if (target == g_color)
        return;
    g_dirty = TRUE;             /* a drawing made only with Fill is still work */
    g_canvas[cy * CANVAS + cx] = FILL_MARK;
    do {
        changed = 0;
        for (y = 0; y < CANVAS; ++y)
            for (x = 0; x < CANVAS; ++x) {
                if (g_canvas[y * CANVAS + x] != target)
                    continue;
                if ((x > 0          && g_canvas[y*CANVAS + x-1] == FILL_MARK) ||
                    (x < CANVAS - 1 && g_canvas[y*CANVAS + x+1] == FILL_MARK) ||
                    (y > 0          && g_canvas[(y-1)*CANVAS + x] == FILL_MARK) ||
                    (y < CANVAS - 1 && g_canvas[(y+1)*CANVAS + x] == FILL_MARK)) {
                    g_canvas[y * CANVAS + x] = FILL_MARK;
                    changed = 1;
                }
            }
    } while (changed);
    for (i = 0; i < CANVAS * CANVAS; ++i)
        if (g_canvas[i] == FILL_MARK)
            g_canvas[i] = g_color;
}

/* Bresenham between two canvas cells in the current colour. */
static void draw_line_cells(int x0, int y0, int x1, int y1)
{
    int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
    int dy = (y1 > y0) ? y1 - y0 : y0 - y1;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    g_dirty = TRUE;             /* ...and so is one made only with Line       */
    for (;;) {
        g_canvas[y0 * CANVAS + x0] = g_color;
        if (x0 == x1 && y0 == y1)
            break;
        {
            int e2 = err * 2;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }
}

/* New / Load / Save, factored out of the toolbar handler so the keyboard
   can reach them.  Tab already cycled Pen/Fill/Line and the arrows drew,
   so everything about the Sketch Pad worked without a mouse EXCEPT
   keeping the drawing. */
static void act_new(void)
{
    if (ok_to_discard()) {
        paint_reset();                 /* which clears g_file */
        g_dirty = FALSE;
        retitle();                     /* ...so back to plain "Sketch Pad" */
    }
}

static void act_load(void)
{
    if (ok_to_discard() &&
        filedlg("Open a drawing", "*.IC?", g_file,
                (int)sizeof(g_file), FALSE)) {
        paint_load(g_file);
        g_dirty = FALSE;               /* a freshly loaded file is clean   */
        retitle();
    }
}

static void act_save(void)
{
    if (filedlg("Save the drawing", "*.IC?", g_file,
                (int)sizeof(g_file), TRUE) &&
        paint_save(g_file)) {
        g_dirty = FALSE;               /* only when the write succeeded    */
        retitle();
    }
}

bool_t paint_click(const Rect *client, int mx, int my)
{
    int i;
    layout(client);
    g_active = FALSE;

    for (i = 0; i < 6; ++i) {
        if (rect_contains(&g_tb[i], mx, my)) {
            if (i == 0) {
                act_new();
            } else if (i == 1) {
                act_load();
            } else if (i == 2) {
                act_save();
            } else {
                g_tool = i - 3;        /* Pen / Fill / Line                 */
                g_ax = g_ay = -1;      /* a tool change drops the anchor    */
            }
            return TRUE;
        }
    }
    for (i = 0; i < 17; ++i) {
        if (rect_contains(&g_sw[i], mx, my)) {
            g_color = (i < 16) ? (u8)i : PAINT_TRANS;
            return TRUE;
        }
    }

    /* A canvas hit behaves per tool. */
    if (mx >= g_canv.x && mx < g_canv.x + g_canv.w &&
        my >= g_canv.y && my < g_canv.y + g_canv.h) {
        int cx = (mx - g_canv.x) / g_zoom;
        int cy = (my - g_canv.y) / g_zoom;
        if (cx >= CANVAS) cx = CANVAS - 1;
        if (cy >= CANVAS) cy = CANVAS - 1;
        if (g_tool == TOOL_FILL) {
            flood_fill(cx, cy);        /* many cells change: full repaint    */
            return TRUE;
        }
        if (g_tool == TOOL_LINE) {
            if (g_ax < 0) {            /* first click: set (and show) anchor */
                g_ax = cx; g_ay = cy;
                g_dirty = TRUE;        /* the anchor paints a pixel too   */
                g_canvas[cy * CANVAS + cx] = g_color;
            } else {                   /* second click: rule the line        */
                draw_line_cells(g_ax, g_ay, cx, cy);
                g_ax = g_ay = -1;
            }
            return TRUE;
        }
        if (paint_at(mx, my)) {        /* the pen, as always                 */
            g_active = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

bool_t paint_drag(const Rect *client, int mx, int my)
{
    if (!g_active)
        return FALSE;
    layout(client);
    return paint_at(mx, my);
}
