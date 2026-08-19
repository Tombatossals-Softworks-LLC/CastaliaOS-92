/* ======================================================================
 * dialog.c - Modal dialogs for CASTALIA/386 (Windows-95 style)
 * ----------------------------------------------------------------------
 * Each dialog draws itself over the frozen scene that already sits in the
 * video back buffer, then runs a small private event loop (the same poll
 * -> dispatch -> present shape as the main loop) until the user answers.
 * The software cursor is maintained exactly as in main.c: the back buffer
 * is the restore source, so moving the pointer only repaints two small
 * rectangles.
 *
 * The chrome is the genuine Windows-95 message box: a raised gray panel
 * under a gradient title bar with a close box, OK/Cancel (or Yes/No)
 * buttons that PRESS IN under the mouse and only fire on release, the
 * default button wearing the heavy black ring, and a dotted focus
 * rectangle you can drive with Tab and the arrow keys.
 * ====================================================================== */
#include <string.h>
#include "dialog.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "mouse.h"
#include "keyboard.h"
#include "music.h"
#include "system.h"   /* sys_idle */

#define DLG_MSG     0
#define DLG_ASK     1
#define DLG_INPUT   2

/* Geometry computed by layout(), shared by draw and hit-testing. All of
   it scales with the active font, so dialogs grow in Mode 12h. */
static Rect g_box, g_btn1, g_btn2, g_field, g_close;
static bool_t g_two_btn;
static int g_th, g_body_y, g_lh;   /* title height, first body y, line h  */

/* Live interaction state, read back by draw_dialog(). */
static int g_focus;    /* button with the keyboard focus / default ring   */
static int g_press;    /* button the mouse went down on (-1 = none)        */
static int g_pvis;     /* button currently drawn pressed-in (-1 = none)    */
static bool_t g_input_fresh;  /* offered text still "selected"             */

/* Control ids for g_focus / g_press / g_pvis. */
#define C_BTN1   0
#define C_BTN2   1
#define C_CLOSE  2

static int nbtn(void) { return g_two_btn ? 2 : 1; }

static void layout(int kind, bool_t has_line2)
{
    int fh   = font_h();
    int boxw = font_adv() * 36;    /* 216 at the 8px font                  */
    int btnw = font_adv() * 8 + 2; /* 50                                   */
    int btnh = fh + 5;             /* 13                                   */
    int content, h, x, y, by, bx, cs;

    g_th = fh + 3;                 /* title bar height (11)                */
    g_lh = fh + 2;                 /* line height      (10)                */

    if (kind == DLG_INPUT)
        content = g_lh + (fh + 5);             /* prompt + field           */
    else
        content = fh + (has_line2 ? g_lh : 0); /* one or two text lines    */

    h = 2 + g_th + 4 + content + 6 + btnh + 6;
    x = (SCREEN_W - boxw) / 2;
    y = (SCREEN_H - h) / 2;
    rect_set(&g_box, x, y, boxw, h);

    g_body_y = y + 2 + g_th + 4;
    rect_set(&g_field, x + 8, g_body_y + g_lh, boxw - 16, fh + 5);

    /* Close box tucked into the right end of the title bar. */
    cs = g_th - 2;
    rect_set(&g_close, x + 2 + (boxw - 4) - cs - 1, y + 3, cs, cs);

    by = y + h - btnh - 6;
    g_two_btn = (kind != DLG_MSG) ? TRUE : FALSE;
    if (g_two_btn) {
        bx = x + (boxw - (btnw * 2 + 12)) / 2;
        rect_set(&g_btn1, bx, by, btnw, btnh);
        rect_set(&g_btn2, bx + btnw + 12, by, btnw, btnh);
    } else {
        bx = x + (boxw - btnw) / 2;
        rect_set(&g_btn1, bx, by, btnw, btnh);
    }
}

/* A one-pixel dotted rectangle - the Windows keyboard focus cue. */
static void focus_rect(int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < w; i += 2) {
        vid_pixel(x + i, y, C_BLACK);
        vid_pixel(x + i, y + h - 1, C_BLACK);
    }
    for (i = 0; i < h; i += 2) {
        vid_pixel(x, y + i, C_BLACK);
        vid_pixel(x + w - 1, y + i, C_BLACK);
    }
}

/* One push button with its Windows-95 states: sunken while held, the
   black default ring when it owns the focus, and the dotted focus cue. */
