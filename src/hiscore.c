/* ======================================================================
 * hiscore.c - Persistent high scores for CASTALIA/386
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "hiscore.h"
#include "system.h"    /* sys_home_path: pin CASTALIA.HI to the home dir */

/* This file has the same load-caps-then-save-writes-back-short shape the
   Cardfile was just fixed for, and is deliberately NOT given the same
   guard.  Sixteen slots against the four games that submit scores means
   it cannot overflow through use, only through hand-editing; a score
   table is not user work; and a clipped-guard here would trade that for
   a new failure mode where legitimate scores silently stop saving.  The
   Cardfile is different on every count - a user fills all sixteen cards
   by design, they are the user's own writing, and CARDFILE.DAT is plain
   text with form-feed separators that invites hand-editing. */
#define HI_MAX  16
#define HI_FILE "CASTALIA.HI"
#define HI_KEYW 11                     /* longest game key we keep           */

static char   g_name[HI_MAX][HI_KEYW + 1];
static long   g_best[HI_MAX];
static int    g_n = 0;
static bool_t g_loaded  = FALSE;
/* CASTALIA.HI held more entries than the table carries (or a malformed
   line stopped the scan), so writing it back would destroy the rest. */
static bool_t g_clipped = FALSE;

/* Always next to CASTALIA.EXE, never in the last-browsed directory. */
static FILE *hi_open(const char *mode)
{
    char p[80];
    sys_home_path(p, (int)sizeof(p), HI_FILE);
    return fopen(p, mode);
}

static void hi_load(void)
{
    FILE *f;
    char  nm[32];
    long  sc;
    if (g_loaded)
        return;
    g_loaded = TRUE;
    f = hi_open("r");
    if (f == NULL)
        return;
    while (g_n < HI_MAX && fscanf(f, "%31s %ld", nm, &sc) == 2) {
        int i = 0;
        while (nm[i] != '\0' && i < HI_KEYW) { g_name[g_n][i] = nm[i]; ++i; }
        g_name[g_n][i] = '\0';
        g_best[g_n] = sc;
        ++g_n;
    }
    /* Anything left in the file did not fit the table (or a malformed line
       stopped the scan).  Rewriting from what we DID read would delete the
       rest, so stop writing instead.
       Two traps here, both of which I got wrong first time: an I/O error
       must COUNT as "did not read it all" (ferror, not !ferror), and a
       file holding exactly HI_MAX entries exits the loop without ever
       touching EOF - so prove another entry really exists before calling
       it clipped, or a full table would silently stop saving forever. */
    if (ferror(f)) {
        g_clipped = TRUE;
    } else if (!feof(f)) {
        char nm2[32];
        long sc2;
        if (fscanf(f, "%31s %ld", nm2, &sc2) == 2)
            g_clipped = TRUE;          /* a real entry we cannot hold      */
    }
    fclose(f);
}

static void hi_save(void)
{
    FILE *f;
    int   i, bad;
    if (g_clipped)
        return;                        /* never overwrite what we lost     */
    f = hi_open("w");
    if (f == NULL)
        return;
    for (i = 0; i < g_n; ++i)
        fprintf(f, "%s %ld\n", g_name[i], g_best[i]);
    /* fopen("w") already truncated the real file, so a failure here has
       destroyed it: at least stop claiming the scores are safe. */
    bad = ferror(f) ? 1 : 0;
    if (fclose(f) != 0)
        bad = 1;
    if (bad)
        g_clipped = TRUE;              /* do not try again over the wreck  */
}

static int hi_find(const char *game)
{
    int i;
    for (i = 0; i < g_n; ++i)
        if (strcmp(g_name[i], game) == 0)
            return i;
    return -1;
}

long hiscore_best(const char *game)
{
    int i;
    hi_load();
    i = hi_find(game);
    return (i >= 0) ? g_best[i] : 0L;
}

bool_t hiscore_submit(const char *game, long score)
{
    int i, k;
    hi_load();
    i = hi_find(game);
    if (i < 0) {                       /* first time we have seen this game  */
        if (g_n >= HI_MAX)
            return FALSE;
        i = g_n++;
        for (k = 0; game[k] != '\0' && k < HI_KEYW; ++k)
            g_name[i][k] = game[k];
        g_name[i][k] = '\0';
        g_best[i] = 0;
    }
    if (score > g_best[i]) {
        g_best[i] = score;
        hi_save();
        return TRUE;
    }
    return FALSE;
}
