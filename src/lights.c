/* ======================================================================
 * lights.c - Lights Out (a lamp-toggling puzzle) for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include "lights.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "music.h"

#define LW 5
#define LCELLS (LW * LW)

static u8  far l_on[LCELLS];
static int l_moves, l_won, l_puzzle = 1;

static unsigned long l_seed = 0x11A87501UL;
static unsigned l_rnd(void)
{
    l_seed = l_seed * 1103515245UL + 12345UL;
    return (unsigned)(l_seed >> 16);
}

static void l_press(int x, int y)
{
    l_on[y * LW + x] ^= 1;
    if (x > 0)      l_on[y * LW + x - 1] ^= 1;
    if (x < LW - 1) l_on[y * LW + x + 1] ^= 1;
    if (y > 0)      l_on[(y - 1) * LW + x] ^= 1;
    if (y < LW - 1) l_on[(y + 1) * LW + x] ^= 1;
}

static void l_new(void)
{
    int i, presses = 4 + l_puzzle * 2;
    if (presses > 14) presses = 14;
    for (i = 0; i < LCELLS; ++i)
        l_on[i] = 0;
    for (i = 0; i < presses; ++i)      /* built from a solved board, so   */
        l_press((int)(l_rnd() % LW),   /* it is solvable by construction  */
                (int)(l_rnd() % LW));
    l_moves = 0;
    l_won   = 0;
}

void lights_open(void)
{
    l_seed ^= sys_ticks() | 1UL;
    l_puzzle = 1;
    l_new();
}

static int l_all_out(void)
{
    int i;
    for (i = 0; i < LCELLS; ++i)
        if (l_on[i])
            return 0;
    return 1;
}

static void l_geom(const Rect *cl, int *cell, int *ox, int *oy)
{
    int cw = (cl->w - 8) / LW;
    int ch = (cl->h - font_h() * 2 - 8) / LW;
    int c  = (cw < ch) ? cw : ch;
    if (c < 8) c = 8;
    *cell = c;
    *ox = cl->x + (cl->w - c * LW) / 2;
    *oy = cl->y + 3;
}

void lights_draw(const Rect *cl)
{
    int cell, ox, oy, x, y;
    char buf[34];
    l_geom(cl, &cell, &ox, &oy);

    for (y = 0; y < LW; ++y)
        for (x = 0; x < LW; ++x) {
            int px = ox + x * cell, py = oy + y * cell;
            if (l_on[y * LW + x]) {
                vid_fillrect(px + 1, py + 1, cell - 2, cell - 2, C_YELLOW);
                ui_raise(px + 1, py + 1, cell - 2, cell - 2);
                vid_fillrect(px + cell / 4, py + cell / 4,
                             cell / 4, cell / 4, C_CREAM);   /* a gleam   */
            } else {
                vid_fillrect(px + 1, py + 1, cell - 2, cell - 2, C_SHADOW);
                ui_sink(px + 1, py + 1, cell - 2, cell - 2);
            }
        }

    if (l_won)
        sprintf(buf, "Lights out in %d!  N: next", l_moves);
    else
        sprintf(buf, "Board %d  Moves %d", l_puzzle, l_moves);
    ui_text_center(cl->x, oy + LW * cell + 3, cl->w, buf,
                   l_won ? C_GREEN : C_BLACK);
    ui_text_center(cl->x, oy + LW * cell + 4 + font_h(), cl->w,
                   "click a lamp: it + neighbours flip", C_DKGRAY);
}

void lights_click(const Rect *cl, int mx, int my)
{
    int cell, ox, oy, x, y;
    if (l_won) {
        ++l_puzzle;
        l_new();
        return;
    }
    l_geom(cl, &cell, &ox, &oy);
    x = (mx - ox) / cell;
    y = (my - oy) / cell;
    if (mx < ox || my < oy || x >= LW || y >= LW)
        return;
    l_press(x, y);
    ++l_moves;
    music_sfx((unsigned)(500 + (x + y) * 40), 1);
    if (l_all_out()) {
        l_won = 1;
        music_sfx(990, 3);
    }
}

bool_t lights_key(int key)
{
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        lights_open();
        return TRUE;
    }
    if (key == 'n' || key == 'N') {
        if (l_won)
            ++l_puzzle;
        l_new();
        return TRUE;
    }
    return FALSE;
}
