/* ======================================================================
 * charmap.c - Character Map utility for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include "charmap.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"

#define FIRST 0x20
#define LAST  0x7F
#define COLS  16
#define ROWS  6                       /* 16 x 6 = 96 chars                 */

static int g_sel = 'A';

void charmap_open(void)
{
    g_sel = 'A';
}

/* Cell size and grid origin. */
static void geom(const Rect *cl, int *cw, int *ch, int *gx, int *gy)
{
    int w = (cl->w - 8) / COLS;
    int h = font_h() + 4;
    if (w < font_adv() + 2) w = font_adv() + 2;
    *cw = w; *ch = h;
    *gx = cl->x + (cl->w - w * COLS) / 2;
    *gy = cl->y + 5;
}

void charmap_draw(const Rect *cl)
{
    int cw, ch, gx, gy, i;
    char buf[32];
    geom(cl, &cw, &ch, &gx, &gy);

    for (i = 0; i <= LAST - FIRST; ++i) {
        int c  = FIRST + i;
        int x  = gx + (i % COLS) * cw;
        int y  = gy + (i / COLS) * ch;
        char s[2];
        s[0] = (char)c; s[1] = '\0';
        if (c == g_sel) {
            vid_fillrect(x, y, cw, ch, C_TITLE);
            font_draw(x + (cw - font_adv()) / 2, y + 2, s, C_WHITE);
        } else {
            font_draw(x + (cw - font_adv()) / 2, y + 2, s, C_BLACK);
        }
    }

    sprintf(buf, "'%c'   code %d  (0x%02X)", (char)g_sel, g_sel, g_sel);
    ui_text_center(cl->x, gy + ROWS * ch + 5, cl->w, buf, C_BLACK);
}

/* The Character Map had no key handler at all - its row in window.c's
   applet table carried a 0 - so without a mouse driver it opened with
   'A' selected and stayed there.  Nothing about it needs a pointer.

   Typing a character SELECTS it, which is the most direct thing a
   keyboard can do here: "show me what this glyph looks like" without
   hunting a 16x6 grid for it.  The arrows walk the grid for the ones
   you cannot type. */
bool_t charmap_key(int key)
{
    int i = g_sel - FIRST;

    if (key >= 32 && key < 127) { g_sel = key; return TRUE; }

    switch (key) {
    case KEY_LEFT:  if (i > 0)                 --g_sel;      return TRUE;
    case KEY_RIGHT: if (i < LAST - FIRST)      ++g_sel;      return TRUE;
    case KEY_UP:    if (i >= COLS)             g_sel -= COLS; return TRUE;
    case KEY_DOWN:  if (i + COLS <= LAST - FIRST) g_sel += COLS; return TRUE;
    case KEY_HOME:  g_sel = FIRST;             return TRUE;
    case KEY_END:   g_sel = LAST;              return TRUE;
    default:        break;
    }
    return FALSE;
}

void charmap_click(const Rect *cl, int mx, int my)
{
    int cw, ch, gx, gy, c, r, i;
    geom(cl, &cw, &ch, &gx, &gy);
    c = (mx - gx) / cw;
    r = (my - gy) / ch;
    if (mx < gx || my < gy || c < 0 || c >= COLS || r < 0 || r >= ROWS)
        return;
    i = r * COLS + c;
    if (i >= 0 && i <= LAST - FIRST)
        g_sel = FIRST + i;
}
