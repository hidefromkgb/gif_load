#ifndef GIF_LOAD_H
#define GIF_LOAD_H

/** gif_load: A slim, fast and header-only GIF loader written in C.
    Original author: hidefromkgb (hidefromkgb@gmail.com)
    _________________________________________________________________________

    This is free and unencumbered software released into the public domain.

    Anyone is free to copy, modify, publish, use, compile, sell, or
    distribute this software, either in source code form or as a compiled
    binary, for any purpose, commercial or non-commercial, and by any means.

    In jurisdictions that recognize copyright laws, the author or authors
    of this software dedicate any and all copyright interest in the
    software to the public domain. We make this dedication for the benefit
    of the public at large and to the detriment of our heirs and
    successors. We intend this dedication to be an overt act of
    relinquishment in perpetuity of all present and future rights to this
    software under copyright law.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
    OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
    OTHER DEALINGS IN THE SOFTWARE.
    _________________________________________________________________________
**/

#ifdef __cplusplus
extern "C" {
#endif

#include <unistd.h>
#include <stdint.h>

#ifndef GIF_MGET
    #include <stdlib.h>
    #define GIF_MGET(m, s, c) m = (uint8_t*)realloc((c)? 0 : m, (c)? s : 0)
#endif

#define GIF_FPAL 0x80     /** Palette flag (both for GHDR and FHDR)       **/
#define GIF_FINT 0x40     /** Interlace flag (FHDR only)                  **/

#pragma pack(push, 1)
typedef struct {          /** ============ GLOBAL GIF HEADER ============ **/
    uint32_t head;        /** 'GIF8' header signature                     **/
    uint16_t type;        /** '7a' or '9a', depending on the type         **/
    uint16_t xdim, ydim;  /** total image width, total image height       **/
    uint8_t flgs;         /** FLAGS:
                              GlobalPlt    bit 7     1: global palette exists
                                                     0: local in each frame
                              ClrRes       bit 6-4   bits/channel = ClrRes+1
                              [reserved]   bit 3     0
                              PixelBits    bit 2-0   |Plt| = 2 * 2^PixelBits
                           **/
    uint8_t bkgd;         /** background color index                      **/
    uint8_t aspr;         /** aspect ratio; usually 0                     **/
} GIF_GHDR;

typedef struct {          /** ============ MAIN FRAME HEADER ============ **/
    uint16_t xoff, yoff;  /** offset of this frame in a "full" image      **/
    uint16_t xdim, ydim;  /** frame width, frame height                   **/
    uint8_t flgs;         /** FLAGS:
                              LocalPlt     bit 7     1: local palette exists
                                                     0: global is used
                              Interlaced   bit 6     1: interlaced frame
                                                     0: non-interlaced frame
                              Sorted       bit 5     usually 0
                              [reserved]   bit 4-3   [undefined]
                              PixelBits    bit 2-0   |Plt| = 2 * 2^PixelBits
                           **/
} GIF_FHDR;

typedef struct {          /** ========= GIF RGB PALETTE ELEMENT ========= **/
    uint8_t R, G, B;      /** color values: red, green, blue              **/
} GIF_RGBX;
#pragma pack(pop)



/** [ internal function, do not use ] **/
static long _GIF_SkipChunk(uint8_t **buff, long *size) {
    long skip;

    ++(*buff);
    if (--(*size) <= 0)
        return 0;
    do {
        *buff += (skip = 1 + **buff);
        if ((*size -= skip) <= 0)
            return 0;
    } while (skip > 1);
    return 1;
}

/** [ internal function, do not use ] **/
static long _GIF_LoadFrameHeader(uint8_t **buff, long *size, GIF_GHDR *ghdr,
                                 GIF_FHDR **fhdr, GIF_RGBX **rpal) {
    ssize_t rclr = 0;

    if ((*size -= (ssize_t)sizeof(**fhdr)) <= 0)
        return -2;

    *fhdr = (GIF_FHDR*)*buff;
    *buff += (ssize_t)sizeof(**fhdr);
    if ((*fhdr)->flgs & GIF_FPAL) { /** local palette always has priority **/
        *rpal = (GIF_RGBX*)*buff;
        *buff += (rclr = 2 << ((*fhdr)->flgs & 7)) * (ssize_t)sizeof(**rpal);
        if ((*size -= rclr * (ssize_t)sizeof(**rpal)) <= 0)
            return -1;
    }
    else if (ghdr->flgs & GIF_FPAL) { /** no local palette, using global! **/
        rclr = 2 << (ghdr->flgs & 7);
        *rpal = (GIF_RGBX*)(ghdr + 1);
    }
    return (long)rclr;
}

/** [ internal function, do not use ] **/
/** return values:
   -5: unexpected end of the data stream
   -4: the data stream is empty
   -3: minimum LZW size is out of its nominal [2; 8] bounds
   -2: initial code is not equal to minimum LZW size
   -1: no end-of-stream mark after end-of-data code
    0: no end-of-data code before end-of-stream mark => [RECOVERABLE ERROR]
    1: decoding successful
 **/
static long _GIF_LoadFrame(uint8_t **buff, long *size, uint8_t *bptr) {
    typedef unsigned long GIF_U; /** short alias for a very useful type **/
    typedef uint16_t GIF_H; GIF_H load, mask; /** bit accum and bitmask **/
    const long GIF_HLEN = sizeof(GIF_H); /** to rid the scope of sizeof **/
    const GIF_U GIF_CLEN = 1 << 12; /** code table length: 4096 items   **/
    GIF_U iter, /** iterator used to inflate codes into index strings   **/
          ctbl, /** last code table index (or greater, when > GIF_CLEN) **/
          curr, /** current code from the code stream                   **/
          prev; /** previous code from the code stream                  **/
    long  ctsz, /** minimum LZW code table size, in bits                **/
          ccsz, /** current code table size, in bits                    **/
          bseq, /** block sequence loop counter                         **/
          bszc; /** bit size counter                                    **/
    uint32_t *code = (uint32_t*)bptr - GIF_CLEN;

    /** preparing initial values **/
    if (--(*size) <= GIF_HLEN) /** does the size suffice? **/
        return -5;
    mask = (GIF_H)((1 << (ccsz = (ctsz = *(*buff)++) + 1)) - 1);
    if (!(bseq = *(*buff)++))
        return -4;
    if ((ctsz < 2) || (ctsz > 8))
        return -3;
    if ((curr = *(GIF_H*)*buff & mask) != (ctbl = (1UL << ctsz)))
        return -2;
    for (bszc = -ccsz, prev = iter = 0; iter < ctbl; iter++)
        code[iter] = (uint32_t)(iter << 24); /** persistent table items **/

    do { /** splitting data stream into codes **/
        if ((*size -= bseq + 1) <= 0)
            return -5;
        for (; bseq > 0; bseq -= GIF_HLEN, *buff += (ssize_t)GIF_HLEN) {
            load = (GIF_H)((bseq < GIF_HLEN)? (1 << (8 * bseq)) - 1 : ~0);
            curr |= (GIF_U)((load &= *(GIF_H*)*buff) << (ccsz + bszc));
            load = (GIF_H)(load >> -bszc);
            bszc += 8 * ((bseq < GIF_HLEN)? bseq : GIF_HLEN);
            while (bszc >= 0) {
                if (((curr &= mask) & (GIF_U)~1) == (GIF_U)(1 << ctsz)) {
                    if (curr & 1) {
                        /** 1 + (1 << ctsz): end-of-data code (ED) **/
                        (*size)--;
                        *buff += bseq;
                        return (!*(*buff)++)? 1 : -1;
                    }
                    /** 0 + (1 << ctsz): table drop code (TD) **/
                    ctbl = (GIF_U)(1 << ctsz);
                    mask = (GIF_H)((1 << (ccsz = ctsz + 1)) - 1);
                }
                else {
                    /** single-pixel code (SP) or multi-pixel code (MP) **/
                    /** ctbl may exceed GIF_CLEN, this is quite normal! **/
                    if (++ctbl < GIF_CLEN) {
                        /** does the code table exceed its bit limit? **/
                        if ((ctbl == mask) && (ctbl < GIF_CLEN - 1)) {
                            mask = (GIF_H)(mask + mask + 1);
                            ccsz++; /** yes; extending **/
                        }
                        /** prev = TD? => curr < ctbl = prev **/
                        code[ctbl] = (uint32_t)prev + 0x1000
                                   + (code[prev] & 0xFFF000);
                    }
                    /** appending pixel string to the frame **/
                    iter = (curr >= ctbl)? prev : curr;
                    bptr += (prev = (code[iter] >> 12) & 0xFFF);
                    while (!0) {
                        *bptr-- = (uint8_t)(code[iter] >> 24);
                        if (!(code[iter] & 0xFFF000))
                            break;
                        iter = code[iter] & 0xFFF;
                    }
                    bptr += prev + 2;
                    if (curr >= ctbl)
                        *bptr++ = (uint8_t)(code[iter] >> 24);

                    /** adding new code to the code table **/
                    if (ctbl < GIF_CLEN)
                        code[ctbl] |= code[iter] & 0xFF000000;
                }
                prev = curr;
                curr = load;
                bszc -= ccsz;
                load = (GIF_H)(load >> ccsz);
            }
        }
        *buff += bseq;
    } while ((bseq = *(*buff)++));
    (*size)--;
    return 0;
}



/** _________________________________________________________________________
    Decoded frame transferrer and frame delay setter. NB: it has to recompute
    interlaced pictures based on GIF_FINT flag in CURR->flgs: 1 = interlaced,
    0 = progressive.
    _________________________________________________________________________
    GHDR: animation global header
    CURR: header of the resulting frame (the one just decoded)
    PREV: may take different values depending on the frame background mode
          0: no background needed (used in single-frame GIFs / first frames)
          1: background is a previous frame
          2: background is a frame before previous
          [actual GIF_FHDR]: previous frame with a "hole" filled with the
                             color of GHDR->bkgd in this GIF_FHDR`s bounds
    CPAL: palette associated with the current (resulting) frame
    CLRS: number of colors in the palette
    BPTR: decoded array of color indices
    DATA: implementation-specific data (e.g. a structure or a pointer to it)
    NFRM: total frame count (may be partial; in this case it`s negative)
    TRAN: transparent color index (or -1 if there`s none)
    TIME: next frame delay, in GIF time units (1 unit = 10 ms); can be 0
    INDX: 0-based index of the resulting frame
 **/
typedef void (*GIF_GWFR)(GIF_GHDR *ghdr, GIF_FHDR *curr, GIF_FHDR *prev,
                         GIF_RGBX *cpal, long clrs, uint8_t *bptr, void *data,
                         long nfrm, long tran, long time, long indx);

/** _________________________________________________________________________
    The main loading function. Returns the total number of frames if the data
    includes proper GIF ending, and otherwise it returns the number of frames
    loaded per current call, multiplied by -1. So, the data may be incomplete
    and in this case the function can be called again when more data arrives,
    just remember to keep SKIP up to date.
    _________________________________________________________________________
    DATA: raw data chunk, may be partial
    SIZE: size of the data chunk that`s currently present
    SKIP: number of frames to skip before resuming
    GWFR: callback function described above
    ANIM: implementation-specific data (e.g. a structure or a pointer to it)
 **/
static long GIF_Load(void *data, long size, long skip,
                     GIF_GWFR gwfr, void *anim) {
    const    /** GIF header constant **/
    uint32_t GIF_HEAD = 'G' + 'I' * 0x100 + 'F' * 0x10000 + '8' * 0x1000000,
             GIF_BLEN = (1 << 12) * sizeof(uint32_t); /** ctbl byte size  **/
    const uint16_t GIF_TYP7 = '7' + 'a' * 0x100,      /** older GIF type  **/
                   GIF_TYP9 = '9' + 'a' * 0x100;      /** newer GIF type  **/
    const  uint8_t GIF_EHDM = 0x21, /** extension header mark             **/
                   GIF_FHDM = 0x2C, /** frame header mark                 **/
                   GIF_EOFM = 0x3B, /** end-of-file mark                  **/
                   GIF_FGCM = 0xF9; /** frame graphics control mark       **/
    #pragma pack(push, 1)
    struct GIF_FGCH {     /** ==== EXTENSION: FRAME GRAPHICS CONTROL ==== **/
        uint8_t flgs;     /** FLAGS:
                              [reserved]   bit 7-5   [undefined]
                              BlendMode    bit 4-2   000: not set; static GIF
                                                     001: leave result as is
                                                     010: restore background
                                                     011: restore previous
                                                     1--: [undefined]
                              UserInput    bit 1     1: show frame till input
                                                     0: default; ~99% of GIFs
                              TransColor   bit 0     1: got transparent color
                                                     0: frame is fully opaque
                           **/
        uint16_t time;    /** delay in GIF time units; 1 unit = 10 ms     **/
        uint8_t tran;     /** transparent color index                     **/
    } *fgch = 0;
    #pragma pack(pop)
    GIF_GHDR *ghdr = (GIF_GHDR*)data;
    GIF_FHDR *curr, *prev = 0;
    GIF_RGBX *cpal;
    size_t blen;
    long desc, ifrm, nfrm = 0;
    uint8_t *buff, *bptr;

    /** checking if the stream is not empty and has a valid signature,
        the data has sufficient size and frameskip value is non-negative **/
    if (!ghdr || (size <= (long)sizeof(*ghdr)) || (ghdr->head != GIF_HEAD)
    || ((ghdr->type != GIF_TYP7) && (ghdr->type != GIF_TYP9)) || (skip < 0))
        return 0;

    buff = (uint8_t*)(ghdr + 1); /** skipping global header **/
    if (ghdr->flgs & GIF_FPAL) /** skipping global palette if there is any **/
        buff += (2 << (ghdr->flgs & 7)) * (ssize_t)sizeof(*cpal);
    if ((size -= buff - (uint8_t*)ghdr) <= 0)
        return 0;

    ifrm = size;
    bptr = buff;
    while ((desc = *bptr++) != GIF_EOFM) { /** frame counting loop **/
        ifrm--;
        if (desc == GIF_FHDM) {
            if (_GIF_LoadFrameHeader(&bptr, &ifrm, ghdr, &curr, &cpal) <= 0)
                break;
            nfrm++;
        }
        if (!_GIF_SkipChunk(&bptr, &ifrm))
            break;
    }
    if (desc != GIF_EOFM)
        nfrm = -nfrm;

    blen = sizeof(*bptr) * ghdr->xdim * ghdr->ydim + GIF_BLEN;
    GIF_MGET(bptr, blen, 1);
    bptr += GIF_BLEN;
    ifrm = 0;
    while (skip < (nfrm < 0)? -nfrm : nfrm) { /** frame extraction loop **/
        size--;
        if ((desc = *buff++) == GIF_FHDM) { /** found a frame **/
            desc = _GIF_LoadFrameHeader(&buff, &size, ghdr, &curr, &cpal);
            /** DESC != GIF_EOFM because GIF_EOFM & (GIF_EOFM - 1) != 0 **/
            if (++ifrm > skip) {
                if ((desc > 0) && (_GIF_LoadFrame(&buff, &size, bptr) >= 0)) {
                    /** writing extracted frame to its persistent location;
                        see also TransColor in the description of FGCH.flgs **/
                    gwfr(ghdr, curr, prev, cpal, desc, bptr, anim, nfrm,
                        (long)((fgch && (fgch->flgs & 0x01))? fgch->tran : -1),
                        (long)((fgch)? fgch->time : 0), ifrm - 1);
                    /** computing blend mode for the next frame **/
                    switch ((fgch)? fgch->flgs & 0x1C : 0x00) {
                        /** restore background **/
                        case 0x08: prev = curr;         break;
                        /** restore previous   **/
                        case 0x0C: prev = (GIF_FHDR*)2; break;
                        /** leave result as is **/
                        case 0x04: prev = (GIF_FHDR*)1; break;
                        /** mode not set       **/
                        default:   prev = (GIF_FHDR*)0; break;
                    }
                    if (size > 0)
                        continue;
                }
                else { /** failed to extract ITER-th frame; exiting **/
                    desc = GIF_EOFM;
                    ifrm--;
                }
            }
        }
        else if (desc == GIF_EHDM) /** found an extension **/
            if (*buff == GIF_FGCM) {
                /** 1 byte for FGCM, 1 for chunk size (must equal 0x04)  **/
                fgch = (struct GIF_FGCH*)(buff + 1 + 1);
            }
        if ((desc == GIF_EOFM) || !_GIF_SkipChunk(&buff, &size))
            break; /** found a GIF ending mark, or there`s no data left  **/
    }
    bptr -= GIF_BLEN;
    GIF_MGET(bptr, blen, 0);
    return (nfrm < 0)? -ifrm + skip : ifrm;
}

#ifdef __cplusplus
}
#endif

#endif /** GIF_LOAD_H **/
