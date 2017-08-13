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
#include <unistd.h>
#include <fcntl.h>

void NewFrame(GIF_GHDR *ghdr, GIF_FHDR *curr, GIF_FHDR *prev, GIF_RGBX *cpal,
              long clrs, uint8_t *bptr, void *data, long nfrm, long tran,
              long time, long indx);

#ifndef _WIN32
    #define O_BINARY 0
#endif

#pragma pack(push, 1)
typedef struct {
    void *data, *draw;
    size_t size;
    int uuid;
} STAT;
#pragma pack(pop)

void NewFrame(GIF_GHDR *ghdr, GIF_FHDR *curr, GIF_FHDR *prev, GIF_RGBX *cpal,
              long clrs, uint8_t *bptr, void *data, long nfrm, long tran,
              long time, long indx) {
    uint32_t *pict, x, y, yoff, iter, ifin, dsrc, ddst;
    uint8_t head[18] = {0};
    STAT *stat = (STAT*)data;
    (void)clrs; (void)time;
    #define BGRA(i) ((cpal[bptr[i]].G << 8) | (cpal[bptr[i]].R << 16) \
                    | cpal[bptr[i]].B | (((bptr[i] - tran)? 255 : 0) << 24))
    if (!indx) {
        /** TGA doesn`t support heights over 0xFFFF, so we have to trim: **/
        nfrm = labs(nfrm) * ghdr->ydim;
        nfrm = (nfrm < 0xFFFF)? nfrm : 0xFFFF;
        /** this is the very first frame, so we must write the header **/
        head[ 2] = 2;
        head[12] = (uint8_t)(ghdr->xdim     );
        head[13] = (uint8_t)(ghdr->xdim >> 8);
        head[14] = (uint8_t)(nfrm     );
        head[15] = (uint8_t)(nfrm >> 8);
        head[16] = 32;   /** 32 bits depth **/
        head[17] = 0x20; /** top-down flag **/
        write(stat->uuid, head, (size_t)18);
        stat->draw = calloc((size_t)ghdr->xdim * ghdr->ydim, sizeof(uint32_t));
    }
    /** interlacing support **/
    iter = (curr->flgs & GIF_FINT)? 0 : 4;
    ifin = (curr->flgs & GIF_FINT)? 4 : 5;

    pict = (uint32_t*)stat->draw;
    if ((uintptr_t)prev > (uintptr_t)sizeof(prev)) {
        /** background: previous frame with a hole **/
        ddst = (uint32_t)ghdr->xdim * prev->yoff + prev->xoff;
        for (y = 0; y < prev->ydim; y++)
            for (x = 0; x < prev->xdim; x++)
                pict[ghdr->xdim * y + x + ddst] = (uint32_t)BGRA(ghdr->bkgd);
    }
    /** [TODO:] the frame is assumed to be inside global bounds,
                however it might exceed them in some GIFs; fix me. **/
    ddst = (uint32_t)ghdr->xdim * curr->yoff + curr->xoff;
    for (dsrc = (uint32_t)-1; iter < ifin; iter++)
        for (yoff = 16U >> ((iter > 1)? iter : 1), y = (8 >> iter) & 7;
             y < curr->ydim; y += yoff)
            for (x = 0; x < curr->xdim; x++)
                if (tran != (long)bptr[++dsrc])
                    pict[ghdr->xdim * y + x + ddst] = (uint32_t)BGRA(dsrc);
    write(stat->uuid, pict,
         (size_t)ghdr->xdim * ghdr->ydim * sizeof(uint32_t));
    #undef BGRA
}

int main(int argc, char *argv[]) {
    STAT stat = {0};

    if (argc < 3)
        write(1, "params: <in>.gif <out>.tga (1 or more times)\n", (size_t)45);
    for (stat.uuid = 2, argc -= (~argc & 1); argc >= 3; argc -= 2) {
        if ((stat.uuid = open(argv[argc - 2], O_RDONLY | O_BINARY)) <= 0)
            return 1;
        stat.size = (size_t)lseek(stat.uuid, (off_t)0, SEEK_END);
        lseek(stat.uuid, (off_t)0, SEEK_SET);
        read(stat.uuid, stat.data = malloc(stat.size), stat.size);
        close(stat.uuid);
        unlink(argv[argc - 1]);
        stat.uuid = open(argv[argc - 1], O_CREAT | O_WRONLY | O_BINARY, 0644);
        if (stat.uuid > 0) {
            GIF_Load(stat.data, (long)stat.size, 0L, NewFrame, (void*)&stat);
            free(stat.draw);
            close(stat.uuid);
            stat.uuid = 0;
        }
        free(stat.data);
    }
    return stat.uuid;
}
```
