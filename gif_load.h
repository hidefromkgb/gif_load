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
#include <stdint.h> /** imports uint8_t, uint16_t and uint32_t **/
#ifndef GIF_MGET
    #include <stdlib.h>
    #define GIF_MGET(m, s, c) m = (uint8_t*)realloc((c)? 0 : m, (c)? s : 0UL)
#endif

#define GIF_BIGE (*(const uint16_t*)"\x7F\x01" == 0x7F01)
#define _GIF_SWAP(h) ((GIF_BIGE)? ((uint16_t)(h << 8) | (h >> 8)) : h)

#pragma pack(push, 1)
typedef struct {                 /** ===== GIF RGB palette element: ===== **/
    uint8_t R, G, B;             /** color values - red, green, blue      **/
} GIF_RGBX;

typedef struct {                 /** ======== frame writer info: ======== **/
    long xdim, ydim, clrs,       /** global dimensions, palette size      **/
         bkgd, tran,             /** background index, transparent index  **/
         intr, mode,             /** interlace flag, frame blending mode  **/
         frxd, fryd, frxo, fryo, /** current frame dimensions and offset  **/
         time, ifrm, nfrm;       /** delay, frame number, frame count     **/
    uint8_t *bptr;               /** frame pixel indices or metadata      **/
    GIF_RGBX *cpal;              /** current palette                      **/
} GIF_WHDR;
#pragma pack(pop)

enum {GIF_NONE = 0, GIF_CURR = 1, GIF_BKGD = 2, GIF_PREV = 3};

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
static long _GIF_LoadFrameHdr(uint8_t **buff, long *size, long flen,
                              long gflg, long fflg, GIF_RGBX **rpal) {
    const uint8_t GIF_FPAL = 0x80; /** "palette is present" flag **/
    long rclr = 0;

    if (flen && (!(*buff += flen) || ((*size -= flen) <= 0)))
        return -2;
    if (flen && (fflg & GIF_FPAL)) { /** local palette has priority **/
        *rpal = (GIF_RGBX*)*buff;
        *buff += (rclr = 2 << (fflg & 7)) * (long)sizeof(**rpal);
        if ((*size -= rclr * (long)sizeof(**rpal)) <= 0)
            return -1;
    }
    else if (gflg & GIF_FPAL) /** no local palette, using global! **/
        rclr = 2 << (gflg & 7);
    return rclr;
}

