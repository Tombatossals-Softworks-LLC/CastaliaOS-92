/* ======================================================================
 * card.c - Cardfile (an index-card notepad) for CASTALIA/386
 * ----------------------------------------------------------------------
 * The card store lives in far memory; the card being edited is copied into
 * a small near buffer so the editing itself is the same simple cursor logic
 * as the Scrap Box.
 * ====================================================================== */
#include <string.h>
#include <stdio.h>
#include "card.h"
#include "video.h"
#include "ui.h"
#include "font.h"
#include "keyboard.h"
#include "dialog.h"
#include "system.h"   /* sys_home_path: keep CARDFILE.DAT beside the EXE */

#define NCARDS   16
#define CARD_LEN 240

static char far g_store[NCARDS * CARD_LEN];
static char g_edit[CARD_LEN];
static int  g_len, g_cursor, g_cur, g_count;

static int  g_lstart[64];
static int  g_nlines, g_rows;

/* ---- store <-> edit buffer ------------------------------------------ */
static void load_card(int i)
{
    int k = 0, base = i * CARD_LEN;
    while (k < CARD_LEN - 1 && g_store[base + k]) { g_edit[k] = g_store[base + k]; ++k; }
    g_edit[k] = '\0';
    g_len = k; g_cursor = 0;
}
static bool_t g_dirty = FALSE;
void card_flush(void);   /* defined below with the file helpers */

static void save_card(int i)
{
    int k, base = i * CARD_LEN;
    for (k = 0; k < g_len; ++k) g_store[base + k] = g_edit[k];
    g_store[base + k] = '\0';
    g_dirty = TRUE;                        /* flushed by card_flush()       */
}

/* ---- persistence -----------------------------------------------------
 * Cardfile used to keep everything in RAM and wipe the store on every
 * open, so a card survived exactly as long as the window did.  The deck
 * now lives in CARDFILE.DAT next to CASTALIA.EXE: one card per record,
 * blank-line separated, written whenever a card changes.
 * -------------------------------------------------------------------- */
#define CARD_FILE "CARDFILE.DAT"
#define CARD_SEP  "\f\n"                 /* form feed: a card boundary    */

static bool_t g_loaded = FALSE;
/* The deck on disk held MORE cards than the table.  card_flush writes
   g_count records over the whole file, so with the tail silently dropped
   at load, merely flipping a card - go_card() saves and flushes - would
   have written the truncated deck back and destroyed the rest.  The
   Agenda already refuses to save a file it could not fully read; the
   Cardfile did not, and CARDFILE.DAT is plain text with form-feed
   separators, which is exactly the kind of file a DOS user edits by
   hand. */
static bool_t g_clipped = FALSE;

static FILE *card_fopen(const char *mode)
{
    char p[80];
    sys_home_path(p, (int)sizeof(p), CARD_FILE);
    return fopen(p, mode);
}

/* Write the deck out.  Called when the user leaves a card, adds or
   deletes one, and when the window closes - never per keystroke: on a
   386 with a floppy that would mean a disk write for every letter. */
void card_flush(void)
{
    FILE *f;
    int i, k, base;
    if (!g_dirty)
        return;
    if (g_clipped)
        return;                        /* never overwrite what we lost     */
    /* Clearing g_dirty BEFORE the write - with fopen("w") having already
       truncated the file - meant an interrupted or failing write left the
       deck marked clean and CARDFILE.DAT empty.  Write first, check the
       close, and only then call it saved. */
    f = card_fopen("w");
    if (f == NULL) {
        /* agenda.c reports this exact condition and this did not, so a
           write-protected or full disk lost the deck in silence while
           the identical failure in the to-do list announced itself. */
        dialog_message("Cardfile", "Could not write CARDFILE.DAT.",
                       "Disk full, or write-protected.");
        return;
    }
    for (i = 0; i < g_count; ++i) {
        base = i * CARD_LEN;
        for (k = 0; k < CARD_LEN && g_store[base + k]; ++k)
            fputc(g_store[base + k], f);
        fputs(CARD_SEP, f);
    }
    /* Say so.  Leaving g_dirty set is right - the deck in RAM is still
       good - but returning in silence meant the user was never told that
       fopen("w") had already truncated CARDFILE.DAT and nothing replaced
       it.  Every sibling save path (agenda, paint, hiscore) reports.
       Sequenced, not "ferror(f) || fclose(f)": || short-circuits, so a
       write error would skip the fclose and leak the handle - the very
       bug agenda.c:63 records. */
    {
        int bad = ferror(f) ? 1 : 0;
        if (fclose(f) != 0)
            bad = 1;
        if (bad) {
            dialog_message("Cardfile", "CARDFILE.DAT was not written",
                           "in full - the disk may be full.");
            return;
        }
    }
    g_dirty = FALSE;
}

