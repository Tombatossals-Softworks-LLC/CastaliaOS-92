/* ======================================================================
 * patience.c - Patience (klondike solitaire) for CASTALIA/386
 * ----------------------------------------------------------------------
 * Cards are one byte: suit in the high nibble (0 hearts, 1 diamonds,
 * 2 spades, 3 clubs), rank 1..13 in the low nibble.  Hearts/diamonds are
 * red.  All state is far (DGROUP is precious).  The renderer draws the
 * whole table into the window's client every repaint - it is a few dozen
 * fills, cheap since the fast path repaints only this window.
 * ====================================================================== */
#include <stdio.h>
#include "patience.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "music.h"

#define PT_COLS 7
#define SUIT(c) ((int)((c) >> 4))
#define RANK(c) ((int)((c) & 15))
#define IS_RED(c) (SUIT(c) < 2)

/* The table. */
static u8 far pt_col[PT_COLS][20];     /* tableau piles, bottom first     */
static int    pt_n[PT_COLS];           /* cards in each pile              */
static int    pt_down[PT_COLS];        /* how many of those are face-down */
static u8 far pt_stock[24];
static int    pt_nstock;
static u8 far pt_waste[24];
static int    pt_nwaste;
static u8     pt_found[4];             /* top rank per suit (0 = empty)   */

/* Selection: source -1 = none, 0..6 = column (pt_seln cards from the
   top), 7 = the waste's top card. */
static int pt_sel = -1;
static int pt_seln = 1;

static int pt_moves;
static int pt_won;

/* The win cascade: one card at a time bounces across the client leaving
   a trail (drawn without clearing, exactly like the classic). */
static int cas_card, cas_x, cas_y, cas_vx, cas_vy, cas_on;
static int cas_clw, cas_clh;           /* client size seen by the last draw */
static int cas_cw = 40, cas_ch = 30;   /* card size seen by the last draw   */
static unsigned long cas_last;

static unsigned long pt_seed = 0x0CA5CADEUL;
static unsigned pt_rnd(void)
{
    pt_seed = pt_seed * 1103515245UL + 12345UL;
    return (unsigned)(pt_seed >> 16);
}

void patience_open(void)
{
    u8 deck[52];
    int i, c;

    for (i = 0; i < 52; ++i)
        deck[i] = (u8)(((i / 13) << 4) | (i % 13 + 1));
    pt_seed ^= sys_ticks() | 1UL;
    for (i = 51; i > 0; --i) {         /* Fisher-Yates                    */
        int j = (int)(pt_rnd() % (unsigned)(i + 1));
        u8 t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }

    c = 0;
    for (i = 0; i < PT_COLS; ++i) {
        int k;
        pt_n[i]    = i + 1;
        pt_down[i] = i;                /* all but the top card face down  */
        for (k = 0; k <= i; ++k)
            pt_col[i][k] = deck[c++];
    }
    pt_nstock = 52 - c;
    for (i = 0; i < pt_nstock; ++i)
        pt_stock[i] = deck[c++];
    pt_nwaste = 0;
    for (i = 0; i < 4; ++i)
        pt_found[i] = 0;
    pt_sel = -1;
    pt_moves = 0;
    pt_won = 0;
    cas_on = 0;
}

/* ---- rules ------------------------------------------------------------ */

static void pt_check_win(void)
{
    if (pt_found[0] == 13 && pt_found[1] == 13 &&
        pt_found[2] == 13 && pt_found[3] == 13) {
        pt_won   = 1;
        cas_on   = 0;                  /* the first tick launches a card  */
        cas_card = 0;
        cas_last = sys_ticks();
        music_sfx(1046, 3);
    }
}

/* Move a card to its foundation if it fits.  from: 0..6 column top,
   7 waste top.  TRUE on success. */
