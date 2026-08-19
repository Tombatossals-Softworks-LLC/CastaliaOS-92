/* ======================================================================
 * recent.c - the Documents menu's recently-opened list for Castalia 92
 * ====================================================================== */
#include <stdio.h>
#include <string.h>
#include "recent.h"
#include "system.h"    /* sys_home_path: pin RECENT.TXT to the home dir  */

#define RECENT_FILE "RECENT.TXT"

/* far, and compact: a name and a folder is all a recent document is. */
typedef struct {
    char name[14];                 /* an 8.3 name plus the terminator     */
    char dir[CFG_PATH_LEN];
} RecEnt;

static RecEnt far g_rec[RECENT_MAX];
static int        g_n = 0;
/* The label the menu draws must live somewhere NEAR - menu.c hands it to
   font_draw, whose parameter is a near char*.  One small ring, not six
   full records. */
static char g_lbl[RECENT_MAX][14];

int recent_count(void) { return g_n; }

const char *recent_name(int i)
{
    if (i < 0 || i >= g_n)
        return "";
    return g_lbl[i];
}

void recent_fill(int i, CfgShortcut *out)
{
    out->name[0] = '\0';
    out->command[0] = '\0';
    out->path[0] = '\0';
    out->icon[0] = '\0';
    out->freemem = FALSE;
    if (i < 0 || i >= g_n)
        return;
    _fstrncpy(out->command, g_rec[i].name, CFG_CMD_LEN - 1);
    out->command[CFG_CMD_LEN - 1] = '\0';
    _fstrncpy(out->path, g_rec[i].dir, CFG_PATH_LEN - 1);
    out->path[CFG_PATH_LEN - 1] = '\0';
}

/* Keep the near label ring in step with the far store. */
static void relabel(void)
{
    int i;
    for (i = 0; i < g_n; ++i) {
        _fstrncpy(g_lbl[i], g_rec[i].name, 13);
        g_lbl[i][13] = '\0';
    }
}

void recent_clear(void)
{
    g_n = 0;
    recent_save();
}

static void far_copy(char far *d, const char *s, int cap)
{
    int i = 0;
    if (s == NULL) { d[0] = '\0'; return; }
    while (s[i] != '\0' && i < cap - 1) { d[i] = s[i]; ++i; }
    d[i] = '\0';
}

static bool_t far_same(const char far *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return FALSE;
        ++a; ++b;
    }
    return (*a == *b) ? TRUE : FALSE;
}

static FILE *rec_open(const char *mode)
{
    char p[80];
    sys_home_path(p, (int)sizeof(p), RECENT_FILE);
    return fopen(p, mode);
}

void recent_note(const char *dir, const char *name)
{
    int i, at = -1;
    if (name == NULL || name[0] == '\0')
        return;

    for (i = 0; i < g_n; ++i)          /* already listed?  it moves up     */
        if (far_same(g_rec[i].name, name) && far_same(g_rec[i].dir, dir)) {
            at = i;
            break;
        }
    if (at < 0) {
        at = (g_n < RECENT_MAX) ? g_n++ : RECENT_MAX - 1;  /* drop the last */
    }
    for (i = at; i > 0; --i)           /* shuffle down, newest to the top  */
        g_rec[i] = g_rec[i - 1];

    far_copy(g_rec[0].name, name, 14);
    far_copy(g_rec[0].dir,  dir,  CFG_PATH_LEN);
    relabel();
    recent_save();
}

/* One record per line: "NAME<tab>DIR".  A tab, not a space or a comma -
   a DOS path can hold neither, and a name cannot hold a tab. */
void recent_load(void)
{
    FILE *f = rec_open("r");
    char line[CFG_PATH_LEN + CFG_NAME_LEN + 4];
    g_n = 0;
    if (f == NULL)
        return;
    while (g_n < RECENT_MAX && fgets(line, sizeof(line), f) != NULL) {
        char *tab = strchr(line, '\t');
        char *end;
        if (tab == NULL)
            continue;
        *tab = '\0';
        end = tab + 1;
        while (*end != '\0' && *end != '\n' && *end != '\r')
            ++end;
        *end = '\0';
        if (line[0] == '\0')
            continue;
        far_copy(g_rec[g_n].name, line,    14);
        far_copy(g_rec[g_n].dir,  tab + 1, CFG_PATH_LEN);
        ++g_n;
    }
    fclose(f);
    relabel();
}

void recent_save(void)
{
    FILE *f = rec_open("w");
    int i;
    if (f == NULL)
        return;                        /* a lost convenience, not an error */
    for (i = 0; i < g_n; ++i) {
        char nm[14], dr[CFG_PATH_LEN];
        _fstrncpy(nm, g_rec[i].name, 13); nm[13] = '\0';
        _fstrncpy(dr, g_rec[i].dir, CFG_PATH_LEN - 1);
        dr[CFG_PATH_LEN - 1] = '\0';
        fprintf(f, "%s\t%s\n", nm, dr);   /* near copies: fprintf is near */
    }
    fclose(f);
}
