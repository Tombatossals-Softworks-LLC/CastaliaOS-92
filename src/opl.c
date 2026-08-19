/* ======================================================================
 * opl.c - OPL2 (YM3812 / AdLib) FM synthesis for CASTALIA/386
 * ----------------------------------------------------------------------
 * See opl.h.  All access is through the AdLib register window at ports
 * 388h (address / status) and 389h (data), which every Sound Blaster
 * with an FM chip decodes.  A tiny post-write delay lets the chip latch
 * on real hardware; it is harmless in an emulator.
 * ====================================================================== */
#include <conio.h>     /* inp / outp                                       */
#include "opl.h"

#define OPL_ADDR 0x388
#define OPL_DATA 0x389

static int g_state = -1;       /* -1 unknown, 0 absent, 1 present           */

/* Modulator operator offset for each of the 9 channels; the carrier is
   this + 3.  (The OPL register map is not linear across channels.) */
static const unsigned char g_op[OPL_VOICES] = {
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12
};

/* F-numbers for the twelve semitones of one octave (the classic AdLib
   table); the block field then shifts by octave. */
static const unsigned g_fnum[12] = {
    0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA,
    0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287
};

/* Track what each voice is currently sounding, for key-off. */
static unsigned char g_vblock[OPL_VOICES];
static unsigned      g_vfnum[OPL_VOICES];
static int           g_vpatch[OPL_VOICES];     /* current patch per voice    */

/* A General-MIDI instrument bank for the OPL2.  Eleven register bytes per
   patch: modulator {0x20,0x40,0x60,0x80,0xE0}, carrier {the same}, then the
   channel's {0xC0} feedback/connection.  There is a distinct voice for each
   of the sixteen GM families (so a piano no longer sounds like a flute), plus
   a couple of dedicated favourites (electric piano, synth bass).  Every
   carrier is near full volume so all voices are audible; sustaining families
   set the EG-type bit (0x20) in their {0x20} bytes, plucked/percussive ones
   leave it clear so the note decays. */
#define OPL_NPATCH 18
static const unsigned char INSTR[OPL_NPATCH][11] = {
    /*  0 PIANO      */ { 0x01,0x4B,0xF1,0x53,0x00, 0x01,0x00,0xF2,0x63,0x00, 0x08 },
    /*  1 CHROM.PERC */ { 0x07,0x12,0xF6,0xF2,0x01, 0x02,0x00,0xF5,0xF3,0x01, 0x06 },
    /*  2 ORGAN      */ { 0x21,0x1A,0xF0,0x33,0x00, 0x21,0x07,0xF0,0x33,0x00, 0x0A },
    /*  3 GUITAR     */ { 0x01,0x11,0xF5,0x74,0x00, 0x01,0x00,0xF6,0x74,0x00, 0x08 },
    /*  4 BASS       */ { 0x01,0x0C,0xF1,0x52,0x00, 0x01,0x00,0xF4,0x72,0x00, 0x08 },
    /*  5 STRINGS    */ { 0x21,0x1E,0x62,0x14,0x00, 0x21,0x00,0x53,0x14,0x00, 0x0A },
    /*  6 ENSEMBLE   */ { 0xA1,0x1E,0x51,0x14,0x00, 0x21,0x00,0x41,0x14,0x00, 0x0A },
    /*  7 BRASS      */ { 0x21,0x0C,0x71,0x21,0x00, 0x21,0x00,0x61,0x21,0x00, 0x0C },
    /*  8 REED       */ { 0x31,0x1C,0x51,0x33,0x00, 0x21,0x00,0x52,0x24,0x00, 0x0A },
    /*  9 PIPE       */ { 0x21,0x1B,0x91,0x17,0x00, 0x21,0x00,0x81,0x17,0x00, 0x0C },
    /* 10 SYNTH LEAD */ { 0x21,0x10,0xF1,0x11,0x00, 0x21,0x00,0xF1,0x11,0x00, 0x0E },
    /* 11 SYNTH PAD  */ { 0x21,0x1E,0x31,0x18,0x00, 0x21,0x00,0x21,0x18,0x00, 0x0A },
    /* 12 SYNTH FX   */ { 0x61,0x1D,0x51,0x16,0x02, 0x21,0x00,0x41,0x16,0x02, 0x0A },
    /* 13 ETHNIC     */ { 0x01,0x11,0xF6,0x84,0x00, 0x01,0x00,0xF7,0x94,0x00, 0x08 },
    /* 14 PERCUSSIVE */ { 0x07,0x00,0xF7,0xF7,0x00, 0x01,0x00,0xF7,0xF8,0x00, 0x06 },
    /* 15 SOUND FX   */ { 0x0F,0x00,0xF0,0xFF,0x03, 0x00,0x00,0xF0,0xFF,0x03, 0x00 },
    /* 16 E.PIANO    */ { 0x01,0x26,0xF2,0x43,0x01, 0x01,0x00,0xF2,0x53,0x01, 0x08 },
    /* 17 SYNTH BASS */ { 0x21,0x0C,0xF2,0x42,0x00, 0x21,0x00,0xF3,0x42,0x00, 0x08 }
};

