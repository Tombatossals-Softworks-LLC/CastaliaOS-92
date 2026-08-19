/* ======================================================================
 * music.c - Music Box applet for CASTALIA/386 (PC speaker)
 * ====================================================================== */
#include <conio.h>     /* inp / outp                                     */
#include <i86.h>       /* int86 - BIOS tick                              */
#include "music.h"
#include "video.h"
#include "system.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"

/* ---- note frequencies (Hz); 0 = rest -------------------------------- */
#define R    0
#define C4 262
#define D4 294
#define Ds4 311
#define E4 330
#define F4 349
#define Fs4 370
#define G4 392
#define Gs4 415
#define A4 440
#define B4 494
#define C5 523
#define D5 587
#define Ds5 622
#define E5 659
#define Fs5 740
#define G5 784

typedef struct { unsigned freq; unsigned char dur; } Note;   /* dur in ticks */

/* Beethoven - Ode to Joy */
static const Note ode[] = {
    {E4,4},{E4,4},{F4,4},{G4,4},{G4,4},{F4,4},{E4,4},{D4,4},
    {C4,4},{C4,4},{D4,4},{E4,4},{E4,6},{D4,8}
};
/* Beethoven - Fuer Elise (opening) */
static const Note elise[] = {
    {E5,3},{Ds5,3},{E5,3},{Ds5,3},{E5,3},{B4,3},{D5,3},{C5,3},{A4,6},
    {R,2},{C4,3},{E4,3},{A4,3},{B4,6},{R,2},{E4,3},{Gs4,3},{B4,3},{C5,6}
};
/* Mozart - Eine kleine Nachtmusik (opening) */
static const Note nacht[] = {
    {G4,3},{D4,3},{G4,3},{D4,3},{G4,2},{D4,2},{G4,2},{B4,4},
    {D5,3},{A4,3},{D5,3},{A4,3},{D5,2},{A4,2},{D5,2},{Fs5,4}
};
/* Petzold - Minuet in G */
static const Note minuet[] = {
    {D5,6},{G4,3},{A4,3},{B4,3},{C5,3},{D5,6},{G4,6},{G4,6},
    {E5,6},{C5,3},{D5,3},{E5,3},{Fs5,3},{G5,6},{G4,6},{G4,6}
};

typedef struct { const char *name; const Note *notes; int count; } Tune;
static const Tune g_tunes[] = {
    { "Ode to Joy",   ode,    sizeof(ode)    / sizeof(Note) },
    { "Fuer Elise",   elise,  sizeof(elise)  / sizeof(Note) },
    { "Nachtmusik",   nacht,  sizeof(nacht)  / sizeof(Note) },
    { "Minuet in G",  minuet, sizeof(minuet) / sizeof(Note) }
};
#define NTUNES (int)(sizeof(g_tunes) / sizeof(Tune))

static int           g_play = -1;     /* tune index playing, or -1         */
/* The highlighted row, which is NOT the playing row: you pick a tune with
   the arrows before you start it, and the one that is playing keeps its
   own mark while you look at another. */
static int           g_sel  = 0;
static int           g_note = 0;
static unsigned long g_start = 0;

/* ---- oscilloscope visualiser ---------------------------------------- */
static signed char far m_sin[256];    /* parabolic sine LUT (far), +/-127  */
static int         m_sin_built = 0;
static unsigned    viz_phase   = 0;    /* scrolls the waveform each frame   */
static void build_msin(void)
{
    int i;
    if (m_sin_built) return;
    m_sin_built = 1;
    for (i = 0; i < 256; ++i) {
        long d = (long)i * 360 / 256, num, den, v;
        int neg = 0;
        if (d > 180) { d -= 180; neg = 1; }
        num = 4L * d * (180 - d);
        den = 40500L - d * (180 - d);
        v   = num * 127 / den;
        m_sin[i] = (signed char)(neg ? -v : v);
    }
}
#define MSIN(a) (m_sin[(a) & 255])