bool_t card_is_dirty(void) { return g_dirty; }

static void cards_load(void)
{
    FILE *f = card_fopen("r");
    int c, k = 0, base;
    if (f == NULL)
        return;
    g_count = 0;
    base = 0;
    while ((c = fgetc(f)) != EOF && g_count < NCARDS) {
        if (c == '\f') {                  /* end of this card              */
            g_store[base + k] = '\0';
            ++g_count;
            base = g_count * CARD_LEN;
            k = 0;
            if ((c = fgetc(f)) != '\n' && c != EOF)
                ungetc(c, f);
            continue;
        }
        if (c == '\r')
            continue;
        if (k < CARD_LEN - 1)
            g_store[base + k++] = (char)c;
    }
    if (k > 0 && g_count < NCARDS) {       /* trailing card, no separator   */
        g_store[base + k] = '\0';
        ++g_count;
    }
    /* Anything still in the file did not fit. */
    if (fgetc(f) != EOF)
        g_clipped = TRUE;
    fclose(f);
}

void card_open(void)
{
    int i;
    const char *welcome = "Welcome to Cardfile.\nType to edit this card.\n< > or PgUp/PgDn flip.\nNew or F7 adds, Del or F8 removes.";
    if (g_loaded) {                        /* already open: just show it    */
        load_card(g_cur);
        return;
    }
    g_loaded = TRUE;
    for (i = 0; i < NCARDS * CARD_LEN; ++i) g_store[i] = 0;
    g_cur = 0; g_count = 0; g_clipped = FALSE;
    cards_load();
    if (g_count == 0) {                    /* first ever run                */
        strcpy(g_edit, welcome);
        g_len = (int)strlen(g_edit); g_cursor = 0;
        g_count = 1;
        save_card(0);
    }
    load_card(0);
}

/* ---- wrapping (over the near edit buffer) ---------------------------- */
static void wrap(int cols)
{
    int i, col;
    if (cols < 1) cols = 1;
    g_nlines = 0; g_lstart[g_nlines++] = 0; col = 0;
    for (i = 0; i < g_len; ++i) {
        if (g_edit[i] == '\n') {
            if (g_nlines < 64) g_lstart[g_nlines++] = i + 1;
            col = 0;
        } else if (++col >= cols) {
            if (g_nlines < 64) g_lstart[g_nlines++] = i + 1;
            col = 0;
        }
    }
}
static int line_of(int pos)
{
    int l = 0;
    while (l + 1 < g_nlines && g_lstart[l + 1] <= pos) ++l;
    return l;
}
static int line_end(int l)  { return (l + 1 < g_nlines) ? g_lstart[l + 1] : g_len; }
static int line_len(int l)
{
    int e = line_end(l);
    if (e > g_lstart[l] && g_edit[e - 1] == '\n') --e;
    return e - g_lstart[l];
}

/* ---- layout --------------------------------------------------------- */
#define TB_N 4
static const char *TB_LBL[TB_N] = { "<", ">", "New", "Del" };
static void tb_rect(const Rect *c, int i, Rect *r)
{
    int bw = font_adv() * 4;
    rect_set(r, c->x + i * (bw + 1), c->y, bw, font_h() + 5);
}
static void area_rect(const Rect *c, Rect *a)
{
    int top = 2 * (font_h() + 5) + 2;
    rect_set(a, c->x, c->y + top, c->w, c->h - top);
}

