/* ======================================================================
 * quadrix.c - Quadrix (a falling-blocks game) for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include "quadrix.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "music.h"

#define QW 10                  /* well width in cells                     */
#define QH 18                  /* well height in cells                    */

/* The seven pieces, four rotations each, as 4x4 bitmasks (bit 15 is the
   top-left cell, row-major).  Nintendo-style rotations, no wall kicks -
   period-correct and honest. */
static const u16 far QSHAPE[7][4] = {
    { 0x0F00, 0x2222, 0x0F00, 0x2222 },   /* I */
    { 0x6600, 0x6600, 0x6600, 0x6600 },   /* O */
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },   /* T */
    { 0x6C00, 0x4620, 0x6C00, 0x4620 },   /* S */
    { 0xC600, 0x2640, 0xC600, 0x2640 },   /* Z */
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },   /* J */
    { 0x2E00, 0x4460, 0x0E80, 0xC440 }    /* L */
};
static const u8 QCOL[7] = {
    C_CYAN, C_YELLOW, C_LTBLUE, C_GREEN, C_RED, C_BLUE, C_DKYELLOW
};
#define QCELL(m, cx, cy) ((m) & (u16)(0x8000U >> ((cy) * 4 + (cx))))

/* The well: 0 = empty, else colour index + 1.  Far - DGROUP is full. */
static u8 far q_well[QW * QH];

static int  q_piece, q_rot, q_x, q_y;      /* the falling piece           */
static int  q_next;                        /* the piece after it          */
static int  q_over;
static long q_score;
static int  q_lines, q_level;
static unsigned long q_last;               /* last gravity tick           */

static unsigned long q_seed = 0x0D06F00DUL;
static int q_rand7(void)
{
    q_seed = q_seed * 1103515245UL + 12345UL;
    return (int)((q_seed >> 16) % 7U);
}

/* TRUE if the piece p/r at (x,y) overlaps a wall, the floor or a block. */
static int q_hits(int p, int r, int x, int y)
{
    int cx, cy;
    u16 m = QSHAPE[p][r];
    for (cy = 0; cy < 4; ++cy)
        for (cx = 0; cx < 4; ++cx) {
            int bx, by;
            if (!QCELL(m, cx, cy))
                continue;
            bx = x + cx; by = y + cy;
            if (bx < 0 || bx >= QW || by >= QH)
                return 1;
            if (by >= 0 && q_well[by * QW + bx])
                return 1;
        }
    return 0;
}

static void q_spawn(void)
{
    q_piece = q_next;
    q_next  = q_rand7();
    q_rot   = 0;
    q_x     = 3;
    q_y     = 0;
    if (q_hits(q_piece, q_rot, q_x, q_y)) {
        q_over = 1;
        music_sfx(140, 5);
    }
}

void quadrix_open(void)
{
    int i;
    for (i = 0; i < QW * QH; ++i)
        q_well[i] = 0;
    q_score = 0; q_lines = 0; q_level = 0; q_over = 0;
    q_seed ^= sys_ticks() | 1UL;
    q_next  = q_rand7();
    q_spawn();
    q_last = sys_ticks();
}

/* Merge the piece into the well, dissolve full rows, keep score. */
static void q_lock(void)
{
    int cx, cy, row, cleared = 0;
    u16 m = QSHAPE[q_piece][q_rot];
    for (cy = 0; cy < 4; ++cy)
        for (cx = 0; cx < 4; ++cx) {
            int bx = q_x + cx, by = q_y + cy;
            if (!QCELL(m, cx, cy))
                continue;
            if (by < 0) { q_over = 1; music_sfx(140, 5); return; }
            q_well[by * QW + bx] = (u8)(q_piece + 1);
        }
    for (row = QH - 1; row >= 0; --row) {
        int full = 1;
        for (cx = 0; cx < QW; ++cx)
            if (!q_well[row * QW + cx]) { full = 0; break; }
        if (full) {
            int r2;
            for (r2 = row; r2 > 0; --r2)
                for (cx = 0; cx < QW; ++cx)
                    q_well[r2 * QW + cx] = q_well[(r2 - 1) * QW + cx];
            for (cx = 0; cx < QW; ++cx)
                q_well[cx] = 0;
            ++cleared;
            ++row;                          /* re-test the dropped row     */
        }
    }
    if (cleared) {
        static const int pts[5] = { 0, 40, 100, 300, 1200 };
        int old_level = q_level;
        q_score += (long)pts[cleared] * (q_level + 1);
        q_lines += cleared;
        q_level  = q_lines / 8;
        if (q_level > 6) q_level = 6;
        music_sfx((unsigned)(700 + cleared * 150), 2);
        if (q_level != old_level)
            music_sfx(1320, 2);             /* level up!                   */
    } else {
        music_sfx(190, 1);                  /* soft thud                   */
    }
    q_spawn();
}

bool_t quadrix_tick(void)
{
    int spd = 7 - q_level;
    if (spd < 1) spd = 1;
    if (q_over)
        return FALSE;
    if (sys_ticks() - q_last < (unsigned long)spd)
        return FALSE;
    q_last = sys_ticks();
    if (!q_hits(q_piece, q_rot, q_x, q_y + 1))
        ++q_y;
    else
        q_lock();
    return TRUE;
}

