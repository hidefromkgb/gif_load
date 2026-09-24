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
    (void)data;
    printf("meta %c%c%c%c%c%c%c%c\n", whdr->bptr[0], whdr->bptr[1],
           whdr->bptr[2], whdr->bptr[3], whdr->bptr[4], whdr->bptr[5],
           whdr->bptr[6], whdr->bptr[7]);
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
    retn = GIF_Load(data, size, Frame, Meta, (void*)&stat, 0L);
    printf("return %ld\n", retn);
    if (stat.dump)
        fclose(stat.dump);
    free(data);
    return 0;
}
