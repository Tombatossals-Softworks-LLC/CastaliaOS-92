/* ======================================================================
 * eyes.c - Eyes (a desk toy) for CASTALIA/386
 * ====================================================================== */
#include "eyes.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"

static int e_ox[2], e_oy[2];           /* current pupil offsets           */
static int e_blink;                    /* >0: lids closed for that long   */
static unsigned long e_next_blink;
static unsigned long e_seed = 0xB11FB11FUL;

static unsigned e_rnd(void)
{
    e_seed = e_seed * 1103515245UL + 12345UL;
    return (unsigned)(e_seed >> 16);
}

void eyes_open(void)
{
    e_ox[0] = e_oy[0] = e_ox[1] = e_oy[1] = 0;
    e_blink = 0;
    e_seed ^= sys_ticks() | 1UL;
    e_next_blink = sys_ticks() + 40UL + (e_rnd() % 90U);
}

static int e_isqrt(long v)
{
    long x, y;
    if (v <= 0) return 0;
    x = v; y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return (int)x;
}

/* Eye geometry from the client rectangle. */
static void e_geom(const Rect *cl, int *r, int *cx0, int *cx1, int *cy)
{
    int rr = (cl->w / 4 < cl->h / 2) ? cl->w / 4 - 4 : cl->h / 2 - 4;
    if (rr < 8) rr = 8;
    *r   = rr;
    *cx0 = cl->x + cl->w / 4 + 1;
    *cx1 = cl->x + (3 * cl->w) / 4 - 1;
    *cy  = cl->y + cl->h / 2;
}

static void e_disc(int cx, int cy, int r, u8 col)
{
    int dy;
    for (dy = -r; dy <= r; ++dy) {
        int hw = e_isqrt((long)r * r - (long)dy * dy);
        vid_hline(cx - hw, cy + dy, hw * 2 + 1, col);
    }
}

bool_t eyes_mouse(const Rect *cl, int mx, int my)
{
    int r, cx[2], cy, i, changed = 0;
    e_geom(cl, &r, &cx[0], &cx[1], &cy);
    for (i = 0; i < 2; ++i) {
        int dx = mx - cx[i], dy = my - cy;
        int maxo = r - r / 3 - 3;
        long d2 = (long)dx * dx + (long)dy * dy;
        int ox, oy;
        if (d2 > (long)maxo * maxo) {
            int len = e_isqrt(d2);
            ox = (int)((long)dx * maxo / len);
            oy = (int)((long)dy * maxo / len);
        } else {
            ox = dx; oy = dy;
        }
        if (ox != e_ox[i] || oy != e_oy[i]) {
            e_ox[i] = ox; e_oy[i] = oy;
            changed = 1;
        }
    }
    return changed ? TRUE : FALSE;
}

bool_t eyes_tick(void)
{
    unsigned long now = sys_ticks();
    if (e_blink > 0) {
        if (now >= e_next_blink) {     /* lids up again                   */
            e_blink = 0;
            e_next_blink = now + 40UL + (e_rnd() % 90U);
            return TRUE;
        }
        return FALSE;
    }
    if (now >= e_next_blink) {         /* blink!                          */
        e_blink = 1;
        e_next_blink = now + 3UL;      /* closed for ~1/6 s               */
        return TRUE;
    }
    return FALSE;
}

void eyes_draw(const Rect *cl)
{
    int r, cx[2], cy, i;
    e_geom(cl, &r, &cx[0], &cx[1], &cy);
    for (i = 0; i < 2; ++i) {
        e_disc(cx[i], cy, r, C_DKGRAY);            /* outline              */
        e_disc(cx[i], cy, r - 1, C_WHITE);         /* sclera               */
        if (e_blink) {                             /* closed lid           */
            e_disc(cx[i], cy, r - 1, C_FACE);
            vid_hline(cx[i] - r + 2, cy, 2 * r - 3, C_DKGRAY);
        } else {
            e_disc(cx[i] + e_ox[i], cy + e_oy[i], r / 3, C_BLUE);
            e_disc(cx[i] + e_ox[i] - r / 12 - 1,
                   cy + e_oy[i] - r / 12 - 1, r / 9, C_WHITE);
        }
    }
}