/* ---- drawing -------------------------------------------------------- */
void card_draw(const Rect *cl)
{
    Rect a, r;
    int i, lineh, cols, row, scroll, cline;
    char idx[40];

    for (i = 0; i < TB_N; ++i) { tb_rect(cl, i, &r); ui_button(&r, TB_LBL[i], FALSE); }

    sprintf(idx, "Card %d / %d", g_cur + 1, g_count);
    font_draw(cl->x + 3, cl->y + font_h() + 7, idx, C_BLACK);
    /* Refusing to save in silence is its own trap - the user would go on
       editing a deck that is never written.  Say so, on the line that
       already reports which card this is, in the colour the rest of the
       shell uses for "your data is at risk".
       Short, and only when it fits: this header is far narrower than the
       Agenda's, which centres a full sentence on a line of its own.  The
       first wording ran off the right edge as "NOT SAVI". */
    if (g_clipped) {
        const char *warn = "NOT SAVING";
        int wx = cl->x + 3 + font_text_width(idx) + FONT_ADV;
        if (wx + font_text_width(warn) <= cl->x + cl->w - 3)
            font_draw(wx, cl->y + font_h() + 7, warn, C_RED);
    }

    area_rect(cl, &a);
    vid_fillrect(a.x, a.y, a.w, a.h, C_CREAM);
    ui_sink(a.x, a.y, a.w, a.h);

    lineh = font_h() + 1;
    cols  = (a.w - 6) / FONT_ADV;
    g_rows = (a.h - 4) / lineh; if (g_rows < 1) g_rows = 1;
    wrap(cols);

    cline = line_of(g_cursor);
    scroll = 0;
    if (cline >= g_rows) scroll = cline - g_rows + 1;

    for (row = 0; row < g_rows; ++row) {
        int l = scroll + row, j, x, y;
        if (l >= g_nlines) break;
        x = a.x + 3; y = a.y + 2 + row * lineh;
        for (j = g_lstart[l]; j < line_end(l); ++j) {
            if (g_edit[j] == '\n') break;
            font_draw_char(x, y, g_edit[j], C_BLACK);
            x += FONT_ADV;
        }
    }
    if (cline >= scroll && cline < scroll + g_rows) {
        int col = g_cursor - g_lstart[cline];
        vid_vline(a.x + 3 + col * FONT_ADV, a.y + 2 + (cline - scroll) * lineh,
                  font_h(), C_BLACK);
    }
}

/* ---- editing -------------------------------------------------------- */
static void insert_char(char ch)
{
    if (g_len >= CARD_LEN - 1) return;
    memmove(g_edit + g_cursor + 1, g_edit + g_cursor, (size_t)(g_len - g_cursor));
    g_edit[g_cursor] = ch; ++g_len; ++g_cursor; g_edit[g_len] = '\0';
    save_card(g_cur);
}
static void delete_at(int pos)
{
    if (pos < 0 || pos >= g_len) return;
    memmove(g_edit + pos, g_edit + pos + 1, (size_t)(g_len - pos - 1));
    --g_len; g_edit[g_len] = '\0';
    save_card(g_cur);
}

/* New and Del, factored out of the toolbar handler so the keyboard can
   reach them too.  They were click-only, and so were < and >: without a
   mouse driver the Cardfile opened on card one and there was no way to
   reach the other fifteen, add one or remove one.  The same gap Settings
   and the Arcade had, in the one applet that holds the user's data. */
static void new_card(void)
{
    if (g_count >= NCARDS)
        return;
    save_card(g_cur);
    g_cur = g_count++;
    g_len = 0; g_cursor = 0; g_edit[0] = '\0';
    save_card(g_cur);
}

static void del_card(void)
{
    int k;
    /* The Disk Cabinet confirms a file delete; this used to drop a card
       instantly with no prompt and no undo. */
    if (dialog_confirm("Cardfile", "Delete this card?",
                       "It cannot be brought back.") != DLG_YES)
        return;
    for (k = g_cur; k < g_count - 1; ++k) {
        int s = k * CARD_LEN, d = (k + 1) * CARD_LEN, j;
        for (j = 0; j < CARD_LEN; ++j) g_store[s + j] = g_store[d + j];
    }
    if (g_count > 1) --g_count; else g_store[0] = 0;
    if (g_cur >= g_count) g_cur = g_count - 1;
    load_card(g_cur);
    g_dirty = TRUE;                /* or the deleted card resurrects   */
    card_flush();
}