/* Map a General-MIDI program (0..127) to a bank patch.  Most programs pick
   the patch for their sixteen-strong family; a few popular voices get their
   own dedicated timbre. */
static int gm_patch(int gm)
{
    gm &= 0x7F;
    if (gm == 4 || gm == 5)   return 16;   /* Rhodes / FM electric piano   */
    if (gm == 38 || gm == 39) return 17;   /* synth bass                   */
    return gm / 8;                          /* otherwise, one per family    */
}

static void io_delay(void)
{
    int i;
    for (i = 0; i < 6; ++i) (void)inp(OPL_ADDR);
}

static void opl_write(int reg, int val)
{
    outp(OPL_ADDR, (unsigned char)reg);
    io_delay();                        /* address settle (~3.3 us on OPL2)   */
    outp(OPL_DATA, (unsigned char)val);
    io_delay();
    io_delay();                        /* data settle (~23 us on OPL2)       */
}

bool_t opl_present(void)
{
    int s1, s2, i;
    if (g_state >= 0)
        return g_state ? TRUE : FALSE;

    opl_write(0x04, 0x60);             /* mask both timers                   */
    opl_write(0x04, 0x80);             /* reset the timer-expired flags      */
    s1 = inp(OPL_ADDR) & 0xE0;
    opl_write(0x02, 0xFF);             /* timer 1 to -1 (expires at once)    */
    opl_write(0x04, 0x21);             /* start timer 1                      */
    for (i = 0; i < 200; ++i) io_delay();          /* wait ~100 us          */
    s2 = inp(OPL_ADDR) & 0xE0;
    opl_write(0x04, 0x60);
    opl_write(0x04, 0x80);
    g_state = (s1 == 0x00 && s2 == 0xC0) ? 1 : 0;
    return g_state ? TRUE : FALSE;
}

/* Write patch `p`'s eleven registers into voice `v`'s two operators. */
static void load_patch(int v, int p)
{
    const unsigned char *ins = INSTR[p];
    unsigned char m = g_op[v], c = (unsigned char)(g_op[v] + 3);
    opl_write(0x20 + m, ins[0]); opl_write(0x40 + m, ins[1]);
    opl_write(0x60 + m, ins[2]); opl_write(0x80 + m, ins[3]);
    opl_write(0xE0 + m, ins[4]);
    opl_write(0x20 + c, ins[5]); opl_write(0x40 + c, ins[6]);
    opl_write(0x60 + c, ins[7]); opl_write(0x80 + c, ins[8]);
    opl_write(0xE0 + c, ins[9]);
    opl_write(0xC0 + v, ins[10]);
}

void opl_program(int voice, int gmprog)
{
    int p;
    if (!opl_present() || voice < 0 || voice >= OPL_MELODIC)
        return;
    p = gm_patch(gmprog);
    if (p != g_vpatch[voice]) {
        load_patch(voice, p);
        g_vpatch[voice] = p;
    }
}

/* Program the five percussion voices that rhythm mode overlays on channels
   6..8, and give them fixed pitches.  Values are the classic AdLib rhythm
   set: audible thumps, snaps and hats. */
