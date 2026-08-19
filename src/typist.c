/* ======================================================================
 * typist.c - Typing Tutor for CASTALIA/386
 * ----------------------------------------------------------------------
 * A desk drill in the great tradition of the shareware typing tutors:
 * one practice line at a time, strict entry (a wrong key beeps and counts
 * an error but never advances), live words-per-minute off the BIOS tick,
 * and a persistent best in CASTALIA.HI.  The drill book starts on the
 * home row and works up to pangrams and honest DOS folklore.
 *
 * The practice lines live in FAR memory (the DGROUP diet); the one line
 * being typed is copied into a small near buffer for the text renderer.
 * WPM is the classic reckoning: five characters make a word, and the
 * 18.2 Hz tick chain makes 1092 ticks a minute.
 * ====================================================================== */
#include <stdio.h>
#include "typist.h"
#include "video.h"
#include "font.h"
#include "system.h"
#include "ui.h"
#include "keyboard.h"
#include "music.h"
#include "hiscore.h"

/* ---- the drill book (far, ASCII, each line at most 44 characters) ----- */
static const char far T01[] = "asdf jkl; asdf jkl; a;sl dkfj a;sl dkfj";
static const char far T02[] = "dad sad fall; ask a lad; all salads fall";
static const char far T03[] = "qwer uiop qwer uiop; two quiet pots tip";
static const char far T04[] = "zxcv m,./ zxcv m,./ mixed cave, calm zoo";
static const char far T05[] = "1234 5678 90 - type 24, then 68, then 90";
static const char far T06[] = "the quick brown fox jumps over a lazy dog";
static const char far T07[] = "pack my box with five dozen liquor jugs";
static const char far T08[] = "how vexingly quick daft zebras jump";
static const char far T09[] = "sphinx of black quartz, judge my vow";
static const char far T10[] = "dir c: /w /p";
static const char far T11[] = "copy report.txt a: /v";
static const char far T12[] = "cd \\castalia and type castalia to begin";
static const char far T13[] = "Abort, Retry, Ignore, Fail?";
static const char far T14[] = "Insert disk 2 of 5 and press any key.";
static const char far T15[] = "Keep your master diskettes write locked.";
static const char far T16[] = "A backup a day keeps the data loss away.";
static const char far T17[] = "Save early, save often, save to two disks.";
static const char far T18[] = "640 KB ought to be enough for anybody.";
static const char far T19[] = "Real typists never look at the keyboard.";
static const char far T20[] = "The 386 hums; the cursor blinks; you type.";
static const char far T21[] = "Press F1 for help, or just keep typing.";
static const char far T22[] = "Do not fold, spindle or mutilate the card.";
static const char far T23[] = "Party like it is 1992, one line at a time.";
static const char far T24[] = "Practice makes 40 words in every minute.";

static const char far * const far T_LINES[] = {
    T01, T02, T03, T04, T05, T06, T07, T08, T09, T10, T11, T12,
    T13, T14, T15, T16, T17, T18, T19, T20, T21, T22, T23, T24
};
#define TN (int)(sizeof(T_LINES) / sizeof(T_LINES[0]))

#define TMAX 48                /* near copy of the line being typed        */
static char g_line[TMAX];
static int  g_len;

static int  g_cur;             /* which drill line                         */
static int  g_pos;             /* characters correctly typed so far        */
static int  g_err;             /* wrong keys on this line                  */
static int  g_done;            /* the line is complete: stats are final    */
static int  g_lines_done;      /* lines finished this session              */
static int  g_wpm, g_acc;      /* final stats of the finished line         */
static int  g_newbest;         /* the finished line set a record           */
static unsigned long g_start;  /* tick of the first correct key (0 = not)  */
static unsigned long g_shown;  /* last live-WPM repaint tick               */

static unsigned long ticks(void) { return sys_ticks(); }

static void load_line(int idx)
{
    const char far *s = T_LINES[idx];
    int i;
    for (i = 0; i < TMAX - 1 && s[i] != '\0'; ++i)
        g_line[i] = s[i];
    g_line[i] = '\0';
    g_len   = i;
    g_pos   = 0;
    g_err   = 0;
    g_done  = 0;
    g_start = 0;
    g_newbest = 0;
}

void typist_open(void)
{
    g_cur        = 0;
    g_lines_done = 0;
    load_line(g_cur);
}