/** [ internal function, do not use ] **/
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
        return -5; /** unexpected end of the data stream **/
    mask = (GIF_H)((1 << (ccsz = (ctsz = *(*buff)++) + 1)) - 1);
    if (!(bseq = *(*buff)++))
        return -4; /** the data stream is empty **/
    if ((ctsz < 2) || (ctsz > 8))
        return -3; /** min LZW size is out of its nominal [2; 8] bounds **/
    if ((ctbl = (1UL << ctsz))
    != ((curr = (GIF_U)mask & _GIF_SWAP(*(GIF_H*)*buff))))
        return -2; /** initial code is not equal to minimum LZW size **/
    for (bszc = -ccsz, prev = iter = 0; iter < ctbl; iter++)
        code[iter] = (uint32_t)(iter << 24); /** persistent table items **/

    do { /** splitting the data stream into codes **/
        if ((*size -= bseq + 1) <= 0)
            return -5; /** unexpected end of the data stream **/
        for (; bseq > 0; bseq -= GIF_HLEN, *buff += GIF_HLEN) {
            load = (GIF_H)(_GIF_SWAP(*(GIF_H*)*buff)
                 & ((bseq < GIF_HLEN)? ((1U << (8 * bseq)) - 1U) : ~0U));
            curr |= (GIF_U)(load << (ccsz + bszc));
            load = (GIF_H)(load >> -bszc);
            bszc += 8 * ((bseq < GIF_HLEN)? bseq : GIF_HLEN);
            while (bszc >= 0) {
                if (((curr &= mask) & (GIF_U)~1) == (GIF_U)(1 << ctsz)) {
                    if (curr & 1) { /** end-of-data code (ED). **/
                        *buff += bseq; /** no end-of-stream mark after ED **/
                        (*size)--;     /**    |   ,-- decoding successful **/
                        return (*(*buff)++)? -1 : 1;
                    }
                    ctbl = (GIF_U)(1 << ctsz); /** table drop code (TD). **/
                    mask = (GIF_H)((1 << (ccsz = ctsz + 1)) - 1);
                }
                else { /** single-pixel (SP) or multi-pixel (MP) code. **/
                    /** ctbl may exceed GIF_CLEN: it can`t overflow even **/
                    /** if the frame`s 65535x65535 (max), being unsigned **/
                    if (++ctbl < GIF_CLEN) { /** is the code table full? **/
                        if ((ctbl == mask) && (ctbl < GIF_CLEN - 1)) {
                            mask = (GIF_H)(mask + mask + 1);
                            ccsz++; /** yes; extending **/
                        }
                        /** prev = TD? => curr < ctbl = prev **/
                        code[ctbl] = (uint32_t)prev + 0x1000
                                   + (code[prev] & 0xFFF000);
                    }
                    /** appending pixels decoded from SP/MP to the frame **/
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

                    if (ctbl < GIF_CLEN) /** appending the code table **/
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
    return 0; /** no ED code before end-of-stream mark; RECOVERABLE ERROR **/
}

/** _________________________________________________________________________
    The main loading function. Returns the total number of frames if the data
    includes proper GIF ending, and otherwise it returns the number of frames
    loaded per current call, multiplied by -1. So, the data may be incomplete
    and in this case the function can be called again when more data arrives,
    just remember to keep SKIP up to date.
    _________________________________________________________________________
    DATA: raw data chunk, may be partial
    SIZE: size of the data chunk that`s currently present
    GWFR: frame writer function, MANDATORY
    AMDF: metadata reader function, set to 0 if not needed
    ANIM: implementation-specific data (e.g. a structure or a pointer to it)
    SKIP: number of frames to skip before resuming
 **/
static long GIF_Load(void *data, long size, void (*gwfr)(void*, GIF_WHDR*),
                     void (*amdf)(void*, GIF_WHDR*), void *anim, long skip) {
    const long    GIF_BLEN = (1 << 12) * sizeof(uint32_t);
    const uint8_t GIF_FINT = 0x40, /** frame interlace flag               **/
                  GIF_EHDM = 0x21, /** extension header mark              **/
                  GIF_FHDM = 0x2C, /** frame header mark                  **/
                  GIF_EOFM = 0x3B, /** end-of-file mark                   **/
                  GIF_FGCM = 0xF9, /** frame graphics control mark        **/
                  GIF_AMDM = 0xFF; /** application metadata mark          **/
    #pragma pack(push, 1)
    struct GIF_GHDR {     /** ============ GIF MASTER HEADER ============ **/
        uint8_t head[6];  /** 'GIF87a' / 'GIF89a' header signature        **/
        uint16_t xdim, ydim; /** total image width, total image height    **/
        uint8_t flgs;     /** FLAGS:
                              GlobalPlt    bit 7     1: global palette exists
                                                     0: local in each frame
                              ClrRes       bit 6-4   bits/channel = ClrRes+1
                              [reserved]   bit 3     0
                              PixelBits    bit 2-0   |Plt| = 2 * 2^PixelBits
                           **/
        uint8_t bkgd;     /** background color index                      **/
        uint8_t aspr;     /** aspect ratio; usually 0                     **/
    } *ghdr = (struct GIF_GHDR*)data;
    struct GIF_FHDR {     /** ========= GIF FRAME MASTER HEADER ========= **/
        uint16_t xoff, yoff; /** offset of this frame in a "full" image   **/
        uint16_t xdim, ydim; /** frame width, frame height                **/
        uint8_t flgs;     /** FLAGS:
                              LocalPlt     bit 7     1: local palette exists
                                                     0: global is used
                              Interlaced   bit 6     1: interlaced frame
                                                     0: non-interlaced frame
                              Sorted       bit 5     usually 0
                              [reserved]   bit 4-3   [undefined]
                              PixelBits    bit 2-0   |Plt| = 2 * 2^PixelBits
                           **/
    } *fhdr;
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
    GIF_WHDR wtmp, whdr = {0};
    long desc, blen;
    uint8_t *buff;

    /** checking if the stream is not empty and has a 'GIF8[79]a' signature,
        the data has sufficient size and frameskip value is non-negative **/
    if (!ghdr || (size <= (long)sizeof(*ghdr)) || (*(buff = ghdr->head) != 71)
    || (buff[1] != 73) || (buff[2] != 70) || (buff[3] != 56) || (skip < 0)
    || ((buff[4] != 55) && (buff[4] != 57)) || (buff[5] != 97))
        return 0;

    buff = (uint8_t*)(ghdr + 1); /** skipping global header **/
    buff += (long)sizeof(*whdr.cpal) /** skipping global palette **/
         * _GIF_LoadFrameHdr(0, 0, 0, ghdr->flgs, 0, 0);
    if ((size -= buff - (uint8_t*)ghdr) <= 0)
        return 0;

    blen = size;
    whdr.bptr = buff;
    while ((desc = *whdr.bptr++) != GIF_EOFM) {
        blen--; /** frame counting loop **/
        if (desc == GIF_FHDM) {
            fhdr = (struct GIF_FHDR*)whdr.bptr;
            if (_GIF_LoadFrameHdr(&whdr.bptr, &blen, sizeof(*fhdr),
                                   ghdr->flgs, fhdr->flgs, &whdr.cpal) <= 0)
                break;
            whdr.ifrm++;
        }
        if (!_GIF_SkipChunk(&whdr.bptr, &blen))
            break;
    }
    whdr.bkgd = ghdr->bkgd;
    whdr.nfrm = (desc == GIF_EOFM)? whdr.ifrm : -whdr.ifrm;
    blen = (whdr.xdim = _GIF_SWAP(ghdr->xdim)) * (long)sizeof(*whdr.bptr)
         * (whdr.ydim = _GIF_SWAP(ghdr->ydim)) + GIF_BLEN;
    GIF_MGET(whdr.bptr, ((unsigned long)blen), 1);
    whdr.bptr += GIF_BLEN;
    whdr.ifrm = -1;
    while (skip < ((whdr.nfrm < 0)? -whdr.nfrm : whdr.nfrm)) {
        size--; /** frame extraction loop **/
        if ((desc = *buff++) == GIF_FHDM) { /** found a frame **/
            fhdr = (struct GIF_FHDR*)buff;
            whdr.frxd = _GIF_SWAP(fhdr->xdim);
            whdr.fryd = _GIF_SWAP(fhdr->ydim);
            whdr.frxo = _GIF_SWAP(fhdr->xoff);
            whdr.fryo = _GIF_SWAP(fhdr->yoff);
            whdr.intr = !!(fhdr->flgs & GIF_FINT);
            whdr.cpal = (GIF_RGBX*)(ghdr + 1);
            whdr.clrs = _GIF_LoadFrameHdr(&buff, &size, sizeof(*fhdr),
                                           ghdr->flgs, fhdr->flgs, &whdr.cpal);
            whdr.mode = (fgch && !(fgch->flgs & 0x10))?
                        (fgch->flgs & 0x0C) >> 2 : GIF_NONE;
            if (++whdr.ifrm >= skip) {
                if ((whdr.clrs <= 0)
                ||  (_GIF_LoadFrame(&buff, &size, whdr.bptr) < 0))
                    size = -(whdr.ifrm--); /** failed to extract the frame **/
                else {
                    whdr.time = (fgch)? _GIF_SWAP(fgch->time) : 0;
                    whdr.tran = (fgch && (fgch->flgs & 0x01))? fgch->tran : -1;
                    wtmp = whdr;
                    gwfr(anim, &wtmp); /** passing the frame to the caller **/
                }
            }
            if ((whdr.ifrm >= skip) && (size > 0))
                continue;
        }
        else if (desc == GIF_EHDM) { /** found an extension **/
            if (*buff == GIF_FGCM) /** frame graphics control **/
                fgch = (struct GIF_FGCH*)(buff + 1 + 1);
            else if ((*buff == GIF_AMDM) && amdf) { /** app metadata **/
                wtmp = whdr;
                wtmp.bptr = buff + 1 + 1; /** just passing the raw chunk **/
                amdf(anim, &wtmp);
            }
        }
        if ((desc == GIF_EOFM) || !_GIF_SkipChunk(&buff, &size))
            break; /** found a GIF ending mark, or there`s no data left **/
    }
    whdr.bptr -= GIF_BLEN;
    GIF_MGET(whdr.bptr, ((unsigned long)blen), 0);
    return (whdr.nfrm < 0)? -whdr.ifrm - 1 + skip : whdr.ifrm + 1;
}

#undef _GIF_SWAP
#ifdef __cplusplus
}
#endif
#endif /** GIF_LOAD_H **/
