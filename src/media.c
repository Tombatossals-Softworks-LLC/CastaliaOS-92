/* ======================================================================
 * media.c - The Gramophone: WAV / MIDI player for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include <conio.h>     /* inp / outp / kbhit / getch                     */
#include <dos.h>       /* _dos_allocmem / MK_FP                          */
#include "media.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "system.h"
#include "keyboard.h"
#include "sblaster.h"
#include "opl.h"
#include "lptdac.h"

/* ---- shared far scratch (carved for WAV samples OR MIDI raw+chords) --- */
#define BUF_BYTES   40000U
#define WAV_MAX     16000          /* 8-bit samples for scope + playback   */
#define MID_RAW     22000          /* raw SMF bytes parsed from memory     */
#define NOTE_MAX    600            /* note-on events collected while parsing*/
#define CHORD_MAX   OPL_MELODIC    /* simultaneous melodic notes per chord */
#define CHORDS_MAX  500            /* distinct chords in a song            */
#define EV_DRUM     0xFF           /* prog sentinel: a channel-10 drum note */
#define PLAY_RATE   4000           /* speaker PWM sample rate (Hz)         */

static unsigned char far *g_buf = (unsigned char far *)0;
static unsigned char far *g_samp;      /* = g_buf (WAV)                    */
static unsigned char far *g_raw;       /* = g_buf (MIDI file image)        */

/* A chord: the set of notes struck together, and how long they hold.  The
   Gramophone plays them polyphonically on the OPL FM chip when one is
   present, or the top note alone on the PC speaker when it is not. */
typedef struct {
    unsigned char n;                   /* melodic notes in this chord        */
    unsigned char note[CHORD_MAX];     /* MIDI notes, highest first          */
    unsigned char prog[CHORD_MAX];     /* GM program per note (its voice)    */
    unsigned char drums;               /* OPL_BD/SD/TT/TC/HH struck this beat */
    unsigned      dur;                 /* BIOS ticks the chord holds         */
} Chord;
static Chord far *g_chords;            /* = g_buf + MID_RAW                  */

static int  g_kind = 0;                /* 0 none, 1 WAV, 2 MIDI            */
static char g_name[28] = "";

/* WAV facts. */
static unsigned      g_rate, g_ch, g_bits;
static unsigned long g_bytes;
static unsigned      g_secs;
static int           g_nsamp;
static unsigned      g_play_rate;      /* rate the samples are actually played*/

/* MIDI facts. */
static int      g_nchords;
/* Cached playing time (BIOS ticks): the whole song, and a running prefix
   sum for chords [0, g_dur_idx).  See mid_secs_upto(). */
static long     g_dur_total = 0;
static long     g_dur_upto  = 0;
static int      g_dur_idx   = 0;
static unsigned g_div;
static unsigned long g_uspq;           /* microseconds per quarter note    */

/* Playback (MIDI is async; WAV is a bounded blocking burst). */
static bool_t        g_play   = FALSE;
static bool_t        g_paused = FALSE;   /* MIDI held at g_cur, not reset    */
static int           g_cur  = 0;
static unsigned long g_note_start = 0;
static unsigned      g_phase = 0;

/* Smooth-animation state: the tick the gravity step last ran, the LCD
   marquee's crawl position, and whether the loaded title is long enough
   to crawl at all (set by draw_marquee, read by media_tick). */
static unsigned long g_anim_last = 0;
static unsigned      g_mq        = 0;
static bool_t        g_mq_moves  = FALSE;

/* The spectrum analyzer: one bouncing bar per column with a slow-falling
   peak cap, the way a certain 1997 MP3 player drew its music.  The bars are
   excited by the notes of the chord playing now and fall under gravity. */
#define BARS_MAX 40
static u8  g_bar[BARS_MAX];              /* live bar heights 0..99           */
static u8  g_peak[BARS_MAX];             /* peak-hold caps 0..99             */
static int g_nbars = 0;                  /* bars that fit the current well   */
static int g_roll_lo = 60, g_roll_hi = 72;   /* MIDI pitch range of the song */

/* Transport is a Winamp-style button row; the rects are filled while the
   panel paints and hit-tested on the next click. */
#define T_COUNT 7
static Rect   g_btn[T_COUNT];  /* prev,play,pause,stop,next,eject,repeat     */
static Rect   g_seek;          /* the position slider                        */
static bool_t g_repeat = FALSE;/* loop the song when it ends                 */
static bool_t g_want_open = FALSE;  /* Eject was clicked; main opens a file  */
#define T_PREV   0
#define T_PLAY   1
#define T_PAUSE  2
#define T_STOP   3
#define T_NEXT   4
#define T_EJECT  5
#define T_REPEAT 6

/* ---- playlist ---------------------------------------------------------
 * A folder's worth of .WAV/.MID tracks (8.3 names in far memory), the
 * current selection, a scroll offset, and the hit-test rectangles for the
 * list rows and the [+ ADD FOLDER] button. */
#define PL_MAX 48
static char far g_pl_name[PL_MAX][13];   /* 8.3 filenames  (far: DGROUP full)*/
static char far g_pl_dir[80];            /* the scanned directory (far)     */
static int      g_pl_count = 0;
static int      g_pl_cur   = -1;         /* loaded track, -1 = none         */
static int      g_pl_top   = 0;          /* first visible row (scroll)      */
static int      g_pl_rows  = 0;          /* rows the list well can show      */
static bool_t   g_want_folder = FALSE;   /* [+ ADD] clicked; main pops dlg  */
static Rect     g_pl_list;               /* the list well (row hit-testing) */
static Rect     g_pl_add;                /* the [+ ADD FOLDER] button        */

/* Forward decls: media_tick / media_click reach these before they appear. */
static void   media_start(void);
static void   pl_play(int i);
static bool_t pl_advance(void);
static bool_t pl_load(int i);

/* ---- speaker ---------------------------------------------------------- */
static void mtone(unsigned freq)
{
    if (freq == 0) {
        outp(0x61, (unsigned char)(inp(0x61) & 0xFC));
        return;
    }
    {
        unsigned div = (unsigned)(1193180UL / (unsigned long)freq);
        outp(0x43, 0xB6);
        outp(0x42, (unsigned char)(div & 0xFF));
        outp(0x42, (unsigned char)((div >> 8) & 0xFF));
        outp(0x61, (unsigned char)(inp(0x61) | 0x03));
    }
}

/* Equal-tempered MIDI note -> Hz (octave 5 = notes 60..71). */
static const unsigned MBASE[12] =
    { 262,277,294,311,330,349,370,392,415,440,466,494 };
static unsigned midifreq(int n)
{
    int oct;
    unsigned f;
    if (n < 0)   n = 0;
    if (n > 127) n = 127;
    oct = n / 12 - 5;
    f   = MBASE[n % 12];
    if (oct > 0)      f <<= oct;
    else if (oct < 0) f >>= (-oct);
    return f;
}

/* A General-MIDI percussion note (channel 10) -> the OPL rhythm voice(s)
   it should strike. */
static int drum_bits(int note)
{
    switch (note) {
        case 35: case 36:                     return OPL_BD;  /* bass drums   */
        case 38: case 40:                     return OPL_SD;  /* snares       */
        case 42: case 44: case 46:            return OPL_HH;  /* hi-hats      */
        case 49: case 51: case 52:
        case 55: case 57:                     return OPL_TC;  /* cymbals      */
        default:                              return OPL_TT;  /* toms & rest  */
    }
}

