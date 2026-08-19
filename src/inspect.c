/* ======================================================================
 * inspect.c - System Inspector applet for CASTALIA/386
 * ----------------------------------------------------------------------
 * The merged System Panel + Inspector: one window that tells a 386/486
 * owner everything about the box.  A sunken readout names the processor,
 * coprocessor, DOS version, video mode and mouse (each hardware fact with
 * its own status LED); a live green oscilloscope sweeps beside an LED
 * time-of-day clock; and animated gauges for conventional, extended/XMS
 * and disk space charge up from empty when the window opens.  Everything
 * is paced off the BIOS 18.2 Hz tick, integer maths only.
 * ====================================================================== */
#include <i86.h>
#include <dos.h>       /* _dos_getdrive, _dos_gettime                     */
#include <stdio.h>
#include <string.h>
#include "inspect.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "system.h"

/* ---- a small parabolic sine LUT for the oscilloscope (far: DGROUP is
   nearly full and this is only read inside the paint) -------------------- */
static signed char far insp_sin[256];
static bool_t insp_sin_ok = FALSE;
static void insp_build_sin(void)
{
    int i;
    if (insp_sin_ok) return;
    insp_sin_ok = TRUE;
    for (i = 0; i < 256; ++i) {
        long dd = (long)i * 360 / 256, num, den, v;
        int neg = 0;
        if (dd > 180) { dd -= 180; neg = 1; }
        num = 4L * dd * (180 - dd);
        den = 40500L - dd * (180 - dd);
        v = num * 127 / den;
        insp_sin[i] = (signed char)(neg ? -v : v);
    }
}
#define ISIN(a) (insp_sin[(a) & 255])

/* ---- probed facts, cached at open (they do not change while open) ----- */
static bool_t        insp_ready = FALSE;
static unsigned long insp_t0 = 0;               /* tick when opened        */
static char          insp_cpu[24];
static bool_t        insp_fpu, insp_mouse, insp_big;
static unsigned      insp_maj, insp_min;
static unsigned      insp_conv, insp_free, insp_ext, insp_drive;
static unsigned long insp_total, insp_dfree, insp_dtot;
static unsigned      insp_equip;                 /* INT 11h equipment word  */

static void dos_ver(unsigned *maj, unsigned *min)
{
    union REGS r;
    r.x.ax = 0x3000;
    int86(0x21, &r, &r);
    *maj = r.h.al;
    *min = r.h.ah;
}

static void insp_probe(void)
{
    const char *nm = system_cpu_name();
    int i;
    for (i = 0; i < (int)sizeof(insp_cpu) - 1 && nm[i]; ++i) insp_cpu[i] = nm[i];
    insp_cpu[i] = '\0';
    insp_fpu   = system_fpu_present();
    insp_mouse = system_has_mouse();
    insp_big   = video_is_big();
    dos_ver(&insp_maj, &insp_min);
    insp_conv  = system_conventional_kb();
    insp_free  = system_free_conv_kb();
    insp_ext   = system_extended_kb();
    insp_total = (unsigned long)insp_conv + (unsigned long)insp_ext;
    system_disk_kb(&insp_dfree, &insp_dtot);
    _dos_getdrive(&insp_drive);
    {
        union REGS r;
        int86(0x11, &r, &r);                     /* BIOS equipment word     */
        insp_equip = r.x.ax;
    }
}

void inspect_open(void)
{
    insp_build_sin();
    insp_probe();
    insp_t0 = sys_ticks();
    insp_ready = TRUE;
}

/* Compact KB -> "<n>M" at/above 10 MB, else "<n>K". */
static void fmt_kb(char *out, unsigned long kb)
{
    if (kb >= 10240UL) sprintf(out, "%luM", kb / 1024UL);
    else               sprintf(out, "%luK", kb);
}

/* A tiny status LED in a black bezel: lit green, or dark. */
static void led(int x, int y, bool_t on)
{
    vid_fillrect(x, y, 5, 5, on ? C_GREEN : C_DKGRAY);
    vid_pixel(x + 1, y + 1, on ? C_WHITE : C_SHADOW);
    vid_rect(x, y, 5, 5, C_BLACK);
}