static void draw_button(const Rect *r, const char *label, int id)
{
    bool_t pressed = (g_pvis == id) ? TRUE : FALSE;
    if (id == g_focus)                            /* heavy default ring     */
        vid_rect(r->x - 2, r->y - 2, r->w + 4, r->h + 4, C_BLACK);
    ui_button(r, label, pressed);
    if (id == g_focus && !pressed)                /* dotted keyboard cue    */
        focus_rect(r->x + 3, r->y + 3, r->w - 6, r->h - 6);
}

/* The title-bar close box: a small raised button carrying an X. */
static void draw_close(void)
{
    int i, s;
    bool_t pressed = (g_pvis == C_CLOSE) ? TRUE : FALSE;
    ui_fill_face(g_close.x, g_close.y, g_close.w, g_close.h);
    if (pressed) ui_sink(g_close.x, g_close.y, g_close.w, g_close.h);
    else         ui_raise(g_close.x, g_close.y, g_close.w, g_close.h);
    s = g_close.h - 6;
    if (s < 3) s = 3;
    for (i = 0; i < s; ++i) {                      /* draw the X            */
        int px = g_close.x + 3 + (pressed ? 1 : 0);
        int py = g_close.y + 3 + (pressed ? 1 : 0);
        vid_pixel(px + i,         py + i, C_BLACK);
        vid_pixel(px + s - 1 - i, py + i, C_BLACK);
    }
}

/* One body line, never wider than the box.  Long text is elided from the
   left with ".." so a path keeps the file name that identifies it. */
static void draw_body(int x, int y, const char *s, int maxch)
{
    int n;
    if (maxch < 4) {
        font_draw_n(x, y, s, maxch, C_BLACK);
        return;
    }
    n = (int)strlen(s);
    if (n <= maxch) {
        font_draw(x, y, s, C_BLACK);
        return;
    }
    font_draw(x, y, "..", C_BLACK);
    font_draw_n(x + 2 * FONT_ADV, y, s + (n - (maxch - 2)), maxch - 2,
                C_BLACK);
}

static void draw_dialog(int kind, const char *title,
                        const char *l1, const char *l2,
                        const char *buf, const char *lbl1, const char *lbl2)
{
    int x = g_box.x, y = g_box.y;

    ui_shadow(g_box.x, g_box.y, g_box.w, g_box.h);
    ui_fill_face(g_box.x, g_box.y, g_box.w, g_box.h);
    ui_raise(g_box.x, g_box.y, g_box.w, g_box.h);

    /* Title bar (matches the gradient window chrome) plus a close box. */
    vid_title_bar(x + 2, y + 2, g_box.w - 4, g_th, TRUE);
    font_draw(x + 6, y + 2 + (g_th - font_h()) / 2, title, C_WHITE);
    draw_close();

    /* Body text, CLIPPED to the box.  Callers hand these lines whole file
       paths - up to 131 bytes from the Sketch Pad and the Scrap Box - and
       an unclipped font_draw ran them straight off the dialog and across
       whatever was behind it.  A path is elided from the FRONT, since its
       last component is the part that identifies it. */
    {
        int maxb = (g_box.w - 16) / FONT_ADV;
        if (l1 != NULL)
            draw_body(x + 8, g_body_y, l1, maxb);
        if (l2 != NULL && kind != DLG_INPUT)
            draw_body(x + 8, g_body_y + g_lh, l2, maxb);
    }

    /* Input field with the (tail of the) text and a caret. */
    if (kind == DLG_INPUT) {
        int maxch = (g_field.w - 6) / FONT_ADV;
        int len = (int)strlen(buf);
        const char *show = buf;
        int caret;
        if (len > maxch)
            show = buf + (len - maxch);
        vid_fillrect(g_field.x, g_field.y, g_field.w, g_field.h, C_WHITE);
        ui_sink(g_field.x, g_field.y, g_field.w, g_field.h);
        /* Offered text shows SELECTED, so it is obvious that typing
           replaces it rather than appending to it. */
        if (g_input_fresh && show[0] != '\0') {
            vid_fillrect(g_field.x + 2, g_field.y + 2,
                         font_text_width(show) + 2, g_field.h - 4, C_TITLE);
            font_draw(g_field.x + 3, g_field.y + 3, show, C_WHITE);
        } else {
            font_draw(g_field.x + 3, g_field.y + 3, show, C_BLACK);
        }
        caret = g_field.x + 3 + font_text_width(show);
        vid_vline(caret, g_field.y + 2, g_field.h - 4, C_BLACK);
    }

    /* Buttons. */
    draw_button(&g_btn1, lbl1, C_BTN1);
    if (g_two_btn)
        draw_button(&g_btn2, lbl2, C_BTN2);
}

