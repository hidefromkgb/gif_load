# gif_load
This is a public domain, single-header and single-call, ANSI C compatible
loader for animated GIFs. 'ANSI C compatible' means that it builds fine with
`-pedantic -ansi` compiler flags, but includes `stdint.h` unavailable prior
to C99.

There are no strict dependencies on the standard C library. The only external
function used by default is `realloc()` (both for freeing and allocation), but
it\`s possible to override it by defining a macro called `GIF_MGET(m, s, c)`
prior to including the header; `m` stands for a `uint8_t`-typed pointer to the
memory block being allocated or freed, `s` is the target block size, typed
`unsigned long`, and `c` equals 0 on freeing and 1 on allocation.

Loading GIFs immediately from disk is not supported: target files must be read
or otherwise mapped into RAM by the caller.

The main function that does the actual loading is called `GIF_Load()`.
It requires a `__cdecl`-conventioned callback function to create the animation
structure of any caller-defined format. This frame writer callback will be
executed once every frame.

Aditionally, it accepts a second callback used to process GIF
[application-specific extensions](https://stackoverflow.com/a/28486261/7019311),
i.e. metadata; in the vast majority of GIFs no such extensions are present, so
this callback is optional.

Both frame writer callback and metadata callback need 2 parameters:

1. callback-specific data
2. pointer to a `GIF_WHDR` structure that encapsulates GIF frame information
(callbacks may alter any fields at will, as the structure passed to them is a
proxy that is discarded after every call):
  * `GIF_WHDR::xdim` - global GIF width, [0; 65535]
  * `GIF_WHDR::ydim` - global GIF height, [0; 65535]
  * `GIF_WHDR::clrs` - number of colors in the current palette (often the same
                       for all frames), {2; 4; 8; 16; 32; 64; 128; 256}
  * `GIF_WHDR::bkgd` - 0-based background color index for the current palette
  * `GIF_WHDR::tran` - 0-based transparent color index for the current palette
                       (or -1 when transparency is disabled)
  * `GIF_WHDR::intr` - boolean flag indicating whether the current frame is
                  [interlaced](https://en.wikipedia.org/wiki/GIF#Interlacing);
                       deinterlacing it is up to the caller (see the example
                       below)
  * `GIF_WHDR::mode` - next frame (SIC next, not current) blending mode:
                       [`GIF_NONE`:] no blending, mainly used in single-frame
                       GIFs, functionally equivalent to `GIF_CURR`;
                       [`GIF_CURR`:] leave the current image state as is;
                       [`GIF_BKGD`:] restore the background color in the
                       boundaries of the current frame; [`GIF_PREV`:] restore
                       the image state that used to be before it was blended
                       with the current frame; *N.B.:* if right before a
                       `GIF_PREV` frame came a `GIF_BKGD` one, the state to
                       be restored is before a certain part of the resulting
                       image was filled with the background color, not after!
  * `GIF_WHDR::frxd` - current frame width, [0; 65535]
  * `GIF_WHDR::fryd` - current frame height, [0; 65535]
  * `GIF_WHDR::frxo` - current frame horizontal offset, [0; 65535]
  * `GIF_WHDR::fryo` - current frame vertical offset, [0; 65535]
  * `GIF_WHDR::time` - next frame delay in GIF time units (1 unit = 10 msec);
                       negative values are possible here, they mean that the
                       frame requires user input to advance, and the actual
                       delay equals –(`time` + 1) GIF time units: zero delay
                       + user input = wait for input indefinitely, nonzero
                       delay + user input = wait for either input or timeout
                       (whichever comes first); *N.B.:* user input requests
                       can be safely ignored, disregarding the GIF standard
  * `GIF_WHDR::ifrm` - 0-based index of the current frame
  * `GIF_WHDR::nfrm` - total frame count, negative if the GIF data supplied
                       is incomplete
  * `GIF_WHDR::bptr` - [frame writer:] pixel indices for the current frame;
                       [metadata callback:] app metadata header (8+3 bytes)
                       followed by a GIF chunk (1 byte designating length L,
                       then L bytes of metadata, and so forth; L = 0 means
                       end of chunk)
  * `GIF_WHDR::cpal` - the current palette containing 3 `uint8_t` values for
                       each of the colors: `R` for the red channel, `G` for
                       green and `B` for blue; this pointer is guaranteed
                       to be the same across frames if and only if the same
                       palette is used for those frames

`GIF_Load()`, in its turn, needs 6 parameters:

1. a pointer to GIF data in RAM
2. GIF data size; may be larger than the actual data if the GIF has a proper
   ending mark
3. a pointer to the frame writer callback
4. a pointer to the metadata callback; may be left empty
5. callback-specific data
6. number of frames to skip before executing the callback; useful to resume
   loading the partial file

Partial GIFs are also supported, but only at a granularity of one frame. For
example, if the file ends in the middle of the fifth frame, no attempt would
be made to recover the upper half, and the resulting animation will only
contain 4 frames. When more data is available, the loader might be called
again, this time with the `skip` parameter equalling 4 to skip those 4 frames.
Note that the metadata callback is not affected by `skip`, and gets called
again every time the frames between which the metadata was written are
skipped.

`gif_load` is endian-aware. To check if the target machine is big-endian,
refer to the `GIF_BIGE` compile-time boolean macro. Although GIF data is
little-endian, all multibyte integers passed to the user through `long`-typed
fields of `GIF_WHDR` have correct byte order regardless of the endianness of
the target machine. Most other data, e.g. pixel indices of a frame, consists
of single bytes and does not require swapping. One notable exception is GIF
application metadata which is passed as the raw chunk of bytes, and then it\`s
the callback\`s job to parse it and decide whether to decode and how to do
that.



# usage
Here is a simple example of how to use `GIF_Load()` to transform an animated
GIF file into a 32-bit uncompressed TGA:

```c
#include "gif_load.h"
#include <fcntl.h>
#ifdef _MSC_VER
    /** MSVC is definitely not my favourite compiler...   >_<   **/
    #pragma warning(disable:4996)
    #include <io.h>