/* An animated gauge row: label, a sunken LED bar filled to
   charge*used/full with a bright leading edge, and a right-aligned value. */
static void gauge(int x, int y, int w, const char *label,
                  unsigned long used, unsigned long full,
                  const char *val, u8 fill, int charge)
{
    int labelw = 8 * font_adv();
    int valw   = font_text_width(val);
    int bx = x + labelw;
    int bh = font_h();
    int bw = w - labelw - valw - 6;

    font_draw(x, y, label, C_BLACK);
    if (bw > 10) {
        ui_sink(bx, y - 1, bw, bh + 2);
        vid_fillrect(bx + 2, y + 1, bw - 4, bh - 2, C_DKGRAY);   /* trough */
        if (full > 0) {
            long fw = (long)(bw - 4) * (long)used / (long)full;
            fw = fw * charge / 256;                              /* charge */
            if (fw < 0)          fw = 0;
            if (fw > bw - 4)     fw = bw - 4;
            if (fw > 0) {
                vid_fillrect(bx + 2, y + 1, (int)fw, bh - 2, fill);
                vid_vline(bx + 2 + (int)fw - 1, y + 1, bh - 2, C_WHITE);
            }
        }
    }
    font_draw(x + w - valw, y, val, C_BLACK);
}

void inspect_draw(const Rect *cl)
{
    char buf[48], vbuf[24];
    int  cx = cl->x, cy = cl->y, cw = cl->w;
    int  pad = 4, lh = font_h() + 3;
    int  x0 = cx + pad, y0 = cy + pad, innerw = cw - 2 * pad;
    int  leftw, rightw, panelh, scoph;
    int  charge, phase;
    unsigned long elapsed;
    struct dostime_t tod;

    insp_build_sin();
    if (!insp_ready) inspect_open();
    _dos_gettime(&tod);

    elapsed = sys_ticks() - insp_t0;
    charge  = (elapsed * 42UL > 256UL) ? 256 : (int)(elapsed * 42UL); /* ~6 tk */
    phase   = (int)((elapsed * 6UL) & 255UL);

    /* (long): innerw * 58 passes 32767 at innerw >= 565, which a
       maximized Inspector in Mode 12h reaches (client 634 -> innerw 626,
       626*58 = 36308 wraps to -29228, leftw = -292).  The left panel then
       had a negative width and vanished, and the scope was drawn 288 px
       off the left edge at 914 px wide on a 640 px screen. */
    leftw  = (int)((long)innerw * 58 / 100);
    rightw = innerw - leftw - pad;
    panelh = 5 * lh + 5;
    scoph  = panelh - lh - 3;

    /* ---- LEFT: hardware readout on a white sunken panel ---------------- */
    vid_fillrect(x0, y0, leftw, panelh, C_WHITE);
    ui_sink(x0, y0, leftw, panelh);
    {
        int ix = x0 + 4, iy = y0 + 3, lw = 5 * font_adv();
        int ledx = x0 + leftw - 9;
        font_draw(ix, iy, "CPU", C_DKGRAY);
        font_draw(ix + lw, iy, insp_cpu, C_TITLE);                    iy += lh;
        font_draw(ix, iy, "FPU", C_DKGRAY);
        font_draw(ix + lw, iy, insp_fpu ? "80x87 ok" : "none", C_BLACK);
        led(ledx, iy + 1, insp_fpu);                                 iy += lh;
        sprintf(buf, "%u.%02u", insp_maj, insp_min);
        font_draw(ix, iy, "DOS", C_DKGRAY);
        font_draw(ix + lw, iy, buf, C_BLACK);                        iy += lh;
        font_draw(ix, iy, "VGA", C_DKGRAY);
        font_draw(ix + lw, iy, insp_big ? "640x480x16" : "320x200x256", C_BLACK);
        iy += lh;
        font_draw(ix, iy, "MOU", C_DKGRAY);
        font_draw(ix + lw, iy, insp_mouse ? "INT 33h ok" : "not found", C_BLACK);
        led(ledx, iy + 1, insp_mouse);
    }

    /* ---- RIGHT: live oscilloscope + LED clock ------------------------- */
    {
        int sx = x0 + leftw + pad, sy = y0;
        int midy = sy + scoph / 2, amp = scoph / 2 - 3, i;
        vid_fillrect(sx, sy, rightw, scoph, C_BLACK);
        ui_sink(sx, sy, rightw, scoph);
        vid_hline(sx + 2, midy, rightw - 4, C_DKGRAY);              /* base */
        for (i = 3; i < rightw - 3; ++i) {
            int a  = i + phase;
            int v  = (ISIN(a * 3) + ISIN(a * 6) / 2) * amp / 190;
            int yy = midy - v;
            if (yy < sy + 2)            yy = sy + 2;
            if (yy > sy + scoph - 3)    yy = sy + scoph - 3;
            if (yy <= midy) vid_vline(sx + i, yy, midy - yy + 1, C_GREEN);
            else            vid_vline(sx + i, midy, yy - midy + 1, C_GREEN);
            vid_pixel(sx + i, yy, C_WHITE);
        }
        {
            int ly = sy + scoph + 2;
            vid_fillrect(sx, ly, rightw, lh, C_BLACK);
            ui_sink(sx, ly, rightw, lh);
            sprintf(buf, "%02u:%02u:%02u", tod.hour, tod.minute, tod.second);
            ui_text_center(sx, ly + (lh - font_h()) / 2, rightw, buf, C_GREEN);
        }
    }

    /* ---- animated memory / disk gauges -------------------------------- */
    {
        int gy = y0 + panelh + pad;
        char d2[16];
        sprintf(vbuf, "%uK", insp_free);
        gauge(x0, gy, innerw, "Conv",
              (unsigned long)(insp_conv - insp_free),
              (unsigned long)insp_conv, vbuf, C_BLUE, charge);       gy += lh + 2;
        sprintf(vbuf, "%uK", insp_ext);
        gauge(x0, gy, innerw, "XMS", (unsigned long)insp_ext,
              insp_total ? insp_total : 1UL, vbuf, C_GREEN, charge); gy += lh + 2;
        fmt_kb(vbuf, insp_dfree);
        fmt_kb(d2, insp_dtot);
        strcat(vbuf, "/");
        strcat(vbuf, d2);
        sprintf(buf, "Disk %c:", (char)('A' + (insp_drive ? insp_drive - 1 : 0)));
        gauge(x0, gy, innerw, buf, insp_dtot - insp_dfree,
              insp_dtot ? insp_dtot : 1UL, vbuf, C_TITLE, charge);   gy += lh + 5;

        /* ---- equipment panel: what the BIOS reports is bolted on ------- */
        {
            int com = (insp_equip >> 9) & 7;
            int lpt = (insp_equip >> 14) & 3;
            int fdd = (insp_equip & 1) ? ((insp_equip >> 6) & 3) + 1 : 0;
            bool_t game = ((insp_equip >> 12) & 1) ? TRUE : FALSE;
            int ph = 2 * lh + 6, ey;
            vid_fillrect(x0, gy, innerw, ph, C_WHITE);
            ui_sink(x0, gy, innerw, ph);
            ey = gy + 3;
            sprintf(buf, "COM %d   LPT %d   FDD %d   GAME %s",
                    com, lpt, fdd, game ? "yes" : "no");
            font_draw(x0 + 4, ey, buf, C_BLACK);
            ey += lh;
            sprintf(buf, "Total RAM %lu KB", insp_total);
            font_draw(x0 + 4, ey, buf, C_TITLE);
            {
                bool_t blink = ((elapsed >> 2) & 1UL) ? TRUE : FALSE;
                int dotx = x0 + innerw - font_text_width("LIVE") - 10;
                vid_fillrect(dotx, ey + 1, 5, 5, blink ? C_RED : C_DKGRAY);
                vid_rect(dotx, ey + 1, 5, 5, C_BLACK);
                font_draw(dotx + 8, ey, "LIVE", blink ? C_RED : C_DKGRAY);
            }
        }
    }
}
