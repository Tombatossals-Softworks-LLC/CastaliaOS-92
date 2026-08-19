/* ======================================================================
 * textscan.c - substring search inside a file, for CASTALIA/386
 * ----------------------------------------------------------------------
 * See textscan.h.  The whole of the interesting part is the overlap in
 * text_in_file: without carrying the last nlen-1 bytes of each block
 * into the next, a match lying across a read boundary is invisible,
 * which on a 512-byte buffer is one blind spot every 512 bytes.  That
 * failure is not visible in any hand test small enough to type - the
 * file has to be over half a kilobyte before the first one exists - so
 * it would have shipped as "sometimes it does not find things".
 * ====================================================================== */
#include <stdio.h>
#include "textscan.h"

/* 512, not larger: this is a LOCAL, because the medium model passes a
   plain char * as NEAR and fread would truncate the segment of a far
   static.  The stack is 8 KB and this frame is one call deep off the
   directory sweep. */
#define TS_BUF 512

int text_needle(char *dst, int cap, const char *src)
{
    int i = 0;
    if (dst == (char *)0 || cap <= 0)
        return 0;
    if (src != (const char *)0) {
        while (src[i] != '\0' && i < cap - 1) {
            char c = src[i];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 32);
            dst[i] = c;
            ++i;
        }
    }
    dst[i] = '\0';
    return i;
}

bool_t text_in_file(const char *path, const char *needle, int nlen)
{
    char   buf[TS_BUF];
    FILE  *f;
    int    keep;
    int    have = 0;
    bool_t found = FALSE;

    if (nlen <= 0)
        return TRUE;                   /* no filter: everything matches    */
    if (nlen > TS_BUF)
        return FALSE;                  /* longer than a block; cannot look */
    keep = nlen - 1;
    f = fopen(path, "rb");
    if (f == NULL)
        return FALSE;                  /* unreadable is not a match        */
    for (;;) {
        size_t got = fread(buf + have, 1, (size_t)(TS_BUF - have), f);
        int    n, i, j;
        if (got == 0)
            break;
        n = have + (int)got;
        for (i = 0; i + nlen <= n; ++i) {
            for (j = 0; j < nlen; ++j) {
                char c = buf[i + j];
                if (c >= 'a' && c <= 'z')
                    c = (char)(c - 32);
                if (c != needle[j])
                    break;
            }
            if (j == nlen) { found = TRUE; break; }
        }
        if (found)
            break;
        /* Carry the tail, so the next block can complete a match that
           starts in this one.  n < keep only on a file shorter than the
           needle, where there is nothing to carry and nothing to find. */
        if (keep > 0 && n >= keep) {
            for (i = 0; i < keep; ++i)
                buf[i] = buf[n - keep + i];
            have = keep;
        } else {
            have = 0;
        }
    }
    fclose(f);
    return found;
}