static bool_t pt_to_foundation(int from)
{
    u8 c;
    if (from == 7) {
        if (pt_nwaste == 0) return FALSE;
        c = pt_waste[pt_nwaste - 1];
    } else {
        if (pt_n[from] == 0) return FALSE;
        c = pt_col[from][pt_n[from] - 1];
    }
    if (RANK(c) != pt_found[SUIT(c)] + 1)
        return FALSE;
    pt_found[SUIT(c)] = (u8)RANK(c);
    if (from == 7) {
        --pt_nwaste;
    } else {
        --pt_n[from];
        if (pt_n[from] > 0 && pt_n[from] == pt_down[from])
            --pt_down[from];           /* flip the exposed card           */
    }
    ++pt_moves;
    music_sfx(760, 1);
    pt_check_win();
    return TRUE;
}

/* Move pt_seln cards from source `from` onto column `to`.  TRUE if legal. */
static bool_t pt_to_column(int from, int n, int to)
{
    u8 first;
    if (from == to)
        return FALSE;
    if (from == 7) {
        if (pt_nwaste == 0) return FALSE;
        first = pt_waste[pt_nwaste - 1];
        n = 1;
    } else {
        if (n < 1 || n > pt_n[from] - pt_down[from]) return FALSE;
        first = pt_col[from][pt_n[from] - n];
    }
    if (pt_n[to] == 0) {
        if (RANK(first) != 13)         /* only a king starts a column     */
            return FALSE;
    } else {
        u8 top = pt_col[to][pt_n[to] - 1];
        if (IS_RED(first) == IS_RED(top) || RANK(first) != RANK(top) - 1)
            return FALSE;
    }
    if (pt_n[to] + n > 20)
        return FALSE;
    if (from == 7) {
        pt_col[to][pt_n[to]++] = pt_waste[--pt_nwaste];
    } else {
        int k;
        for (k = 0; k < n; ++k)
            pt_col[to][pt_n[to] + k] = pt_col[from][pt_n[from] - n + k];
        pt_n[to] += n;
        pt_n[from] -= n;
        if (pt_n[from] > 0 && pt_n[from] == pt_down[from])
            --pt_down[from];
    }
    ++pt_moves;
    music_sfx(430, 1);
    return TRUE;
}

/* ---- geometry ---------------------------------------------------------- */

typedef struct {
    int s;                             /* pixel scale (1 or 2)            */
    int cw, ch;                        /* card size                       */
    int colw;                          /* column pitch                    */
    int ox, top_y, tab_y;              /* margins / row anchors           */
    int dy_down, dy_up;                /* fan offsets                     */
} PtGeom;

static void pt_geom(const Rect *cl, PtGeom *g)
{
    g->s       = font_h() / 8;
    if (g->s < 1) g->s = 1;
    g->colw    = cl->w / PT_COLS;
    /* pt_click divides by colw; a client narrower than PT_COLS pixels made
       that an INT 0.  Corral and Breaker both floor their cell size - so
       does this now. */
    if (g->colw < 1) g->colw = 1;
    g->cw      = g->colw - 3 * g->s;
    g->ch      = 26 * g->s;
    g->ox      = cl->x + (cl->w - g->colw * PT_COLS) / 2 + 1;
    g->top_y   = cl->y + 2 * g->s;
    g->tab_y   = g->top_y + g->ch + 4 * g->s;
    g->dy_down = 3 * g->s;
    g->dy_up   = 7 * g->s;
}

/* ---- drawing ----------------------------------------------------------- */

/* 8x8 suit glyphs (bit 7 leftmost), drawn with vid_bits8. */
static const u8 far SUITG[4][8] = {
    { 0x00,0x66,0xFF,0xFF,0x7E,0x3C,0x18,0x00 },   /* hearts   */
    { 0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x3C,0x18 },   /* diamonds */
    { 0x18,0x3C,0x7E,0xFF,0xFF,0x5A,0x18,0x3C },   /* spades   */
    { 0x18,0x3C,0x18,0x5A,0xFF,0x5A,0x18,0x3C }    /* clubs    */
};
static const char far RANKCH[14] = " A23456789TJQK";

