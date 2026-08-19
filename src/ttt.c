/* ======================================================================
 * ttt.c - Tic-Tac-Toe minigame for CASTALIA/386
 * ====================================================================== */
#include "ttt.h"
#include "video.h"
#include "ui.h"
#include "keyboard.h"
#include "font.h"
#include "music.h"

static int g_b[9];        /* 0 empty, 1 = X (you), 2 = O (machine)         */
/* The keyboard cursor: which square the arrows are resting on. */
static int g_cur = 4;                      /* start in the middle          */
static int g_over = 0;    /* 0 playing, 1 X wins, 2 O wins, 3 draw         */

static const int LINES[8][3] = {
    {0,1,2},{3,4,5},{6,7,8},        /* rows    */
    {0,3,6},{1,4,7},{2,5,8},        /* columns */
    {0,4,8},{2,4,6}                 /* diagonals */
};

void ttt_open(void)
{
    int i;
    for (i = 0; i < 9; ++i)
        g_b[i] = 0;
    g_over = 0;
}

static int winner(void)
{
    int i;
    for (i = 0; i < 8; ++i) {
        int a = LINES[i][0], b = LINES[i][1], c = LINES[i][2];
        if (g_b[a] != 0 && g_b[a] == g_b[b] && g_b[b] == g_b[c])
            return g_b[a];
    }
    return 0;
}

static bool_t board_full(void)
{
    int i;
    for (i = 0; i < 9; ++i)
        if (g_b[i] == 0)
            return FALSE;
    return TRUE;
}

/* The empty cell that completes a line already holding two of `who`. */
static int line_move(int who)
{
    int i;
    for (i = 0; i < 8; ++i) {
        int a = LINES[i][0], b = LINES[i][1], c = LINES[i][2];
        int cnt = (g_b[a]==who) + (g_b[b]==who) + (g_b[c]==who);
        int emp = (g_b[a]==0)   + (g_b[b]==0)   + (g_b[c]==0);
        if (cnt == 2 && emp == 1) {
            if (g_b[a] == 0) return a;
            if (g_b[b] == 0) return b;
            return c;
        }
    }
    return -1;
}

static void machine_move(void)
{
    static const int CORNERS[4] = { 0, 2, 6, 8 };
    int m, i;
    m = line_move(2);                       /* take a win                   */
    if (m < 0) m = line_move(1);            /* block your win               */
    if (m < 0 && g_b[4] == 0) m = 4;        /* centre                       */
    if (m < 0)
        for (i = 0; i < 4 && m < 0; ++i)
            if (g_b[CORNERS[i]] == 0) m = CORNERS[i];
    if (m < 0)
        for (i = 0; i < 9 && m < 0; ++i)
            if (g_b[i] == 0) m = i;
    if (m >= 0)
        g_b[m] = 2;
}

static void settle(void)
{
    int w = winner();
    if (w)              g_over = w;
    else if (board_full()) g_over = 3;
}

/* Cell size and grid origin within the client. */
static void geom(const Rect *cl, int *cell, int *gx, int *gy)
{
    int avail = (cl->w < cl->h ? cl->w : cl->h) - 12 - (font_h() + 8);
    int c = avail / 3;
    if (c < 10) c = 10;
    *cell = c;
    *gx = cl->x + (cl->w - c * 3) / 2;
    *gy = cl->y + 6;
}

void ttt_draw(const Rect *cl)
{
    int cell, gx, gy, i;
    const char *msg;
    geom(cl, &cell, &gx, &gy);

    for (i = 0; i < 9; ++i) {
        int x = gx + (i % 3) * cell, y = gy + (i / 3) * cell;
        ui_fill_face(x + 1, y + 1, cell - 2, cell - 2);
        ui_sink(x + 1, y + 1, cell - 2, cell - 2);
        if (i == g_cur && !g_over)         /* the keyboard cursor          */
            vid_rect(x + 2, y + 2, cell - 4, cell - 4, C_TITLE);
        if (g_b[i] == 1) {                 /* X: two diagonals */
            int s = 7, n = cell - 14, p;
            for (p = 0; p < n; ++p) {
                vid_pixel(x + s + p,     y + s + p,         C_RED);
                vid_pixel(x + s + p + 1, y + s + p,         C_RED);
                vid_pixel(x + s + p,     y + s + n - 1 - p, C_RED);
                vid_pixel(x + s + p + 1, y + s + n - 1 - p, C_RED);
            }
        } else if (g_b[i] == 2) {          /* O: a square ring */
            int s = 7, n = cell - 14;
            vid_rect(x + s,     y + s,     n,     n,     C_BLUE);
            vid_rect(x + s + 1, y + s + 1, n - 2, n - 2, C_BLUE);
        }
    }

    if      (g_over == 1) msg = "You win!  (click = new)";
    else if (g_over == 2) msg = "I win.  (click = new)";
    else if (g_over == 3) msg = "Draw.  (click = new)";
    else                  msg = "You are X - your move";
    ui_text_center(cl->x, gy + cell * 3 + 4, cl->w, msg,
                   g_over ? C_RED : C_BLACK);
}

/* Take a square, if it is free, then let the machine answer.  Shared by
   the mouse and the keyboard. */
static void play_cell(int i)
{
    if (i < 0 || i > 8 || g_b[i] != 0)
        return;
    g_b[i] = 1;                            /* your move                     */
    settle();
    if (!g_over) {
        machine_move();
        settle();
    }
}

void ttt_click(const Rect *cl, int mx, int my)
{
    int cell, gx, gy, c, r;
    if (g_over) {                          /* game over -> new game         */
        ttt_open();
        return;
    }
    geom(cl, &cell, &gx, &gy);
    c = (mx - gx) / cell;
    r = (my - gy) / cell;
    if (mx < gx || my < gy || c < 0 || c > 2 || r < 0 || r > 2)
        return;
    play_cell(r * 3 + c);
    if (g_over == 1)  music_sfx(1047, 3);  /* you win                       */
    else if (g_over)  music_sfx(200, 4);   /* machine win / draw            */
    else              music_sfx(330, 1);   /* the exchange of moves         */
}

/* These four were entirely mouse-driven: focusing one used to swallow
   every keystroke, and Fifteen had no way to restart at all short of
   closing the window.  F2 starts a fresh game, as in the other ten. */
/* Arrows walk the board, Enter or Space takes the square.  Without this
   Tic Tac Toe needed a mouse, and main() treats the mouse as optional. */
bool_t ttt_key(int key)
{
    if (key == KEY_F2) {
        ttt_open();
        return TRUE;
    }
    if (g_over && (key == KEY_ENTER || key == ' ')) {
        ttt_open();
        return TRUE;
    }
    switch (key) {
    case KEY_LEFT:  g_cur = (g_cur % 3 == 0) ? g_cur + 2 : g_cur - 1; return TRUE;
    case KEY_RIGHT: g_cur = (g_cur % 3 == 2) ? g_cur - 2 : g_cur + 1; return TRUE;
    case KEY_UP:    g_cur = (g_cur < 3) ? g_cur + 6 : g_cur - 3;      return TRUE;
    case KEY_DOWN:  g_cur = (g_cur > 5) ? g_cur - 6 : g_cur + 3;      return TRUE;
    case KEY_ENTER:
    case ' ':
        play_cell(g_cur);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}
