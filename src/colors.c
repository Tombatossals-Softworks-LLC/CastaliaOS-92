/* ======================================================================
 * colors.c - Palette viewer utility for CASTALIA/386
 * ====================================================================== */
#include <string.h>
#include "colors.h"
#include "video.h"
#include "ui.h"
#include "font.h"

static const char *NAMES[16] = {
    "Black",  "Title",   "Desktop",  "Face",
    "Shadow", "Hilight", "White",    "LtBlue",
    "Red",    "Yellow",  "DkYellow", "Green",
    "DkGray", "Blue",    "Cyan",     "Cream"
};

static char g_theme[16] = "classic";

void colors_open(const char *theme)
{
    if (theme != NULL && theme[0]) {
        int i = 0;
        while (theme[i] && i < (int)sizeof(g_theme) - 1) { g_theme[i] = theme[i]; ++i; }
        g_theme[i] = '\0';
    }
}

void colors_draw(const Rect *cl)
{
    int cols = 2, rows = 8, gap = 2;
    int labelw = font_adv() * 8 + 2;
    int cellw, cellh, gx, gy, i;
    char head[40];

    strcpy(head, "Theme: ");
    strcat(head, g_theme);
    font_draw(cl->x + 4, cl->y + 3, head, C_BLACK);

    gx = cl->x + 4;
    gy = cl->y + font_h() + 6;
    cellw = (cl->w - 8 - (cols - 1) * gap) / cols;
    cellh = (cl->h - (gy - cl->y) - font_h() - 6 - (rows - 1) * gap) / rows;
    if (cellw < labelw + 10) cellw = labelw + 10;
    if (cellh < font_h() + 6) cellh = font_h() + 6;

    for (i = 0; i < 16; ++i) {
        int cx = gx + (i % cols) * (cellw + gap);
        int cy = gy + (i / cols) * (cellh + gap);
        int sw = cellh - 2;
        vid_fillrect(cx, cy, sw, sw, (u8)i);          /* the colour itself   */
        vid_rect(cx, cy, sw, sw, C_DKGRAY);
        font_draw(cx + sw + 3, cy + (sw - font_h()) / 2, NAMES[i], C_BLACK);
    }

    ui_text_center(cl->x, cl->y + cl->h - font_h() - 2, cl->w,
                   "Set theme= (or [colors]) in CASTALIA.INI", C_DKGRAY);
}
