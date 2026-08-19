/* ======================================================================
 * ci/nearfar_cases.c - the near/far checker's own regression fixture
 * ----------------------------------------------------------------------
 * NOT part of the build.  ci/nearfar.py runs itself over this file and
 * fails unless it flags EVERY case marked BAD and none of the ones marked
 * OK.  The gate has been rewritten four times and shipped green over live
 * memory corruption twice, so it needs tests of its own more than most of
 * the code it guards.
 *
 * Every BAD line below is a shape that really truncates a segment in the
 * medium model, and every one of them compiles without a single Watcom
 * diagnostic - even at -wx -we.  Array decay from far storage is defended
 * by this checker alone.
 * ====================================================================== */
#include <string.h>

typedef struct { char name[13]; int size; } Ent;

static char           far g_buf[64];
static unsigned char  far g_pal[256];        /* multi-word type          */
static unsigned long  far g_ticks[8];        /* multi-word type          */
static Ent            far g_ent[16];
static char           far g_a[8], g_b[8];    /* second declarator        */
static const char * const far g_verbs[4] = { "a", "b", "c", "d" };

static void sink(char *p)            { p[0] = 'x'; }
static void sink_far(char far *p)    { p[0] = 'x'; }
static void sink3(int a, int b, char *p) { (void)a; (void)b; p[0] = 'x'; }

void nearfar_cases(void)
{
    Ent far *e = &g_ent[0];
    char near_buf[64];

    /* ---- BAD: far array decaying into a near parameter --------------- */
    sink(g_buf);                       /* BAD */
    sink((char *)g_pal);               /* BAD - a cast hides nothing      */
    sink(g_buf + 2);                   /* BAD - decay then offset         */
    sink(&g_buf[3]);                   /* BAD                             */
    sink(g_b);                         /* BAD - the second declarator     */
    sink3(1, 2, g_buf);                /* BAD - not argument zero         */
    strcpy(near_buf, g_buf);           /* BAD - libc, argument two        */

    /* ---- BAD: through a far POINTER ---------------------------------- */
    sink(e->name);                     /* BAD - this one deleted files    */
    sink(&e->name[1]);                 /* BAD                             */
    remove(e->name);                   /* BAD - libc                      */

    /* ---- BAD: a member of a far struct array ------------------------- */
    sink(g_ent[2].name);               /* BAD                             */

    /* ---- OK: these must NOT be reported ------------------------------ */
    sink_far(g_buf);                   /* OK - the parameter is far       */
    sink_far(e->name);                 /* OK                              */
    sink(near_buf);                    /* OK - near all the way           */
    _fstrncpy(near_buf, g_buf, 8);     /* OK - a far-aware helper         */
    near_buf[0] = (char)g_pal[3];      /* OK - an element, not an address */
    near_buf[1] = (char)g_ticks[1];    /* OK                              */
    sink((char *)g_verbs[0]);          /* OK - the ELEMENT is a near char* */
}