/* TRUE once if any modal dialog ran since the last query: a dialog paints
   over the whole scene from its own little event loop, so the click that
   summoned it must be followed by a full repaint, never a partial one. */
static bool_t g_dlg_ran = FALSE;

/* filedlg.c takes the screen over exactly as these do, and needs the
   same full repaint on the way out - without it the picker's frame is
   left behind in the corners the caller does not redraw. */
void dialog_note_takeover(void) { g_dlg_ran = TRUE; }

bool_t dialog_took_over(void)
{
    bool_t r = g_dlg_ran;
    g_dlg_ran = FALSE;
    return r;
}

static int dialog_run(int kind, const char *title,
                      const char *l1, const char *l2,
                      char *buf, int bufsize)
{
    const char *lbl1 = "OK";
    const char *lbl2 = "Cancel";
    int  result = -1;
    int  close_result;
    int  prev_b;
    int  prev_mx, prev_my;
    bool_t need_redraw = TRUE;
    /* TRUE while the offered text is still "selected": the first
       printable key replaces it rather than appending to it. */
    bool_t fresh = (kind == DLG_INPUT && buf != NULL && buf[0] != '\0')
                   ? TRUE : FALSE;
    g_input_fresh = fresh;

    g_dlg_ran = TRUE;
    music_sfx(880, 2);                 /* the classic attention ding      */
    if (kind == DLG_ASK) { lbl1 = "Yes"; lbl2 = "No"; }

    layout(kind, (l2 != NULL && l2[0] != '\0') ? TRUE : FALSE);
    close_result = (kind == DLG_ASK) ? DLG_NO :
                   (kind == DLG_MSG) ? DLG_OK : DLG_CANCEL;

    g_focus = C_BTN1;              /* the default (OK / Yes) starts focused */
    g_press = -1;
    g_pvis  = -1;

    prev_b  = mouse_buttons();     /* swallow the click that opened us    */
    prev_mx = mouse_x();
    prev_my = mouse_y();

    for (;;) {
        int key, b, mx, my, want_vis, nkey;
        bool_t down, up, held;

        music_sfx_service();           /* let the attention ding end      */
        mouse_update();
        mx = mouse_x();
        my = mouse_y();
        b  = mouse_buttons();
        held = (b & MB_LEFT) ? TRUE : FALSE;
        down = (held && !(prev_b & MB_LEFT)) ? TRUE : FALSE;
        up   = (!held && (prev_b & MB_LEFT)) ? TRUE : FALSE;

        /* Keyboard, DRAINED - see the note in main.c's own poll.  One key
           per pass costs characters on the hardware this shell is for,
           and a character lost out of a FILENAME is not cosmetic: it
           saves to the wrong file.  Stops as soon as a key produces a
           result - the sentinel is -1, not one of the DLG_* values - so
           nothing is acted on after the dialog has logically closed. */
        nkey = 0;
        while (nkey++ < 16 && result < 0 &&
               (key = kb_poll()) != KEY_NONE) {
            if (key == KEY_ESC) {
                result = close_result;
            } else if (key == KEY_ENTER) {
                /* Enter fires whichever button is focused (the default). */
                if (g_focus == C_BTN2) result = (kind == DLG_ASK) ? DLG_NO : DLG_CANCEL;
                else                   result = (kind == DLG_ASK) ? DLG_YES : DLG_OK;
            } else if (key == KEY_TAB) {
                g_focus = (g_focus + 1) % nbtn();
                need_redraw = TRUE;
            } else if (kind != DLG_INPUT &&
                       (key == KEY_LEFT || key == KEY_UP)) {
                g_focus = (g_focus + nbtn() - 1) % nbtn();
                need_redraw = TRUE;
            } else if (kind != DLG_INPUT &&
                       (key == KEY_RIGHT || key == KEY_DOWN)) {
                g_focus = (g_focus + 1) % nbtn();
                need_redraw = TRUE;
            } else if (kind != DLG_INPUT && key == KEY_SPACE) {
                if (g_focus == C_BTN2) result = (kind == DLG_ASK) ? DLG_NO : DLG_CANCEL;
                else                   result = (kind == DLG_ASK) ? DLG_YES : DLG_OK;
            } else if (kind == DLG_INPUT) {
                int len = (int)strlen(buf);
                /* Type-to-replace, as every text field since has done.
                   The callers keep their buffers static so the last value
                   is offered again, but append-only editing turned that
                   into a trap: Run "calc", then Run "scrap", and the
                   shell tried to launch "calcscrap" - which fell through
                   to DOS, blanked the screen and printed an error.
                   The first printable key now replaces the offered text;
                   Backspace (or Del/Ctrl-U) commits to editing it. */
                if (key == KEY_DEL || key == 21 /* Ctrl-U */) {
                    buf[0] = '\0';
                    fresh = FALSE;
                    g_input_fresh = FALSE;
                    need_redraw = TRUE;
                } else if (key == KEY_BACK) {
                    if (fresh) {                /* keep it, start editing  */
                        fresh = FALSE;
                        g_input_fresh = FALSE;
                    } else if (len > 0) {
                        buf[len - 1] = '\0';
                    }
                    need_redraw = TRUE;
                } else if (key >= 32 && key < 127) {
                    if (fresh) {
                        buf[0] = '\0';
                        len = 0;
                        fresh = FALSE;
                        g_input_fresh = FALSE;
                    }
                    if (len < bufsize - 1) {
                        buf[len] = (char)key;
                        buf[len + 1] = '\0';
                    }
                    need_redraw = TRUE;
                }
            }
        }

        /* Mouse press: remember which control was hit and give it focus. */
        if (down) {
            if (rect_contains(&g_btn1, mx, my)) { g_press = C_BTN1; g_focus = C_BTN1; }
            else if (g_two_btn && rect_contains(&g_btn2, mx, my)) { g_press = C_BTN2; g_focus = C_BTN2; }
            else if (rect_contains(&g_close, mx, my)) { g_press = C_CLOSE; }
        }

        /* Depress visual only while the mouse is held over the same control. */
        want_vis = -1;
        if (held && g_press == C_BTN1 && rect_contains(&g_btn1, mx, my)) want_vis = C_BTN1;
        else if (held && g_press == C_BTN2 && rect_contains(&g_btn2, mx, my)) want_vis = C_BTN2;
        else if (held && g_press == C_CLOSE && rect_contains(&g_close, mx, my)) want_vis = C_CLOSE;
        if (want_vis != g_pvis) { g_pvis = want_vis; need_redraw = TRUE; }

        /* Mouse release: fire only if let go over the pressed control. */
        if (up) {
            if (g_press == C_BTN1 && rect_contains(&g_btn1, mx, my))
                result = (kind == DLG_ASK) ? DLG_YES : DLG_OK;
            else if (g_press == C_BTN2 && rect_contains(&g_btn2, mx, my))
                result = (kind == DLG_ASK) ? DLG_NO : DLG_CANCEL;
            else if (g_press == C_CLOSE && rect_contains(&g_close, mx, my))
                result = close_result;
            g_press = -1;
            if (g_pvis != -1) { g_pvis = -1; need_redraw = TRUE; }
        }

        /* Paint. */
        if (need_redraw) {
            mouse_erase();
            draw_dialog(kind, title, l1, l2, buf, lbl1, lbl2);
            vid_blit_rect(g_box.x, g_box.y, g_box.w, g_box.h);
            mouse_draw();
            need_redraw = FALSE;
            prev_mx = mx; prev_my = my;
        } else if (mx != prev_mx || my != prev_my) {
            mouse_erase();
            mouse_draw();
            prev_mx = mx; prev_my = my;
        }

        prev_b = b;
        if (result != -1)
            break;
        sys_idle();                /* the loop used to spin at 100% CPU    */
    }
    /* Every mouse_update() above accrued INT 33h press counts that the
       main loop would otherwise replay as fresh desktop clicks the moment
       we return (a phantom click on whatever sits under the OK button). */
    (void)mouse_take_lpresses();
    (void)mouse_take_rpresses();
    return result;
}

int dialog_message(const char *title, const char *line1, const char *line2)
{
    return dialog_run(DLG_MSG, title, line1, line2, NULL, 0);
}

int dialog_confirm(const char *title, const char *line1, const char *line2)
{
    return dialog_run(DLG_ASK, title, line1, line2, NULL, 0);
}

int dialog_input(const char *title, const char *prompt,
                 char *buf, int bufsize)
{
    return dialog_run(DLG_INPUT, title, prompt, NULL, buf, bufsize);
}