static void draw_suit(int x, int y, int suit, int s)
{
    int r, c;
    u8 col = (suit < 2) ? C_RED : C_BLACK;
    for (r = 0; r < 8; ++r) {
        u8 bits = SUITG[suit][r];
        if (!bits)
            continue;
        if (s == 1) {
            vid_bits8(x, y + r, bits, col);
        } else {
            /* Coalesce runs of lit bits.  This is the path Mode 12h ALWAYS
               takes (s = font_h()/8 = 2 there), and one clipped fillrect
               per lit pixel meant up to 64 per glyph - about 2900 calls to
               repaint a full tableau. */
            c = 0;
            while (c < 8) {
                int run;
                if (!(bits & (0x80 >> c))) { ++c; continue; }
                run = 1;
                while (c + run < 8 && (bits & (0x80 >> (c + run))))
                    ++run;
                vid_fillrect(x + c * s, y + r * s, run * s, s, col);
                c += run;
            }
        }
    }
}

static void draw_card(const PtGeom *g, int x, int y, u8 c,
                      bool_t facedown, bool_t sel)
{
    int s = g->s;
    /* A real card: clipped corners, a shadow so an overlapping pile
       separates instead of merging into a striped awning, and a mirrored
       bottom-right index.  It was a plain white rectangle with square
       corners and one corner pip, which is placeholder art on the one
       screen made entirely of cards. */
    vid_fillrect(x + 1, y, g->cw - 2, g->ch, facedown ? C_TITLE : C_WHITE);
    vid_fillrect(x, y + 1, g->cw, g->ch - 2, facedown ? C_TITLE : C_WHITE);
    vid_hline(x + 1, y,            g->cw - 2, C_BLACK);
    vid_hline(x + 1, y + g->ch - 1, g->cw - 2, C_BLACK);
    vid_vline(x,            y + 1, g->ch - 2, C_BLACK);
    vid_vline(x + g->cw - 1, y + 1, g->ch - 2, C_BLACK);
    /* A hairline down the right and along the foot, so a fanned pile
       reads as separate cards rather than one block. */
    vid_vline(x + g->cw, y + 2, g->ch - 2, C_DKGRAY);
    vid_hline(x + 2, y + g->ch, g->cw - 1, C_DKGRAY);
    if (facedown) {                    /* patterned back                  */
        vid_dither_rect(x + 2 * s, y + 2 * s, g->cw - 4 * s, g->ch - 4 * s,
                        C_LTBLUE);
        vid_hline(x + 2 * s, y + 2 * s, g->cw - 4 * s, C_HILIGHT);
        return;
    }
    {
        char rt[3];
        u8 col = IS_RED(c) ? C_RED : C_BLACK;
        int rk = RANK(c);
        if (rk == 10) { rt[0] = '1'; rt[1] = '0'; rt[2] = '\0'; }
        else          { rt[0] = RANKCH[rk]; rt[1] = '\0'; }
        font_draw(x + 2 * s, y + s, rt, col);
        draw_suit(x + g->cw - 9 * s, y + s, SUIT(c), s);
        /* The mirrored index, and a centre pip - both of which every
           real deck carries and neither of which was here.  Only when
           the card is tall enough to hold them without crowding. */
        if (g->ch >= font_h() * 3) {
            font_draw(x + g->cw - 3 * s - font_text_width(rt),
                      y + g->ch - font_h() - s, rt, col);
            draw_suit(x + (g->cw - 7 * s) / 2, y + (g->ch - 7 * s) / 2,
                      SUIT(c), s);
        }
    }
    if (sel) {
        vid_rect(x + 1, y + 1, g->cw - 2, g->ch - 2, C_BLUE);
        vid_rect(x + 2, y + 2, g->cw - 4, g->ch - 4, C_BLUE);
    }
}