bool_t quadrix_key(int key)
{
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        quadrix_open();
        return TRUE;
    }
    if (q_over) {
        if (key == KEY_ENTER || key == KEY_SPACE) { quadrix_open(); return TRUE; }
        return FALSE;
    }
    if (key == KEY_LEFT  && !q_hits(q_piece, q_rot, q_x - 1, q_y)) {
        --q_x; return TRUE;
    }
    if (key == KEY_RIGHT && !q_hits(q_piece, q_rot, q_x + 1, q_y)) {
        ++q_x; return TRUE;
    }
    if (key == KEY_UP) {                    /* rotate (if it fits)         */
        int nr = (q_rot + 1) & 3;
        if (!q_hits(q_piece, nr, q_x, q_y)) {
            q_rot = nr;
            music_sfx(520, 1);
            return TRUE;
        }
        return FALSE;
    }
    if (key == KEY_DOWN) {                  /* soft drop                   */
        if (!q_hits(q_piece, q_rot, q_x, q_y + 1)) { ++q_y; q_last = sys_ticks(); }
        else q_lock();
        return TRUE;
    }
    if (key == KEY_SPACE) {                 /* hard drop                   */
        while (!q_hits(q_piece, q_rot, q_x, q_y + 1))
            ++q_y;
        q_lock();
        return TRUE;
    }
    return FALSE;
}

void quadrix_click(const Rect *cl, int mx, int my)
{
    (void)cl; (void)mx; (void)my;
    if (q_over)
        quadrix_open();                     /* click to play again         */
    else
        quadrix_key(KEY_UP);                /* click rotates               */
}

/* ---- drawing --------------------------------------------------------- */

static void q_geom(const Rect *cl, int *cell, int *ox, int *oy)
{
    int ch = (cl->h - font_h() - 8) / QH;
    int cw = (cl->w - 56 * (font_h() / 8)) / QW;
    int c  = (ch < cw) ? ch : cw;
    if (c < 4) c = 4;
    *cell = c;
    *ox = cl->x + 4;
    *oy = cl->y + 3;
}

static void q_block(int x, int y, int cell, u8 col)
{
    vid_fillrect(x, y, cell - 1, cell - 1, col);
    vid_hline(x, y, cell - 1, C_WHITE);     /* a glint on top              */
    vid_vline(x, y, cell - 1, C_WHITE);
}

void quadrix_draw(const Rect *cl)
{
    int cell, ox, oy, x, y, px;
    char buf[32];                      /* "GAME OVER..." is 26 chars + NUL */
    q_geom(cl, &cell, &ox, &oy);

    /* The well. */
    vid_fillrect(ox, oy, QW * cell, QH * cell, C_BLACK);
    ui_sink(ox - 1, oy - 1, QW * cell + 2, QH * cell + 2);
    for (y = 0; y < QH; ++y)
        for (x = 0; x < QW; ++x) {
            u8 v = q_well[y * QW + x];
            if (v)
                q_block(ox + x * cell, oy + y * cell, cell, QCOL[v - 1]);
        }

    /* The falling piece. */
    if (!q_over) {
        u16 m = QSHAPE[q_piece][q_rot];
        int cx, cy;
        for (cy = 0; cy < 4; ++cy)
            for (cx = 0; cx < 4; ++cx)
                if (QCELL(m, cx, cy) && q_y + cy >= 0)
                    q_block(ox + (q_x + cx) * cell, oy + (q_y + cy) * cell,
                            cell, QCOL[q_piece]);
    }

    /* Side panel: the next piece and the numbers. */
    px = ox + QW * cell + 8;
    font_draw(px, oy, "Next", C_BLACK);
    {
        int pcell = (cell > 6) ? cell - 2 : cell;
        int bx = px, by = oy + font_h() + 3, cx, cy;
        u16 m = QSHAPE[q_next][0];
        vid_fillrect(bx, by, pcell * 4 + 2, pcell * 4 + 2, C_BLACK);
        ui_sink(bx - 1, by - 1, pcell * 4 + 4, pcell * 4 + 4);
        for (cy = 0; cy < 4; ++cy)
            for (cx = 0; cx < 4; ++cx)
                if (QCELL(m, cx, cy))
                    q_block(bx + 1 + cx * pcell, by + 1 + cy * pcell,
                            pcell, QCOL[q_next]);
        by += pcell * 4 + font_h();
        sprintf(buf, "Score");            font_draw(px, by, buf, C_BLACK);
        sprintf(buf, "%ld", q_score);     font_draw(px, by + font_h() + 1, buf, C_TITLE);
        by += 2 * font_h() + 6;
        sprintf(buf, "Lines %d", q_lines); font_draw(px, by, buf, C_BLACK);
        by += font_h() + 4;
        sprintf(buf, "Speed %d", q_level + 1); font_draw(px, by, buf, C_BLACK);
    }

    /* Status line. */
    if (q_over)
        sprintf(buf, "GAME OVER - Enter restarts");
    else
        sprintf(buf, "Up:spin  Space:drop");
    ui_text_center(cl->x, oy + QH * cell + 3, cl->w, buf,
                   q_over ? C_RED : C_DKGRAY);
}
