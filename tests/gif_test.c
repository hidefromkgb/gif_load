/** gif_load test driver: decodes a GIF and prints a stable, greppable
    description of every frame, so that the output can be diffed against
    a known-good one. Optionally dumps the pixel indices of the frames.

    usage: gif_test <in>.gif [<out>.raw]
**/
#include "../gif_load.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    FILE *dump;
    unsigned long hash;
    uint8_t *data;
    long size;
} STAT;

static unsigned long Hash(unsigned long hash, const uint8_t *bptr, long size) {
    long iter;

    for (iter = 0; iter < size; iter++)
        hash = hash * 33uL + bptr[iter]; /** plain old djb2 **/
    return hash;
}

static void Frame(void *data, struct GIF_WHDR *whdr) {
    STAT *stat = (STAT*)data;
    long size = whdr->frxd * whdr->fryd;
    char cpal[16]; /** the palette of this very frame, on its own **/

    /** sic: the pixels chain from frame to frame, so that the diff points
        at the first one that went wrong, while the palette does not: a
        local palette should stand out exactly where the GIF switches to
        it, and a global one should repeat itself unchanged **/
    stat->hash = Hash(stat->hash, whdr->bptr, size);
    if (whdr->cpal)
        sprintf(cpal, "%08lx", Hash(5381uL, (const uint8_t*)whdr->cpal,
                                    whdr->clrs * 3L) & 0xFFFFFFFFuL);
    else
        sprintf(cpal, "%s", "none");
    printf("frame %ld/%ld %ldx%ld +%ld+%ld intr=%ld clrs=%ld tran=%ld "
           "mode=%ld time=%ld hash=%08lx pal=%s\n", whdr->ifrm, whdr->nfrm,
           whdr->frxd, whdr->fryd, whdr->frxo, whdr->fryo, whdr->intr,
           whdr->clrs, whdr->tran, whdr->mode, whdr->time,
           stat->hash & 0xFFFFFFFFuL, cpal);
    if (stat->dump)
        fwrite(whdr->bptr, 1, (size_t)size, stat->dump);
}

static void Meta(void *data, struct GIF_WHDR *whdr) {
    STAT *stat = (STAT*)data;
    long left = (long)(stat->data + stat->size - whdr->bptr), iter, frst, last;

    /** the loader hands over the chunk as is, truncated or not, so the
        callback walks it within the bounds of the GIF data: an 8-byte
        identifier, which GIF89a restricts to printable ASCII and which is
        fixed-width, hence printed verbatim; a 3-byte auth code that can be
        anything, hence dumped in hex; then sub-blocks up to the empty one
        that ends them, each of which is dumped in hex too **/
    left = (left > 0) ? left : 0;
    for (iter = 0; (iter < 8) && (iter < left); iter++)
        if ((whdr->bptr[iter] < 0x20) || (whdr->bptr[iter] > 0x7E)) {
            printf("meta (invalid identifier)\n");
            return;
        }
    printf("meta");
    if (iter > 0)
        printf(" %.*s", (int)iter, (const char*)whdr->bptr);
    for (iter = 8; (iter < 11) && (iter < left); iter++)
        printf(" %02X", whdr->bptr[iter]);
    for (iter = 11; (iter < left) && whdr->bptr[iter]; iter = last) {
        last = iter + 1 + whdr->bptr[iter];
        printf(" [");
        for (frst = ++iter; (iter < last) && (iter < left); iter++)
            printf("%s%02X", (iter > frst) ? " " : "", whdr->bptr[iter]);
        printf("]");
    }
    printf("%s\n", (iter < left) ? "" : " (truncated)");
}

int main(int argc, char *argv[]) {
    STAT stat = {0};
    uint8_t *data;
    long size, retn;
    FILE *file;

    if ((argc < 2) || !(file = fopen(argv[1], "rb"))) {
        printf("usage: gif_test <in>.gif [<out>.raw]\n");
        return 2;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = (uint8_t*)malloc((size_t)size);
    size = (long)fread(data, 1, (size_t)size, file);
    fclose(file);
    if (argc > 2)
        stat.dump = fopen(argv[2], "wb");
    stat.hash = 5381uL;
    stat.data = data;
    stat.size = size;
    retn = GIF_Load(data, size, Frame, Meta, (void*)&stat, 0L);
    printf("return %ld\n", retn);
    if (stat.dump)
        fclose(stat.dump);
    free(data);
    return 0;
}