static unsigned long ticks(void)
{
    return sys_ticks();               /* fast BDA read - see system.h      */
}

static void tone(unsigned freq)
{
    if (freq == 0) {                       /* rest / off                    */
        outp(0x61, (unsigned char)(inp(0x61) & 0xFC));
        return;
    }
    {
        unsigned div = (unsigned)(1193180UL / (unsigned long)freq);
        outp(0x43, 0xB6);                  /* channel 2, mode 3, lo/hi      */
        outp(0x42, (unsigned char)(div & 0xFF));
        outp(0x42, (unsigned char)((div >> 8) & 0xFF));
        outp(0x61, (unsigned char)(inp(0x61) | 0x03));   /* speaker on      */
    }
}

void music_stop(void)
{
    g_play = -1;
    tone(0);
}

/* A short bright startup arpeggio when the desktop appears (blocking,
   paced off the BIOS tick, ~0.6 s). */
void music_chime(void)
{
    static const unsigned notes[4] = { C5, E5, G5, 1047 /* C6 */ };
    int i;
    unsigned long t;
    for (i = 0; i < 4; ++i) {
        tone(notes[i]);
        t = ticks();
        while (ticks() - t < 2UL) sys_idle();   /* ~2 ticks per note */
    }
    t = ticks();
    while (ticks() - t < 3UL) sys_idle();       /* let the last note ring */
    tone(0);
}

/* ---- Non-blocking sound effects ------------------------------------- */
static bool_t        g_sfx_on    = TRUE;
static unsigned long g_sfx_start = 0;
static unsigned char g_sfx_dur   = 0;     /* 0 = no effect sounding         */

void music_set_sfx(bool_t on)
{
    g_sfx_on = on;
}

void music_sfx(unsigned freq, unsigned char dur_ticks)
{
    if (!g_sfx_on || g_play >= 0)         /* off, or a tune owns the speaker */
        return;
    tone(freq);
    g_sfx_start = ticks();
    g_sfx_dur   = dur_ticks ? dur_ticks : 1;
}

void music_sfx_service(void)
{
    if (g_sfx_dur == 0)
        return;
    if (g_play >= 0) {                    /* a tune started: let it drive     */
        g_sfx_dur = 0;
        return;
    }
    if (ticks() - g_sfx_start >= (unsigned long)g_sfx_dur) {
        tone(0);
        g_sfx_dur = 0;
    }
}

void music_open(void)
{
    music_stop();
    g_sel = 0;
}

bool_t music_is_playing(void)
{
    return (g_play >= 0) ? TRUE : FALSE;
}

static void play(int tune)
{
    g_play  = tune;
    g_note  = 0;
    g_start = ticks();
    tone(g_tunes[tune].notes[0].freq);
}

bool_t music_tick(void)
{
    const Tune *t;
    unsigned long el;
    if (g_play < 0)
        return FALSE;
    t  = &g_tunes[g_play];
    el = ticks() - g_start;
    if (el >= t->notes[g_note].dur) {
        ++g_note;
        if (g_note >= t->count) {          /* tune finished                 */
            music_stop();
            return TRUE;                   /* repaint to un-highlight       */
        }
        g_start = ticks();
        tone(t->notes[g_note].freq);
    } else if (el >= (unsigned long)t->notes[g_note].dur - 1) {
        tone(0);                           /* short gap -> articulation      */
    }
    return FALSE;
}

/* ---- UI -------------------------------------------------------------- */
#define ROW_H (font_h() + 5)

/* A little filled oscilloscope: a sine whose pitch tracks the sounding note
   and whose phase scrolls each frame, so the Music Box "sings" while it
   plays.  Idle (stopped) it settles to a near-flat trace. */
