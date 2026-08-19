/* ======================================================================
 * picshow.c - Picture Show (a full-screen GIF slideshow) for Castalia 92
 * ----------------------------------------------------------------------
 * The gallery of ASSETS\ICONS\*.GIF - the shipped wallpapers and any
 * picture the user drops beside them - shown full-screen with a caption.
 * Mode 13h only (the pictures need the 256-entry DAC); each slide change
 * rides a palette fade, the same dissolve the Light Show uses.
 * ====================================================================== */
#include <dos.h>       /* _dos_findfirst / _dos_findnext / _dos_allocmem */
#include <stdio.h>
#include <string.h>
#include "picshow.h"
#include "video.h"
#include "font.h"
#include "keyboard.h"
#include "system.h"
#include "gif.h"

#define PS_MAX 24                       /* pictures shown at most          */
#define PS_DIR "ASSETS\\ICONS\\"

static int collect(char names[PS_MAX][13])
{
    struct find_t ff;
    unsigned rc;
    int n = 0;
    char pat[80];
    /* Anchor the scan to the install directory: a relative pattern would
       search wherever the Disk Cabinet last browsed to and come up empty. */
    sys_home_path(pat, (int)sizeof(pat), PS_DIR "*.GIF");
    rc = _dos_findfirst(pat, _A_RDONLY | _A_ARCH, &ff);
    while (rc == 0 && n < PS_MAX) {
        int i = 0;
        while (ff.name[i] != '\0' && i < 12) {
            names[n][i] = ff.name[i];
            ++i;
        }
        names[n][i] = '\0';
        ++n;
        rc = _dos_findnext(&ff);
    }
    return n;
}

/* The Mode-12h / empty-gallery notice: a plain panel, any key returns. */
static void notice(const char *msg)
{
    int w = font_text_width(msg) + 24;
    vid_fillrect(0, 0, SCREEN_W, SCREEN_H, C_DESKTOP);
    vid_fillrect(SCREEN_W / 2 - w / 2, SCREEN_H / 2 - 20, w, 40, C_FACE);
    vid_bevel(SCREEN_W / 2 - w / 2, SCREEN_H / 2 - 20, w, 40,
              C_HILIGHT, C_SHADOW);
    font_draw(SCREEN_W / 2 - w / 2 + 12, SCREEN_H / 2 - font_h() / 2,
              msg, C_BLACK);
    vid_present();
    kb_flush();
    while (kb_poll() == KEY_NONE)
        sys_idle();
}

/* Decode picture k into the far frame buffer and present it, centred on
   black with its caption along the bottom.  FALSE = unreadable file. */
static bool_t show_one(char names[PS_MAX][13], int k, int n, u8 far *buf)
{
    unsigned char pal[768];
    char rel[32], path[80], cap[40];
    int w = 0, h = 0, ncol = 0;
    long i, npix;
    int ox, oy, y;

    sprintf(rel, PS_DIR "%s", names[k]);
    sys_home_path(path, (int)sizeof(path), rel);
    if (!gif_decode(path, buf, SCREEN_W, SCREEN_H, &w, &h, pal, &ncol))
        return FALSE;
    if (ncol > 176) ncol = 176;         /* the free DAC window 16..191     */

    video_fade_out();                   /* dissolve the previous slide     */
    if (ncol > 0)
        video_set_dac(16, ncol, pal);
    npix = (long)w * h;                 /* shift pixels into slots 16..191 */
    for (i = 0; i < npix; ++i) {
        int v = buf[i] + 16;
        if (v > 191) v = 191;
        buf[i] = (u8)v;
    }

    vid_fillrect(0, 0, SCREEN_W, SCREEN_H, C_BLACK);
    ox = (SCREEN_W - w) / 2;
    oy = (SCREEN_H - h) / 2;
    for (y = 0; y < h; ++y)
        vid_copy_row(ox, oy + y, buf + (long)y * w, w);

    /* Both strings on a black plate.  The picture fills the screen, so the
       hint - drawn in mid-grey straight onto the image - disappeared
       completely into any light or busy part of it, and the one line that
       tells you how to leave was the one you could not read. */
    {
        const char *hint = "< > next  ESC exit";
        int sy = SCREEN_H - font_h() - 3;
        sprintf(cap, "%s  (%d/%d)", names[k], k + 1, n);
        vid_fillrect(0, sy - 1, SCREEN_W, font_h() + 4, C_BLACK);
        font_draw(4, sy + 1, cap, C_WHITE);
        font_draw(SCREEN_W - font_text_width(hint) - 4, sy + 1, hint,
                  C_FACE);
    }
    vid_present();
    video_fade_in();                    /* and bloom the new one in        */
    return TRUE;
}

void picshow_run(void)
{
    char names[PS_MAX][13];
    unsigned seg = 0;
    u8 far *buf;
    int n, cur = 0, tries;

    if (video_is_big()) {
        notice("Picture Show needs 256-colour mode (video=mode13h).");
        return;
    }
    n = collect(names);
    if (n == 0) {
        notice("No .GIF pictures in ASSETS\\ICONS.");
        return;
    }
    if (_dos_allocmem((unsigned)((64000UL + 15UL) / 16UL), &seg) != 0) {
        notice("Not enough memory for the Picture Show.");
        return;
    }
    buf = (u8 far *)MK_FP(seg, 0);
    kb_flush();

    /* First readable picture; give up if the whole gallery is bad. */
    tries = 0;
    while (!show_one(names, cur, n, buf) && ++tries < n)
        cur = (cur + 1) % n;
    if (tries >= n) {
        _dos_freemem(seg);
        notice("No readable .GIF pictures found.");
        return;
    }

    for (;;) {
        int key = kb_poll();
        if (key == KEY_NONE) {
            sys_idle();
            continue;
        }
        if (key == KEY_ESC)
            break;
        if (key == KEY_LEFT)
            cur = (cur + n - 1) % n;
        else if (key == KEY_RIGHT || key == KEY_SPACE || key == KEY_ENTER)
            cur = (cur + 1) % n;
        else
            continue;
        tries = 0;                     /* skip past unreadable neighbours  */
        while (!show_one(names, cur, n, buf) && ++tries < n)
            cur = (cur + 1) % n;
        if (tries >= n)
            break;
    }

    _dos_freemem(seg);
    video_fade_out();                  /* the caller fades the desktop in  */
}