static void go_card(int i)
{
    save_card(g_cur);
    card_flush();                          /* leaving a card commits it     */
    if (i < 0) i = 0; if (i >= g_count) i = g_count - 1;
    g_cur = i; load_card(i);
}

bool_t card_click(const Rect *cl, int mx, int my)
{
    Rect r, a;
    int i, row, col, l, lineh;
    for (i = 0; i < TB_N; ++i) {
        tb_rect(cl, i, &r);
        if (rect_contains(&r, mx, my)) {
            if (i == 0) go_card(g_cur - 1);
            else if (i == 1) go_card(g_cur + 1);
            else if (i == 2) new_card();
            else             del_card();
            return TRUE;
        }
    }
    area_rect(cl, &a);
    if (mx < a.x || mx >= a.x + a.w || my < a.y || my >= a.y + a.h) return FALSE;
    lineh = font_h() + 1;
    row = (my - a.y - 2) / lineh;
    col = (mx - a.x - 3 + FONT_ADV / 2) / FONT_ADV; if (col < 0) col = 0;
    /* The draw scrolls long cards so the cursor line is visible; map the
       clicked row through the same scroll or the caret lands lines off. */
    {
        int cline = line_of(g_cursor), scroll = 0;
        if (cline >= g_rows) scroll = cline - g_rows + 1;
        l = row + scroll;
    }
    if (l < 0) l = 0; if (l >= g_nlines) l = g_nlines - 1;
    if (col > line_len(l)) col = line_len(l);
    g_cursor = g_lstart[l] + col;
    if (g_cursor > g_len) g_cursor = g_len;
    return TRUE;
}

bool_t card_key(int key)
{
    if (key >= 32 && key < 127) { insert_char((char)key); return TRUE; }
    switch (key) {
    case KEY_ENTER: insert_char('\n'); return TRUE;
    case KEY_BACK:  if (g_cursor > 0) { --g_cursor; delete_at(g_cursor); } return TRUE;
    case KEY_DEL:   delete_at(g_cursor); return TRUE;
    case KEY_LEFT:  if (g_cursor > 0) --g_cursor; return TRUE;
    case KEY_RIGHT: if (g_cursor < g_len) ++g_cursor; return TRUE;
    case KEY_UP: {
        int l = line_of(g_cursor), col = g_cursor - g_lstart[l];
        if (l > 0) { int pl = l - 1, pn = line_len(pl);
                     g_cursor = g_lstart[pl] + (col < pn ? col : pn); }
        return TRUE;
    }
    case KEY_DOWN: {
        int l = line_of(g_cursor), col = g_cursor - g_lstart[l];
        if (l + 1 < g_nlines) { int nl = l + 1, nn = line_len(nl);
                                g_cursor = g_lstart[nl] + (col < nn ? col : nn); }
        return TRUE;
    }
    /* Home and End are the ends of the LINE, as they are in the Scrap
       Box - the two editors should not disagree about a key this basic. */
    case KEY_HOME: g_cursor = g_lstart[line_of(g_cursor)]; return TRUE;
    case KEY_END:  g_cursor = line_end(line_of(g_cursor)); return TRUE;
    /* PgUp/PgDn flip the deck: a card is 240 characters, so there is no
       page to scroll WITHIN one, and flipping is what the two toolbar
       arrows do.  F7 adds a card, matching the Disk Cabinet's F7 for a
       new folder; F8 removes one, because Del is spoken for here by the
       character it deletes. */
    case KEY_PGUP: go_card(g_cur - 1); return TRUE;
    case KEY_PGDN: go_card(g_cur + 1); return TRUE;
    case KEY_F7:   new_card();         return TRUE;
    case KEY_F8:   del_card();         return TRUE;
    default: break;
    }
    return FALSE;
}
