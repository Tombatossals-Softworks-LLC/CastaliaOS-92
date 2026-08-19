/* ======================================================================
 * peek.c - Hex Peek, the file inspector for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "peek.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"

#define PK_COLS   8                    /* bytes per row                    */
#define PK_MAXROW 24                   /* enough for a maximized window    */

static char g_path[132] = "";
static long g_size = 0;
static long g_off  = 0;
static int  g_rows = 16;               /* rows shown by the last draw      */

static u8   g_buf[PK_COLS * PK_MAXROW];
static int  g_got = 0;
static long g_buf_off = -1;            /* file offset the buffer holds     */

bool_t peek_open_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return FALSE;
    fseek(f, 0L, SEEK_END);
    g_size = ftell(f);
    fclose(f);

    strcpy(g_path, path);
    g_off = 0;
    g_buf_off = -1;                    /* force a reload                   */
    return TRUE;
}

/* Pull the current page into the buffer (only when the offset moved or
   the window grew and more rows are wanted than were last read). */
static void load_page(void)
{
    FILE *f;
    static unsigned g_buf_want = 0;
    unsigned want = (unsigned)(PK_COLS * g_rows);
    if (want > sizeof(g_buf))
        want = (unsigned)sizeof(g_buf);
    if (g_buf_off == g_off && g_buf_want >= want)
        return;
    g_got = 0;
    g_buf_off = g_off;
    g_buf_want = want;
    f = fopen(g_path, "rb");
    if (f == NULL)
        return;
    fseek(f, g_off, SEEK_SET);
    g_got = (int)fread(g_buf, 1, want, f);
    fclose(f);
}

void peek_draw(const Rect *cl)
{
    int  lh = font_h() + 1;
    int  y  = cl->y + 3;
    int  r, i;
    char line[44];

    g_rows = (cl->h - (font_h() + 6)) / lh;
    if (g_rows < 1)          g_rows = 1;
    if (g_rows > PK_MAXROW)  g_rows = PK_MAXROW;
    load_page();

    /* Header: offset / size, and how to move. */
    sprintf(line, "%06lX of %06lX   PgUp/PgDn", g_off, g_size);
    font_draw(cl->x + 4, y, line, C_TITLE);
    y += lh + 2;

    for (r = 0; r < g_rows; ++r) {
        int  n = g_got - r * PK_COLS;
        char *p = line;
        if (n <= 0)
            break;
        if (n > PK_COLS)
            n = PK_COLS;
        p += sprintf(p, "%06lX ", g_off + (long)r * PK_COLS);
        for (i = 0; i < PK_COLS; ++i) {
            if (i < n)
                p += sprintf(p, "%02X", g_buf[r * PK_COLS + i]);
            else {
                *p++ = ' '; *p++ = ' ';
            }
            if (i == 3)
                *p++ = ' ';
        }
        *p++ = ' ';
        for (i = 0; i < n; ++i) {
            u8 c = g_buf[r * PK_COLS + i];
            *p++ = (char)((c >= 32 && c < 127) ? c : '.');
        }
        *p = '\0';
        font_draw(cl->x + 4, y, line, C_BLACK);
        y += lh;
    }
    if (g_size == 0)
        font_draw(cl->x + 4, y, "(empty file)", C_DKGRAY);
}

bool_t peek_key(int key)
{
    long page = (long)g_rows * PK_COLS;
    long last, o = g_off;

    last = g_size - PK_COLS;           /* keep at least one row on screen  */
    if (last < 0)
        last = 0;

    if      (key == KEY_UP)   o -= PK_COLS;
    else if (key == KEY_DOWN) o += PK_COLS;
    else if (key == KEY_PGUP) o -= page;
    else if (key == KEY_PGDN) o += page;
    else if (key == KEY_HOME) o  = 0;
    else if (key == KEY_END)  o  = (g_size / PK_COLS) * PK_COLS - page + PK_COLS;
    else
        return FALSE;

    if (o > last) o = last;
    if (o < 0)    o = 0;
    o = (o / PK_COLS) * PK_COLS;       /* row-aligned                      */
    if (o == g_off)
        return FALSE;
    g_off = o;
    return TRUE;
}
