/* Malloc-backed stand-ins for the DOS block allocator (see host.h). */
#include "host.h"

void *host_block[HOST_BLOCKS];

void host_reset_blocks(void)
{
    int i;
    for (i = 0; i < HOST_BLOCKS; ++i) {
        if (host_block[i] != NULL) {
            free(host_block[i]);
            host_block[i] = NULL;
        }
    }
}

int host_live_blocks(void)
{
    int i, n = 0;
    for (i = 0; i < HOST_BLOCKS; ++i)
        if (host_block[i] != NULL)
            ++n;
    return n;
}

/* Slot 0 is reserved so a zero "segment" still reads as unallocated in
   the parsers, which use 0 as their "no block" sentinel. */
int host_allocmem(unsigned paras, unsigned *seg)
{
    int i;
    for (i = 1; i < HOST_BLOCKS; ++i) {
        if (host_block[i] == NULL) {
            /* Exactly the requested size: an overrun by even one byte is
               then a real heap overflow the sanitizer will catch. */
            size_t n = (size_t)paras * 16u;
            host_block[i] = malloc(n);
            if (host_block[i] == NULL)
                return 1;
            /* DOS hands back whatever the last owner left there, and the
               parsers' scratch tables are only partly initialised before
               use - a decoder that trusts an unwritten dictionary entry
               behaves very differently on dirty memory than on the zeroed
               pages malloc happens to give a fresh process.  Poison it so
               the tests see the DOS case. */
            memset(host_block[i], 0xFF, n);
            *seg = (unsigned)i;
            return 0;
        }
    }
    return 1;
}

int host_freemem(unsigned seg)
{
    if (seg == 0 || seg >= HOST_BLOCKS || host_block[seg] == NULL)
        return 1;
    free(host_block[seg]);
    host_block[seg] = NULL;
    return 0;
}