static void draw_slot(const PtGeom *g, int x, int y)
{
    vid_rect(x, y, g->cw, g->ch, C_DKGRAY);
}

void patience_draw(const Rect *cl)
{
    PtGeom g;
    int i;
    char buf[36];
    pt_geom(cl, &g);
    cas_clw = cl->w;
    cas_clh = cl->h;
    cas_cw  = g.cw;
    cas_ch  = g.ch;

    if (pt_won) {
        /* The cascade: draw ONE new card position per repaint on top of
           whatever is already there - the trail paints itself. */
        if (cas_on)
            draw_card(&g, cl->x + cas_x, cl->y + cas_y,
                      (u8)cas_card, FALSE, FALSE);
        ui_text_center(cl->x, cl->y + 2, cl->w,
                       "* YOU WIN - click for a new deal *", C_YELLOW);
        return;
    }

    vid_fillrect(cl->x, cl->y, cl->w, cl->h, C_GREEN);

    /* Stock. */
    if (pt_nstock > 0)
        draw_card(&g, g.ox, g.top_y, 0, TRUE, FALSE);
    else
        draw_slot(&g, g.ox, g.top_y);
    /* Waste. */
    if (pt_nwaste > 0)
        draw_card(&g, g.ox + g.colw, g.top_y, pt_waste[pt_nwaste - 1],
                  FALSE, (pt_sel == 7) ? TRUE : FALSE);
    else
        draw_slot(&g, g.ox + g.colw, g.top_y);
    /* Foundations (columns 3..6). */
    for (i = 0; i < 4; ++i) {
        int x = g.ox + (3 + i) * g.colw;
        if (pt_found[i] > 0)
            draw_card(&g, x, g.top_y, (u8)((i << 4) | pt_found[i]),
                      FALSE, FALSE);
        else {
            draw_slot(&g, x, g.top_y);
            draw_suit(x + (g.cw - 8 * g.s) / 2,
                      g.top_y + (g.ch - 8 * g.s) / 2, i, g.s);
        }
    }

    /* Tableau. */
    for (i = 0; i < PT_COLS; ++i) {
        int x = g.ox + i * g.colw, y = g.tab_y, k;
        if (pt_n[i] == 0) {
            draw_slot(&g, x, y);
            continue;
        }
        for (k = 0; k < pt_n[i]; ++k) {
            bool_t fd  = (k < pt_down[i]) ? TRUE : FALSE;
            bool_t sel = (pt_sel == i && k >= pt_n[i] - pt_seln) ? TRUE : FALSE;
            draw_card(&g, x, y, pt_col[i][k], fd, sel);
            y += fd ? g.dy_down : g.dy_up;
        }
    }

    sprintf(buf, "Moves %d   N: new deal", pt_moves);
    font_draw(cl->x + 2 * g.s, cl->y + cl->h - font_h() - 1, buf, C_CREAM);
}

/* ---- the win cascade --------------------------------------------------- */

bool_t patience_tick(void)
{
    if (!pt_won)
        return FALSE;
    if (sys_ticks() == cas_last && cas_on)
        return FALSE;                  /* at most one step per BIOS tick  */
    cas_last = sys_ticks();
    if (!cas_on) {                     /* launch the next card            */
        int suit = (int)(pt_rnd() & 3);
        int span = cas_clw - cas_cw;
        if (span < 20) span = 20;
        cas_card = (suit << 4) | (int)(1 + pt_rnd() % 13);
        cas_x  = (int)(pt_rnd() % (unsigned)span);
        cas_y  = 10;
        cas_vx = ((pt_rnd() & 1) ? 3 : -3) * ((int)(pt_rnd() % 3) + 1);
        cas_vy = 0;
        cas_on = 1;
        return TRUE;
    }
    cas_vy += 2;
    cas_x  += cas_vx;
    cas_y  += cas_vy;
    /* The vid_* primitives clip to the SCREEN, not this window, so keep
       every card wholly inside the client - sill and side exits use the
       card's real size (it doubles in Mode 12h). */
    if (cas_y > cas_clh - cas_ch) {    /* bounce off the sill             */
        cas_y  = cas_clh - cas_ch;
        cas_vy = -(cas_vy * 3) / 4;
        if (cas_vy > -3)
            cas_on = 0;                /* spent: launch another next tick */
    }
    if (cas_x < 0 || cas_x > cas_clw - cas_cw)
        cas_on = 0;
    return TRUE;
}

