# gif_load
This is a public domain, single-header and single-call, ANSI C compatible
(builds fine with `-pedantic -ansi` compiler flags, but includes `stdint.h`
unavailable prior to C99) loader for animated GIFs.

There are no strict dependencies on the standard C library. The only external
function used by default is `realloc()` (both for freeing and allocation), but
it\`s possible to override it by defining a macro called `GIF_MGET(m, s, c)`
prior to including the header; `m` stands for a `uint8_t` typed pointer to the
memory block being allocated or freed, `s` is the target block size, and `c`
equals 0 on freeing and 1 on allocation.

Loading GIFs immediately from disk is not supported: target files must be read
or otherwise mapped into RAM by the caller.

The main function that does the actual loading is called `GIF_Load()`.
It requires a `__cdecl`-conventioned callback function to create the animation
structure of any caller-defined format. This callback will be executed once
every frame.

The callback needs 11 parameters:

1. GIF animation global header
2. header of the resulting frame (the one just decoded)
3. may take different values depending on the frame background mode
  * 0: no background needed (used in single-frame GIFs / first frames)
  * 1: background is a previous frame
  * 2: background is a frame before previous
  * [actual GIF_FHDR]: previous frame with a "hole" filled with the
                       color of GIF_GHDR::bkgd in this GIF_FHDR`s bounds

4. palette associated with the current (resulting) frame
5. number of colors in the palette
6. decoded array of color indices
7. callback-specific data
8. total frame count (may be partial; in this case it\`s negative)
9. transparent color index (or -1 if there\`s none)
10. next frame delay, in GIF time units (1 unit = 10 ms); can be 0
11. 0-based index of the resulting frame

`GIF_Load()`, in its turn, needs 5:

1. a pointer to GIF data in RAM
2. GIF data size; may be larger than the actual data if the GIF has a proper
   ending mark
3. number of frames to skip before executing the callback; useful to resume
   loading the partial file
4. a pointer to the callback function
5. callback-specific data

The library supports partial GIFs, but only at a frame-by-frame granularity.
For example, if the file ends in the middle of the fifth frame, no attempt
would be made to recover the upper half, and the resulting animation will
only contain 4 frames. When more data is available, the loader might be called
again, this time with skip parameter equalling 4 to skip these 4 frames.



# usage
Here is a simple example of how to use `GIF_Load()` to transform an animated
GIF file into a 32-bit uncompressed TGA:

```c
#include "gif_load.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef _WIN32
    #define O_BINARY 0
#endif

void FrameCallback(GIF_GHDR *ghdr, GIF_FHDR *curr, GIF_FHDR *prev,
                   GIF_RGBX *cpal, long clrs, uint8_t *bptr, void *data,
                   long nfrm, long tran, long time, long indx) {
    uint32_t *pict, x, y, yoff, iter, ifin, dsrc, ddst;
    uintptr_t *file = (uintptr_t*)data;
    uint8_t head[18] = {};

    #define BGRA(i) (cpal[bptr[i]].R << 16) | (cpal[bptr[i]].G << 8) \
                   | cpal[bptr[i]].B | ((i != tran)? 0xFF000000 : 0)
    if (!indx) {
        /** this is the very first frame, so we must write the header **/
        head[ 2] = 2;
        head[12] = (ghdr->xdim     ) & 0xFF;
        head[13] = (ghdr->xdim >> 8) & 0xFF;
        head[14] = ((labs(nfrm) * ghdr->ydim)     ) & 0xFF;
        head[15] = ((labs(nfrm) * ghdr->ydim) >> 8) & 0xFF;
        head[16] = 32;   /** 32 bits depth **/
        head[17] = 0x20; /** top-down flag **/
        write(file[0], head, 18);
        file[1] = (uintptr_t)calloc(ghdr->xdim * ghdr->ydim, sizeof(uint32_t));
    }
    /** interlacing support **/
    iter = (curr->flgs & GIF_FINT)? 0 : 4;
    ifin = (curr->flgs & GIF_FINT)? 4 : 5;

    pict = (uint32_t*)file[1];
    if ((uintptr_t)prev > (uintptr_t)sizeof(prev)) {
        /** background: previous frame with a hole **/
        ddst = ghdr->xdim * prev->yoff + prev->xoff;
        for (y = 0; y < prev->ydim; y++)
            for (x = 0; x < prev->xdim; x++)
                pict[ghdr->xdim * y + x + ddst] = BGRA(ghdr->bkgd);
    }
    /** [TODO:] the frame is assumed to be inside global bounds,
                however it might exceed them in some GIFs; fix me. **/
    ddst = ghdr->xdim * curr->yoff + curr->xoff;
    for (dsrc = -1; iter < ifin; iter++)
        for (yoff = 16 >> ((iter > 1)? iter : 1), y = (8 >> iter) & 7;
             y < curr->ydim; y += yoff)
            for (x = 0; x < curr->xdim; x++)
                if (tran != (long)bptr[++dsrc])
                    pict[ghdr->xdim * y + x + ddst] = BGRA(dsrc);
    write(file[0], pict, ghdr->xdim * ghdr->ydim * sizeof(uint32_t));
    #undef BGRA
}

int main(int argc, char *argv[]) {
    intptr_t file[2];
    void *data;

    if (argc < 3)
        write(1, "Input GIF and output TGA file names required!\n", 46);
    else if ((file[0] = open(argv[1], O_RDONLY | O_BINARY)) > 0) {
        file[1] = lseek(file[0], 0, SEEK_END);
        lseek(file[0], 0, SEEK_SET);
        read(file[0], data = malloc(file[1]), file[1]);
        close(file[0]);
        if ((file[0] = open(argv[2], O_CREAT | O_WRONLY | O_BINARY, 0644)) > 0) {
            GIF_Load(data, file[1], 0, FrameCallback, (void*)file);
            free((void*)file[1]); /** gets rewritten in FrameCallback() **/
            close(file[0]);
        }
        free(data);
        return 0;
    }
    return 1;
}
```