/* Live WPM while the fingers fly; the final WPM once the line is done. */
static int wpm_now(void)
{
    unsigned long el;
    int chars = g_done ? g_len : g_pos;
    if (g_start == 0 || chars <= 0)
        return 0;
    el = (g_done ? g_shown : ticks()) - g_start;
    if (el < 9UL) el = 9UL;                     /* first half-second       */
    return (int)((long)chars * 1092L / (5L * (long)el));
}

bool_t typist_tick(void)
{
    /* Refresh the live meter twice a second while a line is under way. */
    if (g_start == 0 || g_done)
        return FALSE;
    if (ticks() - g_shown < 9UL)
        return FALSE;
    g_shown = ticks();
    return TRUE;
}

bool_t typist_key(int key)
{
    if (g_done) {
        if (key == KEY_ENTER || key == KEY_SPACE) {
            g_cur = (g_cur + 1) % TN;
            load_line(g_cur);
            return TRUE;
        }
        return FALSE;
    }
    if (key == KEY_ENTER) {                     /* restart this line       */
        load_line(g_cur);
        return TRUE;
    }
    if (key == KEY_TAB) {                       /* skip to the next line   */
        g_cur = (g_cur + 1) % TN;
        load_line(g_cur);
        return TRUE;
    }
    if (key < 32 || key > 126)
        return FALSE;
    if ((char)key == g_line[g_pos]) {
        if (g_start == 0) { g_start = ticks(); g_shown = g_start; }
        ++g_pos;
        if (g_pos >= g_len) {
            g_done  = 1;
            g_shown = ticks();
            ++g_lines_done;
            g_wpm = wpm_now();
            g_acc = (int)((long)g_len * 100 / (g_len + g_err));
            if (g_acc >= 85 &&
                hiscore_submit("typist", (long)g_wpm))
                g_newbest = 1;
            music_sfx(g_newbest ? 1320 : 988, 2);
        }
    } else {
        ++g_err;
        music_sfx(196, 1);                      /* the tutor's tut-tut     */
    }
    return TRUE;
}

void typist_draw(const Rect *cl)
{
    char buf[64];
    int  x0 = cl->x + 6, y = cl->y + 4;
    int  cols = (cl->w - 20) / FONT_ADV;
    int  i, row;

    if (cols < 10) cols = 10;

    sprintf(buf, "Drill %d of %d", g_cur + 1, TN);
    font_draw(x0, y, buf, C_BLACK);
    sprintf(buf, "Best %ld wpm", hiscore_best("typist"));
    font_draw(cl->x + cl->w - 6 - font_text_width(buf), y, buf, C_TITLE);
    y += font_h() + 4;

    /* The copy desk: the line, wrapped if the window is narrow, with the
       typed part dimmed and the next wanted key on a blue caret block. */
    {
        int rows  = (g_len + cols - 1) / cols;
        int box_h = rows * (font_h() + 2) + 8;
        vid_fillrect(cl->x + 4, y, cl->w - 8, box_h, C_WHITE);
        ui_sink(cl->x + 4, y, cl->w - 8, box_h);
        for (row = 0; row < rows; ++row) {
            int from = row * cols;
            int n    = g_len - from;
            int tx   = x0 + 2;
            int ty   = y + 4 + row * (font_h() + 2);
            if (n > cols) n = cols;
            for (i = 0; i < n; ++i) {
                char ch = g_line[from + i];
                if (from + i == g_pos && !g_done) {
                    vid_fillrect(tx - 1, ty - 1, FONT_ADV + 1,
                                 font_h() + 2, C_BLUE);
                    font_draw_char(tx, ty, ch, C_WHITE);
                } else {
                    font_draw_char(tx, ty, ch,
                                   (from + i < g_pos) ? C_SHADOW : C_BLACK);
                }
                tx += FONT_ADV;
            }
        }
        y += box_h + 5;
    }

    if (g_done) {
        sprintf(buf, "%d wpm at %d%% - %s", g_wpm, g_acc,
                g_newbest ? "a new record!" : "well typed.");
        font_draw(x0, y, buf, g_newbest ? C_GREEN : C_BLACK);
    } else {
        sprintf(buf, "wpm %d   errors %d", wpm_now(), g_err);
        font_draw(x0, y, buf, C_BLACK);
    }
    y += font_h() + 3;

    sprintf(buf, "Lines done %d", g_lines_done);
    font_draw(x0, y, buf, C_SHADOW);
    y += font_h() + 5;

    ui_text_center(cl->x, y, cl->w,
                   g_done ? "Enter: next line"
                          : "Type the line.  Enter restarts, Tab skips.",
                   C_TITLE);
}