/* ---- input ------------------------------------------------------------- */

/* Turn the next stock card onto the waste (or recycle an empty stock).
   Shared by the stock click and the SPACE key. */
static void pt_deal(void)
{
    if (pt_nstock > 0) {
        pt_waste[pt_nwaste++] = pt_stock[--pt_nstock];
    } else {                                       /* recycle the waste   */
        while (pt_nwaste > 0)
            pt_stock[pt_nstock++] = pt_waste[--pt_nwaste];
        music_sfx(300, 1);
    }
    pt_sel = -1;
}

bool_t patience_key(int key)
{
    if (key == KEY_F2) {          /* F2 = New Game, everywhere */
        patience_open();
        return TRUE;
    }
    if (key == 'n' || key == 'N') {
        patience_open();
        return TRUE;
    }
    if (key == ' ') {                  /* SPACE deals - no aiming needed   */
        if (pt_won) {
            patience_open();
            return TRUE;
        }
        pt_deal();
        return TRUE;
    }
    return FALSE;
}

void patience_click(const Rect *cl, int mx, int my, bool_t dbl)
{
    PtGeom g;
    int col;

    if (pt_won) {
        patience_open();
        return;
    }
    pt_geom(cl, &g);
    col = (mx - g.ox) / g.colw;
    if (col < 0 || col >= PT_COLS || mx < g.ox)
        col = -1;

    /* Top row: stock / waste / foundations. */
    if (col >= 0 && my >= g.top_y && my < g.tab_y - 2 * g.s) {
        if (col == 0) {                            /* the stock           */
            pt_deal();
            return;
        }
        if (col == 1) {                            /* the waste           */
            if (dbl) { pt_sel = -1; pt_to_foundation(7); return; }
            pt_sel = (pt_sel == 7) ? -1 : (pt_nwaste > 0 ? 7 : -1);
            pt_seln = 1;
            return;
        }
        if (col >= 3) {                            /* a foundation        */
            if (pt_sel >= 0 && pt_seln == 1) {
                int from = pt_sel;
                pt_sel = -1;
                pt_to_foundation(from);
            }
            return;
        }
        pt_sel = -1;
        return;
    }

    /* Tableau. */
    if (col >= 0 && my >= g.tab_y - 2 * g.s) {
        if (pt_sel >= 0) {                         /* try to drop here    */
            int from = pt_sel, n = pt_seln;
            pt_sel = -1;
            if (pt_to_column(from, n, col))
                return;
            /* An illegal drop falls through to select the clicked pile. */
        }
        if (pt_n[col] > 0) {
            /* Which card was clicked?  Walk the fan from the bottom. */
            int y = g.tab_y, k, hitk = pt_n[col] - 1;
            for (k = 0; k < pt_n[col] - 1; ++k) {
                int step = (k < pt_down[col]) ? g.dy_down : g.dy_up;
                if (my < y + step) { hitk = k; break; }
                y += step;
            }
            if (hitk < pt_down[col])               /* face-down: only top */
                hitk = pt_n[col] - 1;
            if (hitk >= pt_down[col]) {
                if (dbl && hitk == pt_n[col] - 1) {
                    pt_to_foundation(col);
                    return;
                }
                pt_sel  = col;
                pt_seln = pt_n[col] - hitk;
            }
        }
        return;
    }
    pt_sel = -1;
}