static void setup_percussion(void)
{
    static const struct { unsigned char op, c1, c2, c3, c4, c5; } P[6] = {
        { 0x10, 0x00, 0x00, 0xF8, 0xF8, 0x00 },   /* BD modulator            */
        { 0x13, 0x00, 0x00, 0xF8, 0xF8, 0x00 },   /* BD carrier              */
        { 0x11, 0x01, 0x00, 0xF8, 0xF7, 0x00 },   /* Hi-hat                  */
        { 0x14, 0x01, 0x00, 0xF8, 0xF7, 0x00 },   /* Snare                   */
        { 0x12, 0x05, 0x00, 0xF8, 0xF6, 0x00 },   /* Tom                     */
        { 0x15, 0x01, 0x00, 0xF8, 0xF7, 0x00 }    /* Cymbal                  */
    };
    int i;
    for (i = 0; i < 6; ++i) {
        opl_write(0x20 + P[i].op, P[i].c1);
        opl_write(0x40 + P[i].op, P[i].c2);
        opl_write(0x60 + P[i].op, P[i].c3);
        opl_write(0x80 + P[i].op, P[i].c4);
        opl_write(0xE0 + P[i].op, P[i].c5);
    }
    opl_write(0xC0 + 6, 0x00);
    opl_write(0xA0 + 6, 0x58); opl_write(0xB0 + 6, 0x09);   /* BD  pitch      */
    opl_write(0xA0 + 7, 0xD4); opl_write(0xB0 + 7, 0x0D);   /* SD/HH pitch    */
    opl_write(0xA0 + 8, 0x58); opl_write(0xB0 + 8, 0x0B);   /* TT/TC pitch    */
}

void opl_init(void)
{
    int v;
    if (!opl_present())
        return;
    opl_write(0x01, 0x20);             /* enable the waveform-select regs    */
    opl_write(0x08, 0x00);             /* no note-select / CSM               */
    opl_write(0xBD, 0x00);             /* clear rhythm/AM/vibrato            */
    for (v = 0; v < OPL_MELODIC; ++v) {
        opl_write(0xA0 + v, 0x00);
        opl_write(0xB0 + v, 0x00);     /* key off                            */
        g_vblock[v] = 0; g_vfnum[v] = 0;
        g_vpatch[v] = 0;
        load_patch(v, 0);              /* piano until a program says otherwise*/
    }
    setup_percussion();
    opl_write(0xBD, 0x20);             /* rhythm mode on (ch 6..8 = drums)   */
}

void opl_drums(int bits)
{
    if (!opl_present())
        return;
    bits &= 0x1F;
    opl_write(0xBD, 0x20);             /* drop the drum bits...              */
    opl_write(0xBD, (unsigned char)(0x20 | bits));  /* ...and strike (0->1)  */
}

void opl_note_on(int voice, int midinote)
{
    int oct, semi, block, fnum;
    if (!opl_present() || voice < 0 || voice >= OPL_MELODIC)
        return;
    if (midinote < 0)   midinote = 0;
    if (midinote > 127) midinote = 127;
    oct  = midinote / 12 - 1;          /* MIDI note 12 = C0 here             */
    semi = midinote % 12;
    block = oct;
    if (block < 0) block = 0;
    if (block > 7) block = 7;
    fnum = (int)g_fnum[semi];
    g_vblock[voice] = (unsigned char)block;
    g_vfnum[voice]  = (unsigned)fnum;
    opl_write(0xA0 + voice, fnum & 0xFF);
    opl_write(0xB0 + voice,
              0x20 | (block << 2) | ((fnum >> 8) & 0x03));   /* key on       */
}

void opl_note_off(int voice)
{
    if (!opl_present() || voice < 0 || voice >= OPL_MELODIC)
        return;
    /* Clear only the key-on bit; keep block/fnum so the release tail is in
       tune. */
    opl_write(0xB0 + voice,
              (g_vblock[voice] << 2) | ((g_vfnum[voice] >> 8) & 0x03));
}

void opl_silence(void)
{
    int v;
    if (!opl_present())
        return;
    for (v = 0; v < OPL_MELODIC; ++v)
        opl_note_off(v);
    opl_write(0xBD, 0x20);             /* release the drums, keep rhythm on  */
}