/* ---- one-shot buffer allocation -------------------------------------- */
static bool_t ensure_buf(void)
{
    unsigned seg;
    if (g_buf != (unsigned char far *)0)
        return TRUE;
    if (_dos_allocmem((unsigned)((BUF_BYTES + 15U) / 16U), &seg) != 0)
        return FALSE;
    g_buf   = (unsigned char far *)MK_FP(seg, 0);
    g_samp  = g_buf;
    g_raw    = g_buf;
    g_chords = (Chord far *)(g_buf + MID_RAW);
    return TRUE;
}

/* ---- little-endian readers over the far raw image -------------------- */
static unsigned rd16(const unsigned char far *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static unsigned long rd32(const unsigned char far *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}
static unsigned be16(const unsigned char far *p)
{
    return ((unsigned)p[0] << 8) | (unsigned)p[1];
}
static unsigned long be32(const unsigned char far *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

/* ---- WAV loader ------------------------------------------------------- */
static bool_t load_wav(const char *path)
{
    FILE *f;
    unsigned char hdr[12], ck[8];
    unsigned long clen;
    bool_t have_fmt = FALSE;

    g_rate = 8000; g_ch = 1; g_bits = 8; g_bytes = 0; g_nsamp = 0;

    f = fopen(path, "rb");
    if (f == NULL)
        return FALSE;
    if (fread(hdr, 1, 12, f) != 12 ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(f);
        return FALSE;
    }
    /* Walk chunks: read the fmt fields, then stream the data chunk into a
       downsampled 8-bit mono buffer at PLAY_RATE. */
    while (fread(ck, 1, 8, f) == 8) {
        clen = (unsigned long)rd32(ck + 4);
        if (memcmp(ck, "fmt ", 4) == 0) {
            unsigned char fb[16];
            unsigned long srate;
            /* A fmt chunk shorter than 16 bytes leaves fb[14..15] unwritten,
               and g_bits then came off uninitialised stack - which decided
               bytesps and steered the whole decode.  Zero it first. */
            unsigned n = (clen < 16) ? (unsigned)clen : 16;
            memset(fb, 0, sizeof(fb));
            if (fread(fb, 1, n, f) != n) break;
            g_ch   = rd16(fb + 2);
            /* The WAV sample rate is a 32-bit field and g_rate is 16-bit:
               96 kHz used to truncate to 30464 and 192 kHz to 60928, taking
               the step, the play rate, the duration and the readout with
               them.  Range-check before narrowing. */
            srate  = rd32(fb + 4);
            g_rate = (srate >= 1UL && srate <= 65535UL) ? (unsigned)srate : 0;
            g_bits = rd16(fb + 14);
            if (g_ch < 1)   g_ch = 1;
            if (g_rate < 1) g_rate = 8000;
            have_fmt = TRUE;
            if (clen > n) fseek(f, (long)(clen - n), SEEK_CUR);
            if (clen & 1) fseek(f, 1L, SEEK_CUR);    /* RIFF word padding   */
        } else if (memcmp(ck, "data", 4) == 0 && have_fmt) {
            /* Frame stride is bits/8 per channel, rounded up - not
               "2 if 16-bit else 1".  A 24-bit stereo frame is 6 bytes but
               that yielded 4, so the duration came out 1.5x long and both
               the intra-frame skip and the inter-frame seek under-shot,
               walking progressively further out of step into noise. */
            unsigned bytesps = ((g_bits + 7) / 8) * g_ch;
            unsigned need    = (g_bits >= 16) ? 2 : 1;  /* first channel only */
            unsigned long total = clen / (bytesps ? bytesps : 1);
            unsigned step;
            unsigned long i;
            unsigned char fr[8];
            g_bytes = clen;
            g_secs  = (unsigned)(total / (g_rate ? g_rate : 1));
            /* A Sound Blaster or a Covox plays near the WAV's native rate
               (best quality), downsampling only enough to fit the buffer;
               a Sound Source is capped at its ~7 kHz FIFO drain; the PC
               speaker keeps its fixed ~4 kHz PWM rate. */
            if (sb_present() || lptdac_present()) {
                unsigned cap = sb_present() ? 0 : lptdac_max_rate();
                step = (unsigned)((total + WAV_MAX - 1) / WAV_MAX);
                if (step < 1) step = 1;
                if (cap != 0 && g_rate / step > cap) {
                    /* 32-bit: g_rate may be the full 16-bit range, so
                       g_rate + cap - 1 wrapped and gave s2 = 0 - the
                       guard then never fired and a Sound Source was
                       driven at ~10x its FIFO drain rate. */
                    unsigned s2 = (unsigned)
                        (((unsigned long)g_rate + cap - 1) / cap);
                    if (s2 > step) step = s2;
                }
                g_play_rate = g_rate / step;
                if (g_play_rate < 1) g_play_rate = g_rate;
            } else {
                step = g_rate / PLAY_RATE;
                if (step < 1) step = 1;
                g_play_rate = PLAY_RATE;
            }
            for (i = 0; i < total && g_nsamp < WAV_MAX; i += step) {
                int v;
                /* (long)(step-1) FIRST: both operands are 16-bit, so the
                   product used to wrap before the widening cast and a wide
                   frame (6-channel 16-bit is 12 bytes) seeked to the wrong
                   place, decoding the tail of the file as noise. */
                if (i > 0)
                    fseek(f, (long)(step - 1) * (long)bytesps, SEEK_CUR);
                /* Read only the first channel's sample (1 or 2 bytes) - a
                   frame can be far wider than fr[] (e.g. 6-channel 16-bit is
                   12 bytes), so the rest of the frame is skipped, not read. */
                if (fread(fr, 1, need, f) != need)
                    break;
                if (bytesps > need)
                    fseek(f, (long)(bytesps - need), SEEK_CUR);
                if (g_bits >= 16) {
                    int s = (int)(short)rd16(fr);   /* left channel */
                    v = (s >> 8) + 128;
                } else {
                    v = fr[0];                       /* already 0..255 */
                }
                if (v < 0)   v = 0;
                if (v > 255) v = 255;
                g_samp[g_nsamp++] = (unsigned char)v;
            }
            break;
        } else {
            fseek(f, (long)clen, SEEK_CUR);          /* skip other chunks   */
            if (clen & 1) fseek(f, 1L, SEEK_CUR);    /* RIFF word padding   */
        }
    }
    fclose(f);
    return (g_nsamp > 0) ? TRUE : FALSE;
}

/* ---- MIDI loader (SMF -> a stream of chords with per-note instruments) - */
/* Far: 3.6 KB of parse scratch has no business in the near segment
   (the arrays are only ever indexed, never passed as near pointers). */
static unsigned long far g_ev_tick[NOTE_MAX];  /* scratch during parse     */
static unsigned char far g_ev_note[NOTE_MAX];
static unsigned char far g_ev_prog[NOTE_MAX]; /* GM program per note-on    */
static int           g_nev;

static void add_event(unsigned long tick, int note, int prog)
{
    /* Insertion sort by tick (stable), keeping EVERY note-on - notes struck
       at the same tick become one chord later, so the music is polyphonic.
       The program travels with its note so each voice gets its instrument. */
    int i = g_nev;
    if (g_nev >= NOTE_MAX)
        return;
    while (i > 0 && g_ev_tick[i - 1] > tick) {
        g_ev_tick[i] = g_ev_tick[i - 1];
        g_ev_note[i] = g_ev_note[i - 1];
        g_ev_prog[i] = g_ev_prog[i - 1];
        --i;
    }
    g_ev_tick[i] = tick;
    g_ev_note[i] = (unsigned char)note;
    g_ev_prog[i] = (unsigned char)prog;
    ++g_nev;
}

static bool_t load_mid(const char *path)
{
    unsigned len, pos;
    unsigned ntrk, ti;
    unsigned char chan_prog[16];       /* live GM program per MIDI channel   */
    int ci;

    g_nev = 0; g_nchords = 0; g_div = 480; g_uspq = 500000UL;
    for (ci = 0; ci < 16; ++ci) chan_prog[ci] = 0;   /* default: piano       */

    /* Read into the FAR buffer via _dos_read (fread's buffer is near in the
       medium model, but g_raw lives in its own _dos_allocmem segment). */
    {
        int fh;
        unsigned nread = 0;
        if (_dos_open(path, 0, &fh) != 0)      /* 0 = read only            */
            return FALSE;
        _dos_read(fh, g_raw, MID_RAW, &nread);
        _dos_close(fh);
        len = nread;
    }
    if (len < 14 || _fmemcmp(g_raw, "MThd", 4) != 0)
        return FALSE;

    ntrk = be16(g_raw + 10);
    g_div = be16(g_raw + 12);
    if (g_div == 0 || (g_div & 0x8000)) g_div = 480;   /* ignore SMPTE      */
    /* be32 is 32-bit; the old cast kept only the low 16 bits, so an MThd
       length of 0xFFFFFFF0 gave pos = 65528, pos + 8 wrapped to 0, the
       guard below passed and the parser read 25 KB past the 40000-byte
       block.  Range-check in 32-bit before narrowing. */
    {
        unsigned long hlen = be32(g_raw + 4);
        if (hlen > (unsigned long)len || hlen + 8UL > (unsigned long)len) {
            g_nchords = 0;
            return FALSE;
        }
        pos = (unsigned)(8UL + hlen);                 /* past the header    */
    }

    for (ti = 0; ti < ntrk && pos + 8 <= len; ++ti) {
        /* Same trap the MThd length above was fixed for, still open here:
           narrowing tlen first let a crafted MTrk length wrap `end` down
           BELOW len, so the "end > len" clamp could not see it.  Clamp in
           32-bit, then narrow. */
        unsigned long tlen = be32(g_raw + pos + 4);
        unsigned long tend = (unsigned long)pos + 8UL + tlen;
        unsigned end = (tend > (unsigned long)len) ? len : (unsigned)tend;
        unsigned p = pos + 8;
        unsigned long abstick = 0;
        unsigned char status = 0;
        while (p < end) {
            unsigned long dt = 0;
            unsigned char b;
            /* variable-length delta time */
            do { b = g_raw[p++]; dt = (dt << 7) | (b & 0x7F); }
            while ((b & 0x80) && p < end);
            abstick += dt;
            if (p >= end) break;
            b = g_raw[p];
            if (b & 0x80) { status = b; ++p; }         /* running status     */
            if (p >= end) break;    /* every arm below reads at least once */
            if (status == 0xFF) {                      /* meta               */
                unsigned char type = g_raw[p++];
                unsigned long mlen = 0;
                /* The guard above only proved p <= end-1, and the type
                   byte just consumed it: without this the length loop
                   reads g_raw[end] before any re-check.  The SysEx arm
                   below is already safe - it consumes nothing first. */
                if (p >= end) break;
                do { b = g_raw[p++]; mlen = (mlen << 7) | (b & 0x7F); }
                while ((b & 0x80) && p < end);
                if (type == 0x51 && mlen == 3 && p + 3 <= end)
                    g_uspq = ((unsigned long)g_raw[p] << 16) |
                             ((unsigned long)g_raw[p + 1] << 8) |
                             (unsigned long)g_raw[p + 2];
                /* A length past the track end (or one crafted to wrap the
                   16-bit offset) would loop the parser forever: stop. */
                if (p >= end || mlen > (unsigned long)(end - p)) {
                    p = end; break;
                }
                p += (unsigned)mlen;
            } else if (status == 0xF0 || status == 0xF7) {
                unsigned long slen = 0;
                do { b = g_raw[p++]; slen = (slen << 7) | (b & 0x7F); }
                while ((b & 0x80) && p < end);
                if (p >= end || slen > (unsigned long)(end - p)) {
                    p = end; break;
                }
                p += (unsigned)slen;
            } else {
                unsigned char hi = status & 0xF0;
                unsigned char ch = status & 0x0F;
                unsigned char d1, d2 = 0;
                /* A truncated final event used to read one or two bytes
                   past the track (and, when end == len, past what was
                   actually read from disk). */
                if (hi != 0xC0 && hi != 0xD0) {
                    if (p + 2 > end) break;
                    d1 = g_raw[p++];
                    d2 = g_raw[p++];
                } else {
                    d1 = g_raw[p++];
                }
                if (hi == 0xC0)                        /* program change     */
                    chan_prog[ch] = d1;
                else if (hi == 0x90 && d2 > 0)         /* note on            */
                    add_event(abstick, d1,
                              (ch == 9) ? EV_DRUM       /* ch10 = percussion  */
                                        : (int)chan_prog[ch]);
            }
        }
        pos = end;
    }

    /* Group the sorted note-ons into chords: every note-on at the same tick
       is one chord, and it holds until the next distinct tick. */
    {
        int i = 0;
        unsigned long tpb = (g_div ? g_div : 480);
        while (i < g_nev && g_nchords < CHORDS_MAX) {
            unsigned long t = g_ev_tick[i];
            unsigned long span, us, bt;
            Chord c;
            c.n = 0; c.drums = 0;
            while (i < g_nev && g_ev_tick[i] == t) {   /* collect the chord   */
                int note = g_ev_note[i], prog = g_ev_prog[i], j, dup = 0;
                ++i;
                if (prog == EV_DRUM) {                  /* percussion         */
                    c.drums |= (unsigned char)drum_bits(note);
                    continue;
                }
                for (j = 0; j < c.n; ++j)
                    if (c.note[j] == note) { dup = 1; break; }
                if (!dup && c.n < CHORD_MAX) {          /* insert, high first */
                    j = c.n;
                    while (j > 0 && c.note[j - 1] < note) {
                        c.note[j] = c.note[j - 1];
                        c.prog[j] = c.prog[j - 1];      /* keep prog paired   */
                        --j;
                    }
                    c.note[j] = (unsigned char)note;
                    c.prog[j] = (unsigned char)prog;
                    ++c.n;
                }
            }
            span = (i < g_nev) ? (g_ev_tick[i] - t) : tpb;
            if (span == 0) span = 1;
            us = span * (g_uspq / tpb);                /* microseconds       */
            bt = us / 54925UL;                         /* -> ~18.2 Hz ticks  */
            if (bt < 1)  bt = 1;
            if (bt > 60) bt = 60;
            c.dur = (unsigned)bt;
            if (c.n > 0 || c.drums)
                g_chords[g_nchords++] = c;             /* far struct copy     */
        }
    }

    /* The pitch range across the whole song, so the analyzer and piano roll
       spread its notes across their full height. */
    {
        int i, lo = 127, hi = 0;
        for (i = 0; i < g_nchords; ++i) {
            int cn = (int)g_chords[i].n, j;
            for (j = 0; j < cn; ++j) {
                int m = (int)g_chords[i].note[j];
                if (m < lo) lo = m;
                if (m > hi) hi = m;
            }
        }
        if (hi <= lo) hi = lo + 1;
        g_roll_lo = lo; g_roll_hi = hi;
    }
    /* Total playing time is fixed for the life of the song; the readout
       used to re-derive it from all 500 chords on every repaint. */
    {
        int i;
        g_dur_total = 0;
        for (i = 0; i < g_nchords; ++i)
            g_dur_total += (long)g_chords[i].dur;
        g_dur_upto = 0;
        g_dur_idx  = 0;
    }
    return (g_nchords > 0) ? TRUE : FALSE;
}

/* ---- public: open ---------------------------------------------------- */
static bool_t ext_is(const char *p, const char *e)
{
    int n = (int)strlen(p), k = (int)strlen(e), i;
    if (n < k) return FALSE;
    for (i = 0; i < k; ++i) {
        char a = p[n - k + i], b = e[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return FALSE;
    }
    return TRUE;
}

bool_t media_open_file(const char *path)
{
    const char *base;
    int i;

    media_stop();
    if (!ensure_buf())
        return FALSE;

    if (ext_is(path, ".MID") || ext_is(path, ".MIDI")) {
        if (!load_mid(path)) { g_kind = 0; return FALSE; }
        g_kind = 2;
    } else {
        if (!load_wav(path)) { g_kind = 0; return FALSE; }
        g_kind = 1;
    }

    /* Short display name (basename). */
    base = path;
    for (i = 0; path[i]; ++i)
        if (path[i] == '\\' || path[i] == '/' || path[i] == ':')
            base = path + i + 1;
    for (i = 0; base[i] && i < (int)sizeof(g_name) - 1; ++i)
        g_name[i] = base[i];
    g_name[i] = '\0';
    g_phase = 0;
    return TRUE;
}

/* ---- playback -------------------------------------------------------- */
static unsigned pit_count(void)
{
    unsigned lo, hi;
    outp(0x43, 0x00);                  /* latch channel 0                  */
    lo = inp(0x40);
    hi = inp(0x40);
    return (hi << 8) | lo;
}
static void pit_wait(unsigned period)
{
    unsigned start = pit_count(), now, el;
    do {
        now = pit_count();
        el  = (unsigned)((start - now) & 0xFFFF);     /* ch0 counts down    */
    } while (el < period);
}

/* WAV: a bounded, key-interruptible PWM burst.  Channel 2 in mode 0 makes
   each sample a pulse whose width the speaker cone integrates; the sample
   period is paced by channel 0 so the pitch is CPU-speed independent. */
static void wav_play(void)
{
    unsigned char p61;
    unsigned period;
    int i;

    /* Device ladder: a Sound Blaster plays the samples as real digital
       audio through its DAC (single-cycle 8-bit DMA); failing that, a
       declared parallel-port DAC (Covox / Disney Sound Source) gets the
       stream; the bare PC speaker's pulse-width burst is the floor that
       always works. */
    if (sb_present()) {
        if (sb_play_8bit(g_samp, (unsigned)g_nsamp, g_play_rate))
            return;
    }
    if (lptdac_play(g_samp, g_nsamp, g_play_rate))
        return;

    p61    = inp(0x61);
    period = (unsigned)(1193180UL / (g_play_rate ? g_play_rate : PLAY_RATE));
    outp(0x43, 0x90);                  /* ch2, LSB only, mode 0, binary     */
    outp(0x61, (unsigned char)(p61 | 0x03));
    for (i = 0; i < g_nsamp; ++i) {
        outp(0x42, g_samp[i]);
        pit_wait(period);
        if (kbhit()) { getch(); break; }
    }
    outp(0x61, (unsigned char)(p61 & 0xFC));   /* restore speaker off       */
}

/* Sound chord `idx`: polyphonically on the OPL FM chip, or its top note
   alone on the PC speaker when no FM chip is present. */
static void play_chord(int idx)
{
    Chord c = g_chords[idx];           /* far -> local copy                  */
    if (opl_present()) {
        int v;
        for (v = 0; v < (int)c.n; ++v) {
            opl_program(v, (int)c.prog[v]);   /* voice's instrument first     */
            opl_note_on(v, c.note[v]);
        }
        if (c.drums)
            opl_drums((int)c.drums);          /* strike the percussion        */
    } else if (c.n > 0) {
        mtone(midifreq(c.note[0]));
    }
}

/* Release whatever is sounding. */
static void hush(void)
{
    if (opl_present()) opl_silence();
    else               mtone(0);
}

/* ---- the spectrum analyzer's state ----------------------------------- */
static void bars_reset(void)
{
    int i;
    for (i = 0; i < BARS_MAX; ++i) { g_bar[i] = 0; g_peak[i] = 0; }
}

/* A cheap 16-bit-safe hash, so bars struck by different notes leap to
   different heights and the analyzer looks busy rather than uniform. */
static unsigned bhash(unsigned v)
{
    v = (unsigned)(v * 2141u + 9871u);
    v ^= (unsigned)(v >> 5);
    return v;
}

/* One ~18 Hz animation step: gravity.  The bars fall fast, the peak caps
   drift down slowly and never sink below their bar.  TRUE while anything
   moved, so the player repaints only while the picture is changing - once
   everything is at rest (after a stop, or between quiet passages) the
   window costs nothing again. */
static bool_t bars_decay(void)
{
    bool_t moved = FALSE;
    int i;
    for (i = 0; i < g_nbars; ++i) {
        if (g_bar[i] > 0) {
            g_bar[i] = (g_bar[i] > 6) ? (u8)(g_bar[i] - 6) : 0;
            moved = TRUE;
        }
        if (g_peak[i] > 0) {
            g_peak[i] = (g_peak[i] > 2) ? (u8)(g_peak[i] - 2) : 0;
            if (g_peak[i] < g_bar[i]) g_peak[i] = g_bar[i];
            moved = TRUE;
        }
    }
    return moved;
}

/* A chord just struck: its notes leap their columns up (a cheap hash
   varies the heights so the analyzer looks busy rather than uniform),
   a drum hit thumps the low bins, and the peak caps ride up with the
   bars.  Gravity is bars_decay's job, one step per tick. */
static void bars_excite(void)
{
    Chord c;
    int lo, hi, j, span;
    if (!g_play || g_nbars < 1)
        return;
    c  = g_chords[g_cur];                      /* far -> local copy          */
    lo = g_roll_lo; hi = g_roll_hi;
    if (hi <= lo) hi = lo + 1;
    span = hi - lo + 1;
    for (j = 0; j < (int)c.n; ++j) {
        int bin = ((int)c.note[j] - lo) * g_nbars / span;
        unsigned h;
        if (bin < 0) bin = 0;
        if (bin >= g_nbars) bin = g_nbars - 1;
        h = 55u + (bhash((unsigned)(c.note[j] * 7 + g_phase)) % 45u);
        if ((u8)h > g_bar[bin]) g_bar[bin] = (u8)h;
    }
    if (c.drums) {                              /* a drum hit thumps the lows */
        int b;
        for (b = 0; b < 3 && b < g_nbars; ++b)
            if (g_bar[b] < 82) g_bar[b] = 82;
    }
    for (j = 0; j < g_nbars; ++j)
        if (g_peak[j] < g_bar[j]) g_peak[j] = g_bar[j];
}

void media_stop(void)
{
    if (g_kind == 2) hush();
    g_play   = FALSE;
    g_paused = FALSE;
    g_cur    = 0;
    /* No bars_reset() here: the analyzer's bars collapse gracefully under
       bars_decay's gravity instead of vanishing on the spot. */
}

bool_t media_is_playing(void) { return g_play; }

/* Start a MIDI from the top (WAV plays as a blocking burst and returns). */
static void media_start(void)
{
    if (g_kind == 1) {
        wav_play();                    /* blocking burst; returns when done  */
        g_play   = FALSE;
        g_paused = FALSE;
    } else if (g_kind == 2 && g_nchords > 0) {
        if (opl_present()) opl_init(); /* program the FM voices              */
        g_play   = TRUE;
        g_paused = FALSE;
        g_cur    = 0;
        g_note_start = sys_ticks();
        bars_reset();
        play_chord(0);
        bars_excite();
    }
}

/* Pause / resume (MIDI only - a WAV burst is atomic). */
static void media_pause(void)
{
    if (g_kind != 2)
        return;
    if (g_play) {                      /* playing -> hold at g_cur           */
        g_play   = FALSE;
        g_paused = TRUE;
        hush();
    } else if (g_paused) {             /* held -> pick up where we left off  */
        g_play   = TRUE;
        g_paused = FALSE;
        g_note_start = sys_ticks();
        play_chord(g_cur);
    }
}

/* Jump to chord `pos` (MIDI); WAV just replays from the top. */
static void media_seek(int pos)
{
    if (g_kind != 2 || g_nchords < 1) {
        if (g_kind == 1) media_start();
        return;
    }
    if (pos < 0) pos = 0;
    if (pos >= g_nchords) pos = g_nchords - 1;
    hush();
    g_cur = pos;
    g_note_start = sys_ticks();
    if (g_play) { play_chord(g_cur); bars_excite(); }
}

bool_t media_tick(bool_t fg)
{
    unsigned long now = sys_ticks();
    bool_t moved = FALSE;

    /* The ~18 Hz cosmetic step, run only while the player is the focused
       window (same frugality rule as the Music Box's scope): gravity pulls
       the analyzer's bars and caps down between chords - and to rest after
       a stop - and a long title crawls across the LCD while the music
       plays.  A repaint is requested only when a pixel actually moved. */
    if (fg && g_kind == 2 && now != g_anim_last) {
        g_anim_last = now;
        if (bars_decay())
            moved = TRUE;
        if (g_play && g_mq_moves && (now & 3) == 0) {
            ++g_mq;                    /* one character every 4 ticks        */
            moved = TRUE;
        }
    }

    if (!g_play || g_kind != 2)
        return moved;
    if (now - g_note_start < (unsigned long)g_chords[g_cur].dur)
        return moved;
    hush();                            /* release the sounding chord         */
    ++g_cur;
    g_note_start = now;
    ++g_phase;
    if (g_cur >= g_nchords) {
        if (g_repeat) {                /* loop back to the top and keep going */
            g_cur = 0;
            play_chord(0);
            bars_excite();
            return TRUE;
        }
        if (pl_advance())              /* a playlist rolls on to the next     */
            return TRUE;
        g_play = FALSE;                /* the bars settle under bars_decay    */
        g_cur  = 0;
        return TRUE;
    }
    play_chord(g_cur);
    bars_excite();
    return TRUE;
}

/* Eject: main.c polls this once per loop and, when set, pops the "Play..."
   dialog so a new file can be loaded without leaving the player. */
bool_t media_poll_open(void)
{
    bool_t w = g_want_open;
    g_want_open = FALSE;
    return w;
}

/* ---- playlist implementation ----------------------------------------- */

/* Copy far 8.3 name i into a near buffer (out holds >= 13 bytes). */
static void pl_name_near(int i, char *out)
{
    int k;
    for (k = 0; k < 12 && g_pl_name[i][k]; ++k) out[k] = g_pl_name[i][k];
    out[k] = '\0';
}

/* Copy the far directory string into a near buffer (out holds >= 80). */
static void pl_dir_near(char *out)
{
    int k;
    for (k = 0; k < 79 && g_pl_dir[k]; ++k) out[k] = g_pl_dir[k];
    out[k] = '\0';
}

/* Build "dir\name" (or just "name") for track i into out (>= 100 bytes). */
static void pl_fullpath(int i, char *out)
{
    char nm[14], dir[80];
    pl_name_near(i, nm);
    pl_dir_near(dir);
    if (dir[0]) sprintf(out, "%s\\%s", dir, nm);
    else        strcpy(out, nm);
}

/* Keep the current row inside the visible list window. */
static void pl_reveal(void)
{
    if (g_pl_rows < 1) return;
    if (g_pl_cur < g_pl_top)              g_pl_top = g_pl_cur;
    if (g_pl_cur >= g_pl_top + g_pl_rows) g_pl_top = g_pl_cur - g_pl_rows + 1;
    if (g_pl_top < 0)                     g_pl_top = 0;
}

/* Load track i into the player (does not start playback). */
/* TRUE only when the track really loaded.  This used to swallow a
   failure whole: the old row stayed highlighted, g_play was never
   cleared, the PLAY button stayed drawn pressed and media_is_playing()
   kept answering TRUE for a track that was not playing at all. */
static bool_t pl_load(int i)
{
    char path[100];
    if (i < 0 || i >= g_pl_count) return FALSE;
    pl_fullpath(i, path);
    if (!media_open_file(path)) {
        media_stop();                  /* nothing is playing - say so     */
        g_pl_cur = -1;
        return FALSE;
    }
    g_pl_cur = i;
    pl_reveal();
    return TRUE;
}

/* Load track i and start it. */
static void pl_play(int i)
{
    if (pl_load(i))
        media_start();
}

/* Auto-advance to the next track when one ends.  A MIDI starts itself; a
   WAV is a blocking burst, so it is only loaded (the desktop never freezes
   on an auto-advance) and waits for Play.  TRUE if it moved. */
static bool_t pl_advance(void)
{
    if (g_pl_count <= 0 || g_pl_cur < 0 || g_pl_cur + 1 >= g_pl_count)
        return FALSE;
    if (!pl_load(g_pl_cur + 1))
        return FALSE;                  /* a bad track ends the run        */
    if (g_kind == 2)
        media_start();
    return TRUE;
}

/* Scan g_pl_dir for one extension pattern, appending the matches. */
static void pl_scan(const char *pat)
{
    struct find_t ff;
    unsigned rc;
    char spec[100], dir[80];
    pl_dir_near(dir);
    if (dir[0]) sprintf(spec, "%s\\%s", dir, pat);
    else        strcpy(spec, pat);
    rc = _dos_findfirst(spec, _A_RDONLY | _A_ARCH, &ff);
    while (rc == 0) {
        if ((ff.attrib & (_A_SUBDIR | _A_VOLID)) == 0 && g_pl_count < PL_MAX) {
            int k;
            for (k = 0; k < 12 && ff.name[k]; ++k)
                g_pl_name[g_pl_count][k] = ff.name[k];
            g_pl_name[g_pl_count][k] = '\0';
            ++g_pl_count;
        }
        rc = _dos_findnext(&ff);
    }
}

void media_add_folder(const char *dir)
{
    int n = 0;
    while (dir[n] && n < (int)sizeof(g_pl_dir) - 1) { g_pl_dir[n] = dir[n]; ++n; }
    g_pl_dir[n] = '\0';
    while (n > 0 && (g_pl_dir[n - 1] == '\\' || g_pl_dir[n - 1] == '/'))
        g_pl_dir[--n] = '\0';              /* drop any trailing separator     */
    g_pl_count = 0;
    g_pl_top   = 0;
    g_pl_cur   = -1;
    pl_scan("*.WAV");
    pl_scan("*.MID");
    if (g_pl_count > 0)
        pl_load(0);                        /* show the first track, ready     */
}

bool_t media_poll_folder(void)
{
    bool_t w = g_want_folder;
    g_want_folder = FALSE;
    return w;
}

/* ---- drawing --------------------------------------------------------- */
static void draw_scope(const Rect *v)
{
    int i, mid = v->y + v->h / 2;
    int prev = mid;
    vid_fillrect(v->x, v->y, v->w, v->h, C_BLACK);
    ui_sink(v->x, v->y, v->w, v->h);
    vid_hline(v->x + 1, mid, v->w - 2, C_DKGRAY);
    if (g_nsamp <= 0)
        return;
    /* Walk the waveform with a DDA instead of a 32-bit multiply AND a
       32-bit divide per column, and wrap g_phase by comparison instead of
       a 16-bit modulo.  amp is the other loop invariant that used to be
       recomputed per column. */
    if (v->w - 2 < 2)
        return;                              /* nothing to plot, no divide  */
    {
        int      cols = v->w - 2;
        unsigned wrap = (unsigned)g_nsamp;
        unsigned pos  = g_phase % wrap;      /* once, not once per column   */
        long     step = ((long)(g_nsamp - 1) << 8) / cols;
        long     acc  = step;                /* i = 1 is the first column   */
        int      amp  = v->h - 4;
        int      ytop = v->y + 1, ybot = v->y + v->h - 2;
        for (i = 1; i < cols; ++i) {
            unsigned k = pos + (unsigned)(acc >> 8);
            int s, y, a, b;
            if (k >= wrap) k -= wrap;        /* one add can wrap only once  */
            s = (int)g_samp[k];
            y = mid - ((s - 128) * amp) / 256;
            a = prev < y ? prev : y;
            b = prev < y ? y : prev;
            if (a < ytop) a = ytop;
            if (b > ybot) b = ybot;
            /* A column is a solid vertical run: one clipped call, not one
               per pixel (each of which re-ran four bound tests and the
               mode dispatch). */
            if (b >= a)
                vid_vline(v->x + 1 + i, a, b - a + 1, C_GREEN);
            prev = y;
            acc += step;
        }
    }
}

/* The spectrum analyzer well: bars from a green floor up through yellow to
   red, each topped with a slow-falling white peak cap - the signature look
   of a mid-90s software player, redrawn for the OPL. */
static void draw_analyzer(const Rect *v)
{
    int ix = v->x + 2, iy = v->y + 1, iw = v->w - 4, ih = v->h - 2;
    int i, bw, colw, redT, ylT;
    vid_fillrect(v->x, v->y, v->w, v->h, C_BLACK);
    ui_sink(v->x, v->y, v->w, v->h);
    if (ih < 2 || iw < 2)
        return;
    g_nbars = iw / 4;                  /* ~4px per bar (3 wide + 1 gap)      */
    if (g_nbars < 1) g_nbars = 1;
    if (g_nbars > BARS_MAX) g_nbars = BARS_MAX;
    bw = iw / g_nbars; if (bw < 2) bw = 2;
    colw = bw - 1;     if (colw < 1) colw = 1;
    redT = ih * 2 / 3;                 /* colour-zone thresholds (in pixels) */
    ylT  = ih * 2 / 5;
    for (i = 0; i < g_nbars; ++i) {
        int bx   = ix + i * bw;
        int hpix = (int)g_bar[i]  * ih / 100;
        int ppix = (int)g_peak[i] * ih / 100;
        int base = iy + ih;
        /* The colour changes at exactly two thresholds, so a bar is three
           solid blocks - not one clipped vid_hline per scan line (40 bars
           of ~38px was ~1500 calls a frame). */
        if (hpix > 0) {
            int ng = hpix < ylT + 1 ? hpix : ylT + 1;         /* green      */
            int ny = hpix < redT + 1 ? hpix : redT + 1;       /* +yellow    */
            vid_fillrect(bx, base - ng, colw, ng, C_GREEN);
            if (ny > ng)
                vid_fillrect(bx, base - ny, colw, ny - ng, C_YELLOW);
            if (hpix > ny)
                vid_fillrect(bx, base - hpix, colw, hpix - ny, C_RED);
        }
        if (ppix > 0) {                /* the floating peak cap              */
            int prow = iy + ih - 1 - ppix;
            if (prow < iy) prow = iy;
            vid_hline(bx, prow, colw, C_WHITE);
        }
    }
}

/* ---- Winamp-style furniture ------------------------------------------ */

static void fmt_time(unsigned secs, char *buf)
{
    unsigned m = secs / 60, s = secs % 60;
    if (m > 99) m = 99;
    sprintf(buf, "%u:%02u", m, s);
}

/* Seconds of playing time in chords [0, idx).  This ran twice per repaint
   as two O(g_nchords) walks over far structs - up to 1000 far loads and
   1000 32-bit adds, 18 times a second, to redraw a clock that changes at
   most once a second.  The playhead only ever walks forward, so keep a
   running prefix sum and rebuild it in full only on a backward seek. */
static unsigned mid_secs_upto(int idx)
{
    int i;
    if (idx > g_nchords) idx = g_nchords;
    if (idx < 0) idx = 0;
    if (idx < g_dur_idx) { g_dur_upto = 0; g_dur_idx = 0; }
    for (i = g_dur_idx; i < idx; ++i)
        g_dur_upto += (long)g_chords[i].dur;
    g_dur_idx = idx;
    return (unsigned)((g_dur_upto * 5L) / 91L); /* BIOS ticks -> seconds     */
}

/* Filled triangles for the transport glyphs. */
static void tri_right(int lx, int cy, int s, u8 col)
{
    int dy;
    for (dy = -s; dy <= s; ++dy) {
        int a = (dy < 0) ? -dy : dy;
        vid_hline(lx, cy + dy, s - a + 1, col);
    }
}
static void tri_left(int rx, int cy, int s, u8 col)
{
    int dy;
    for (dy = -s; dy <= s; ++dy) {
        int a = (dy < 0) ? -dy : dy, w = s - a + 1;
        vid_hline(rx - w + 1, cy + dy, w, col);
    }
}

/* Draw transport glyph `which` centred in button rect r. */
static void draw_glyph(int which, const Rect *r, u8 col)
{
    int cx = r->x + r->w / 2, cy = r->y + r->h / 2;
    int s  = r->h / 2 - 3;
    int bw;
    if (s < 2) s = 2;
    bw = (s >= 3) ? 2 : 1;
    switch (which) {
    case T_PREV:
        vid_fillrect(cx - s - 1, cy - s, bw, 2 * s + 1, col);
        tri_left(cx + s, cy, s, col);
        break;
    case T_PLAY:
        tri_right(cx - s + 1, cy, s, col);
        break;
    case T_PAUSE:
        vid_fillrect(cx - s, cy - s, bw, 2 * s + 1, col);
        vid_fillrect(cx + s - bw + 1, cy - s, bw, 2 * s + 1, col);
        break;
    case T_STOP:
        vid_fillrect(cx - s, cy - s, 2 * s, 2 * s, col);
        break;
    case T_NEXT:
        tri_right(cx - s, cy, s, col);
        vid_fillrect(cx + s + 1, cy - s, bw, 2 * s + 1, col);
        break;
    case T_EJECT: {                    /* an up-triangle over a base bar     */
        int k;
        for (k = 0; k < s; ++k)
            vid_hline(cx - k, cy - s + k, 2 * k + 1, col);
        vid_fillrect(cx - (s - 1), cy + 1, 2 * s - 1, (s >= 3) ? 2 : 1, col);
        break;
    }
    case T_REPEAT: {                   /* a loop of two arrows               */
        int top = cy - s + 1, bot = cy + s - 1;
        vid_hline(cx - s + 1, top, 2 * s - 2, col);
        vid_hline(cx - s + 1, bot, 2 * s - 2, col);
        vid_vline(cx + s - 1, top, bot - top + 1, col);
        vid_vline(cx - s + 1, top, bot - top + 1, col);
        vid_pixel(cx + s - 2, top - 1, col);   /* right-arrow barbs (top)    */
        vid_pixel(cx + s - 2, top + 1, col);
        vid_pixel(cx - s + 2, bot - 1, col);   /* left-arrow barbs (bottom)  */
        vid_pixel(cx - s + 2, bot + 1, col);
        break;
    }
    default: break;
    }
}

static void draw_tbtn(int idx, bool_t pressed)
{
    Rect *r = &g_btn[idx];
    int   o = pressed ? 1 : 0;
    Rect  g;
    ui_fill_face(r->x, r->y, r->w, r->h);
    if (pressed) ui_sink(r->x, r->y, r->w, r->h);
    else         ui_raise(r->x, r->y, r->w, r->h);
    g.x = r->x + o; g.y = r->y + o; g.w = r->w; g.h = r->h;
    draw_glyph(idx, &g, C_BLACK);
}

/* Scroll the track title across a fixed pixel width; short names sit still. */
static void draw_marquee(int x, int y, int wpx, const char *s, u8 col)
{
    int  fa  = font_adv();
    int  vis = (fa > 0) ? wpx / fa : 0;
    int  len = (int)strlen(s);
    char buf[64];
    if (vis < 1)
        return;
    if (vis > (int)sizeof(buf) - 1) vis = (int)sizeof(buf) - 1;
    if (len <= vis) { g_mq_moves = FALSE; font_draw(x, y, s, col); return; }
    g_mq_moves = TRUE;                          /* media_tick crawls g_mq     */
    {
        int total = len + 4, i;                 /* four-space loop gap        */
        int start = (int)(g_mq % (unsigned)total);
        for (i = 0; i < vis; ++i) {
            int k = (start + i) % total;
            buf[i] = (k < len) ? s[k] : ' ';
        }
        buf[i] = '\0';
        font_draw(x, y, buf, col);
    }
}

/* The position slider: a sunken groove, a blue elapsed fill and a raised
   thumb the mouse can drop anywhere to seek (MIDI). */
static void draw_seek(int x, int y, int w, int h)
{
    int fillw, thumbx;
    rect_set(&g_seek, x, y, w, h);
    vid_fillrect(x, y, w, h, C_FACE);
    ui_sink(x, y, w, h);
    if (g_kind == 2 && g_nchords > 0)  /* long: cur*width tops 16 bits     */
        fillw = (int)((long)g_cur * (w - 4) / g_nchords);
    else if (g_kind == 1)
        fillw = w - 4;
    else
        fillw = 0;
    if (fillw > 0)
        vid_fillrect(x + 2, y + 2, fillw, h - 4, C_BLUE);
    thumbx = x + 2 + fillw - 1;
    if (thumbx < x + 1)     thumbx = x + 1;
    if (thumbx > x + w - 4) thumbx = x + w - 4;
    vid_fillrect(thumbx, y, 3, h, C_FACE);
    ui_raise(thumbx, y, 3, h);
}

/* Paint the whole player: an LCD title/time strip, a big visualiser well,
   a device/detail line, the transport row and the seek slider - a compact
   386-native take on the classic skinned MP3 player. */
void media_draw(const Rect *cl)
{
    int pad = 3, fh = font_h(), fa = font_adv();
    int x0 = cl->x + pad, w0 = cl->w - 2 * pad;
    int top = cl->y + pad, bottom = cl->y + cl->h - pad;
    int lcdh = fh + 6, infoh = fh + 2, btnh = fh + 6;
    int seekh = (fh >= 12) ? 8 : 6;
    int btn_y  = bottom - seekh - 2 - btnh;
    int info_y = btn_y - 2 - infoh;
    int seek_y = bottom - seekh;
    int viz_y  = top + lcdh + 2;
    int viz_h  = fh * 2 + 8;            /* the visualiser is compact now      */
    int pl_y, pl_h;
    int iny, i, timew, tagw, btnw, gap, mqx, mqw;
    char clock[16], line[40];
    const char *disp, *tag, *dev;
    Rect viz;

    if (viz_h < 16) viz_h = 16;
    pl_y = viz_y + viz_h + 2;
    pl_h = (info_y - 2) - pl_y;         /* the playlist fills the rest        */
    if (pl_h < fh + 6) pl_h = fh + 6;

    /* 1. LCD strip: the time, a scrolling title, and the format tag. */
    vid_fillrect(x0, top, w0, lcdh, C_BLACK);
    ui_sink(x0, top, w0, lcdh);
    iny = top + (lcdh - fh) / 2;
    if (g_kind == 2) {                 /* MIDI shows elapsed / total time    */
        char a[8], b2[8];
        fmt_time(mid_secs_upto(g_cur), a);
        fmt_time((unsigned)((g_dur_total * 5L) / 91L), b2);
        sprintf(clock, "%s/%s", a, b2);
    } else if (g_kind == 1) {
        fmt_time(g_secs, clock);
    } else {
        strcpy(clock, "0:00");
    }
    font_draw(x0 + 3, iny, clock, C_GREEN);
    timew = font_text_width(clock) + 5;
    tag   = (g_kind == 2) ? "MID" : (g_kind == 1) ? "WAV" : "---";
    tagw  = fa * 3 + 3;
    font_draw(x0 + w0 - tagw, iny, tag, C_CYAN);
    disp  = (g_kind == 0) ? "no media loaded" : g_name;
    mqx   = x0 + 3 + timew;
    mqw   = (x0 + w0 - tagw - 3) - mqx;
    if (mqw > 0)
        draw_marquee(mqx, iny, mqw, disp, C_GREEN);

    /* 2. The visualiser: an oscilloscope for WAV, the analyzer for MIDI. */
    rect_set(&viz, x0, viz_y, w0, viz_h);
    if (g_kind == 1) draw_scope(&viz);
    else             draw_analyzer(&viz);

    /* 2b. The playlist: a header with the [+ FOLDER] button and a scrolling
       list of the folder's tracks, the playing one lit blue. */
    {
        int headerh = fh + 4;
        int list_y  = pl_y + headerh;
        int list_h  = pl_h - headerh;
        int rh = fh + 1, r, addw;
        char hdr[24];
        if (list_h < rh) list_h = rh;
        addw = font_text_width("+ FOLDER") + 10;
        sprintf(hdr, "PLAYLIST %d", g_pl_count);
        font_draw(x0, pl_y + 2, hdr, C_TITLE);
        rect_set(&g_pl_add, x0 + w0 - addw, pl_y, addw, headerh - 1);
        ui_button(&g_pl_add, "+ FOLDER", FALSE);
        rect_set(&g_pl_list, x0, list_y, w0, list_h);
        vid_fillrect(x0, list_y, w0, list_h, C_WHITE);
        ui_sink(x0, list_y, w0, list_h);
        g_pl_rows = (list_h - 2) / rh;
        if (g_pl_rows < 1) g_pl_rows = 1;
        if (g_pl_count == 0) {
            font_draw(x0 + 4, list_y + 3, "(empty - + FOLDER to add)", C_DKGRAY);
        } else {
            for (r = 0; r < g_pl_rows; ++r) {
                int t = g_pl_top + r, ry = list_y + 2 + r * rh;
                char nm[14];
                if (t >= g_pl_count) break;
                pl_name_near(t, nm);
                if (t == g_pl_cur) {
                    vid_fillrect(x0 + 1, ry, w0 - 2, rh, C_BLUE);
                    font_draw(x0 + 4, ry, nm, C_WHITE);
                } else {
                    font_draw(x0 + 4, ry, nm, C_BLACK);
                }
            }
        }
    }

    /* 3. Device on the left, quality detail on the right. */
    dev = (g_kind == 1) ? (sb_present()     ? sb_name()
                         : lptdac_present() ? lptdac_name()
                                            : "PC speaker")
        : (g_kind == 2) ? (opl_present() ? "OPL FM synth" : "PC speaker")
        : "Insert a .WAV or .MID";
    font_draw(x0, info_y, dev, C_GREEN);
    line[0] = '\0';
    if (g_kind == 1)
        sprintf(line, "%uHz %u-bit", g_rate, g_bits);
    else if (g_kind == 2)
        sprintf(line, "%lu BPM", 60000000UL / (g_uspq ? g_uspq : 500000UL));
    if (line[0] != '\0')
        font_draw(x0 + w0 - font_text_width(line), info_y, line, C_DKGRAY);

    /* 4. Transport: prev, play, pause, stop, next, eject, repeat. */
    btnw = fh + 6; if (btnw < 12) btnw = 12;
    gap  = 2;
    for (i = 0; i < T_COUNT; ++i)
        rect_set(&g_btn[i], x0 + i * (btnw + gap), btn_y, btnw, btnh);
    draw_tbtn(T_PREV,   FALSE);
    draw_tbtn(T_PLAY,   g_play   ? TRUE : FALSE);
    draw_tbtn(T_PAUSE,  g_paused ? TRUE : FALSE);
    draw_tbtn(T_STOP,   FALSE);
    draw_tbtn(T_NEXT,   FALSE);
    draw_tbtn(T_EJECT,  FALSE);
    draw_tbtn(T_REPEAT, g_repeat ? TRUE : FALSE);

    /* 5. The seek slider. */
    draw_seek(x0, seek_y, w0, seekh);
}

bool_t media_click(const Rect *cl, int mx, int my)
{
    int i, jump;
    bool_t haspl = (g_pl_count > 0 && g_pl_cur >= 0) ? TRUE : FALSE;
    (void)cl;

    /* The Add-Folder button works even with nothing loaded yet. */
    if (rect_contains(&g_pl_add, mx, my)) {
        g_want_folder = TRUE;
        return TRUE;
    }
    /* Click a playlist row to play that track. */
    if (g_pl_count > 0 && rect_contains(&g_pl_list, mx, my)) {
        int rh  = font_h() + 1;
        int row = g_pl_top + (my - (g_pl_list.y + 2)) / rh;
        if (row >= 0 && row < g_pl_count)
            pl_play(row);
        return TRUE;
    }

    /* Eject is how you LOAD something, so it has to work when nothing is
       loaded - the guard below used to sit in front of it, leaving an
       empty Gramophone with a drawn button that did nothing. */
    if (g_kind == 0) {
        if (rect_contains(&g_btn[T_EJECT], mx, my)) {
            g_want_open = TRUE;
            return TRUE;
        }
        return FALSE;
    }

    for (i = 0; i < T_COUNT; ++i) {
        if (!rect_contains(&g_btn[i], mx, my))
            continue;
        jump = g_nchords / 10; if (jump < 1) jump = 1;
        switch (i) {
        case T_PREV:   if (haspl)            pl_play(g_pl_cur > 0 ? g_pl_cur - 1 : 0);
                       else if (g_kind == 2) media_seek(g_cur - jump);
                       else                  media_start();
                       break;
        case T_PLAY:   if (g_paused) media_pause();  /* resume held MIDI     */
                       else          media_start();
                       break;
        case T_PAUSE:  media_pause(); break;
        case T_STOP:   media_stop();  break;
        case T_NEXT:   if (haspl)            pl_play(g_pl_cur + 1 < g_pl_count
                                                     ? g_pl_cur + 1 : g_pl_cur);
                       else if (g_kind == 2) media_seek(g_cur + jump);
                       else                  media_start();
                       break;
        case T_EJECT:  g_want_open = TRUE;      break;   /* main pops dialog  */
        case T_REPEAT: g_repeat = g_repeat ? FALSE : TRUE; break;
        default: break;
        }
        return TRUE;
    }

    /* Click anywhere on the groove to seek (MIDI). */
    if (g_kind == 2 && g_nchords > 0 && rect_contains(&g_seek, mx, my)) {
        int rel   = mx - (g_seek.x + 2);
        int inner = g_seek.w - 4; if (inner < 1) inner = 1;
        if (rel < 0) rel = 0;
        media_seek((int)((long)rel * g_nchords / inner));
        return TRUE;
    }
    return FALSE;
}

bool_t media_key(int key)
{
    int jump;

    /* These three come BEFORE the nothing-loaded guard, and E most of
       all: Eject is how you LOAD something.  With no file open the
       handler used to return immediately, so from the keyboard the
       Gramophone could not be given anything to play - the same shape as
       the Eject BUTTON having once sat behind that guard, which left an
       empty player with a drawn button that did nothing.
       Both set a flag the main loop turns into a file picker, exactly as
       the clicks do; the work does not happen here. */
    if (key == 'e' || key == 'E') { g_want_open   = TRUE; return TRUE; }
    if (key == 'f' || key == 'F') { g_want_folder = TRUE; return TRUE; }

    if (g_kind == 0)
        return FALSE;

    if (key == 's' || key == 'S') { media_stop(); return TRUE; }

    /* Previous / next TRACK, which only the transport buttons could
       reach.  PgUp/PgDn move through a list of records elsewhere in the
       shell (the Cardfile's deck), so they move through this one. */
    if ((key == KEY_PGUP || key == KEY_PGDN) &&
        g_pl_count > 0 && g_pl_cur >= 0) {
        int t = (key == KEY_PGDN) ? g_pl_cur + 1 : g_pl_cur - 1;
        if (t < 0)            t = 0;
        if (t >= g_pl_count)  t = g_pl_count - 1;
        pl_play(t);
        return TRUE;
    }

    if (key == KEY_SPACE || key == KEY_ENTER) {
        if (g_play || g_paused) media_pause();   /* toggle (MIDI)            */
        else                    media_start();   /* start (or WAV burst)     */
        return TRUE;
    }
    if (g_kind == 2 && (key == KEY_LEFT || key == KEY_RIGHT)) {
        jump = g_nchords / 20; if (jump < 1) jump = 1;
        media_seek(g_cur + (key == KEY_RIGHT ? jump : -jump));
        return TRUE;
    }
    return FALSE;
}