#else
    #include <unistd.h>
#endif
#ifndef _WIN32
    #define O_BINARY 0
#endif

#pragma pack(push, 1)
typedef struct {
    void *data, *draw;
    unsigned long size;
    int uuid;
} STAT; /** #pragma avoids -Wpadded on 64-bit machines **/
#pragma pack(pop)

void Frame(void*, GIF_WHDR*); /** keeps -Wmissing-prototypes happy **/
void Frame(void *data, GIF_WHDR *whdr) {
    uint32_t *pict, x, y, yoff, iter, ifin, dsrc, ddst;
    uint8_t head[18] = {0};
    STAT *stat = (STAT*)data;

    #define BGRA(i) \
        ((uint32_t)(whdr->cpal[whdr->bptr[i]].R << ((GIF_BIGE)? 8 : 16)) | \
         (uint32_t)(whdr->cpal[whdr->bptr[i]].G << ((GIF_BIGE)? 16 : 8)) | \
         (uint32_t)(whdr->cpal[whdr->bptr[i]].B << ((GIF_BIGE)? 24 : 0)) | \
        ((whdr->bptr[i] != whdr->tran)? (GIF_BIGE)? 0xFF : 0xFF000000 : 0))
    if (!whdr->ifrm) {
        /** TGA doesn`t support heights over 0xFFFF, so we have to trim: **/
        whdr->nfrm = ((whdr->nfrm < 0)? -whdr->nfrm : whdr->nfrm) * whdr->ydim;
        whdr->nfrm = (whdr->nfrm < 0xFFFF)? whdr->nfrm : 0xFFFF;
        /** this is the very first frame, so we must write the header **/
        head[ 2] = 2;
        head[12] = (uint8_t)(whdr->xdim     );
        head[13] = (uint8_t)(whdr->xdim >> 8);
        head[14] = (uint8_t)(whdr->nfrm     );
        head[15] = (uint8_t)(whdr->nfrm >> 8);
        head[16] = 32;   /** 32 bits depth **/
        head[17] = 0x20; /** top-down flag **/
        write(stat->uuid, head, 18UL);
        stat->draw = calloc(sizeof(uint32_t),
                           (unsigned long)(whdr->xdim * whdr->ydim));
    }
    /** [TODO:] the frame is assumed to be inside global bounds,
                however it might exceed them in some GIFs; fix me. **/
    /** [TODO:] add GIF_PREV support; not widely used, but anyway. **/
    pict = (uint32_t*)stat->draw;
    ddst = (uint32_t)(whdr->xdim * whdr->fryo + whdr->frxo);
    ifin = (!(iter = (whdr->intr)? 0 : 4))? 4 : 5; /** interlacing support **/
    for (dsrc = (uint32_t)-1; iter < ifin; iter++)
        for (yoff = 16U >> ((iter > 1)? iter : 1), y = (8 >> iter) & 7;
             y < (uint32_t)whdr->fryd; y += yoff)
            for (x = 0; x < (uint32_t)whdr->frxd; x++)
                if (whdr->tran != (long)whdr->bptr[++dsrc])
                    pict[(uint32_t)whdr->xdim * y + x + ddst] = BGRA(dsrc);
    write(stat->uuid, pict, sizeof(uint32_t) * (uint32_t)whdr->xdim
                                             * (uint32_t)whdr->ydim);
    if (whdr->mode == GIF_BKGD) /** cutting a hole for the next frame **/
        for (y = 0; y < (uint32_t)whdr->fryd; y++)
            for (x = 0; x < (uint32_t)whdr->frxd; x++)
                pict[(uint32_t)whdr->xdim * y + x + ddst] = BGRA(whdr->bkgd);
    #undef BGRA
}

int main(int argc, char *argv[]) {
    STAT stat = {0};

    if (argc < 3)
        write(1, "arguments: <in>.gif <out>.tga (1 or more times)\n", 48UL);
    for (stat.uuid = 2, argc -= (~argc & 1); argc >= 3; argc -= 2) {
        if ((stat.uuid = open(argv[argc - 2], O_RDONLY | O_BINARY)) <= 0)
            return 1;
        stat.size = (unsigned long)lseek(stat.uuid, 0UL, 2 /** SEEK_END **/);
        lseek(stat.uuid, 0UL, 0 /** SEEK_SET **/);
        read(stat.uuid, stat.data = malloc(stat.size), stat.size);
        close(stat.uuid);
        unlink(argv[argc - 1]);
        stat.uuid = open(argv[argc - 1], O_CREAT | O_WRONLY | O_BINARY, 0644);
        if (stat.uuid > 0) {
            GIF_Load(stat.data, (long)stat.size, Frame, 0, (void*)&stat, 0L);
            free(stat.draw);
            close(stat.uuid);
            stat.uuid = 0;
        }
        free(stat.data);
    }
    return stat.uuid;
}
```