static void draw_scope(const Rect *cl)
{
    /* Every other metric in the shell derives from the font height, and
       this one did not: a fixed 40 pixels is half the window in Mode 13h
       and a strip along the bottom of a 2x window in Mode 12h. */
    int sh = font_h() * 5;
    int sx = cl->x + 6, sw = cl->w - 12;
    int sy = cl->y + cl->h - sh - 4;
    int cy = sy + sh / 2, x;
    int freq = (g_play >= 0) ? (int)g_tunes[g_play].notes[g_note].freq : 0;
    int cyc  = freq ? (freq / 70 + 1) : 1;
    int amp  = freq ? (sh / 2 - 2) : 2;
    if (cyc > 8) cyc = 8;
    vid_fillrect(sx, sy, sw, sh, C_BLACK);
    vid_rect(sx - 1, sy - 1, sw + 2, sh + 2, C_DKGRAY);
    for (x = 0; x < sw; ++x) {
        /* (long): x*cyc*256 overflows a 16-bit int a few pixels in,
           which garbled the whole right side of the trace. */
        int a = (int)(((long)x * cyc * 256) / sw + (long)viz_phase) & 255;
        int h = amp * MSIN(a) / 127;                /* -amp .. amp           */
        if (h < 0) vid_vline(sx + x, cy + h, -h + 1, C_GREEN);
        else       vid_vline(sx + x, cy,      h + 1, C_GREEN);
    }
    viz_phase += 12;
}

void music_draw(const Rect *cl)
{
    int i, y;
    build_msin();
    ui_text_center(cl->x, cl->y + 5, cl->w, "Music Box", C_TITLE);
    y = cl->y + 5 + font_h() + 6;
    for (i = 0; i < NTUNES; ++i) {
        int ry = y + i * ROW_H;
        if (i == g_play) {
            vid_fillrect(cl->x + 4, ry - 1, cl->w - 8, ROW_H, C_TITLE);
            font_draw(cl->x + 10, ry + 1, ">", C_YELLOW);      /* play mark */
            font_draw(cl->x + 22, ry + 1, g_tunes[i].name, C_WHITE);
        } else {
            font_draw(cl->x + 22, ry + 1, g_tunes[i].name, C_BLACK);
        }
        /* The keyboard cursor is an outline, so it can sit on the playing
           row without hiding the fact that it IS the playing row. */
        if (i == g_sel)
            vid_rect(cl->x + 4, ry - 1, cl->w - 8, ROW_H, C_TITLE);
    }
    y += NTUNES * ROW_H + 4;
    ui_text_center(cl->x, y, cl->w,
                   (g_play >= 0) ? "Enter or click again: stop"
                                 : "Arrows and Enter, or click",
                   C_DKGRAY);
    draw_scope(cl);
}

void music_click(const Rect *cl, int mx, int my)
{
    int top = cl->y + 5 + font_h() + 6;
    int i;
    (void)mx;
    if (my < top)
        return;
    i = (my - top) / ROW_H;
    if (i < 0 || i >= NTUNES)
        return;
    g_sel = i;                 /* the cursor follows the mouse             */
    if (i == g_play)
        music_stop();          /* clicking the playing tune stops it        */
    else
        play(i);
}

/* Arrows walk the list, Enter or Space starts the highlighted tune - or
   stops it if it is the one already playing.  The Music Box was one of
   two applets with a click handler and no key handler; the other, the
   Character Map, was fixed in 0.55 and this one was left because nothing
   could open it at all. */
bool_t music_key(int key)
{
    switch (key) {
    case KEY_UP:
        if (g_sel > 0) { --g_sel; return TRUE; }
        break;
    case KEY_DOWN:
        if (g_sel < NTUNES - 1) { ++g_sel; return TRUE; }
        break;
    case KEY_HOME:
        if (g_sel != 0) { g_sel = 0; return TRUE; }
        break;
    case KEY_END:
        if (g_sel != NTUNES - 1) { g_sel = NTUNES - 1; return TRUE; }
        break;
    case KEY_ENTER:
    case KEY_SPACE:
        if (g_sel == g_play) music_stop();
        else                 play(g_sel);
        return TRUE;
    case 's': case 'S':
        if (g_play >= 0) { music_stop(); return TRUE; }
        break;
    default:
        break;
    }
    return FALSE;
}
