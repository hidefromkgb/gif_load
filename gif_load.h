#ifndef GIF_LOAD_H
#define GIF_LOAD_H

#include <stdint.h>

#ifndef GIF_MGET
    #include <malloc.h>
    #define GIF_MGET(m, s, c) m = realloc((c)? 0 : m, (c)? s : 0)
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
static long GIF_SkipChunk(uint8_t **buff, long *size) {
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
static long GIF_LoadFrameHeader(uint8_t **buff, long *size, GIF_GHDR *ghdr,
                                GIF_FHDR **fhdr, GIF_RGBX **rpal) {
    long rclr = 0;

    if ((*size -= sizeof(**fhdr)) <= 0)
        return -2;

    *fhdr = (GIF_FHDR*)*buff;
    *buff += sizeof(**fhdr);
    if ((*fhdr)->flgs & GIF_FPAL) {
        /** local palette always has a priority over global **/
        *rpal = (GIF_RGBX*)*buff;
        *buff += (rclr = 2 << ((*fhdr)->flgs & 7)) * sizeof(**rpal);
        if ((*size -= rclr * sizeof(**rpal)) <= 0)
            return -1;
    }
    else if (ghdr->flgs & GIF_FPAL) {
        /** no local palette, using global **/
        rclr = 2 << (ghdr->flgs & 7);
        *rpal = (GIF_RGBX*)(ghdr + 1);
    }
    return rclr;
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
static long GIF_LoadFrame(uint8_t **buff, long *size, uint8_t *bptr) {
    const long GIF_CLEN = (1 << 12); /** code table length (4096 items) **/
    long iter, /** loop iterator for expanding codes into index strings **/
         bseq, /** block sequence loop counter                          **/
         bszc, /** bit size counter                                     **/
         ctbl, /** code table counter, points to the last element       **/
         curr, /** current code from the code stream                    **/
         prev, /** previous code from the code stream                   **/
         ctsz, /** minimum LZW code table size, in bits                 **/
         ccsz; /** current code table size, in bits                     **/
    #define EMT_TYPE uint16_t
    EMT_TYPE load, mask;
    uint32_t *code;

    /** does the size suffice our needs? **/
    if (--(*size) <= sizeof(load))
        return -5;

    /** preparing initial values **/
    ctsz = *(*buff)++;
    if (!(bseq = *(*buff)++))
        return -4;
    if ((ctsz < 2) || (ctsz > 8))
        return -3;

    mask =  (1 << (ccsz = ctsz + 1)) - 1;
    curr = *(EMT_TYPE*)*buff & mask;
    bszc =  -ccsz;
    prev =   0;

    if (curr != (ctbl = (1 << ctsz)))
        return -2;

    /** filling persistent part of the code table **/
    for (code = (uint32_t*)bptr - GIF_CLEN, iter = 0; iter < ctbl; iter++)
        code[iter] = iter << 24;

    /** splitting data stream into codes **/
    do {
        if ((*size -= bseq + 1) <= 0)
            return -5;
        for (; bseq > 0; bseq -= sizeof(load), *buff += sizeof(load)) {
            load = *(EMT_TYPE*)*buff;

            if (bseq < sizeof(load))
                load &= (1 << (8 * bseq)) - 1;

            curr |= load << (ccsz + bszc);
            load >>= -bszc;
            bszc += 8 * ((bseq < sizeof(load))? bseq : sizeof(load));

            while (bszc >= 0) {
                curr &= mask;
                if ((curr & -2) == (1 << ctsz)) {
                    /** 1 + (1 << ctsz): end-of-data code (ED) **/
                    if (curr & 1) {
                        (*size)--;
                        *buff += bseq;
                        return (!*(*buff)++)? 1 : -1;
                    }
                    /** 0 + (1 << ctsz): table drop code (DT); ED = DT + 1 **/
                    ctbl = (1 << ctsz);
                    mask = (1 << (ccsz = ctsz + 1)) - 1;
                }
                else {
                    /** single-pixel code (SP) or multi-pixel code (MP) **/
                    if (ctbl < GIF_CLEN - 1) {
                        /** prev = DT? => curr < ctbl = prev **/
                        code[++ctbl] = prev + (code[prev] & 0xFFF000) + 0x1000;
                    }
                    /** appending pixel string to the frame **/
                    iter = (curr >= ctbl)? prev : curr;
                    bptr += (prev = (code[iter] >> 12) & 0xFFF);
                    while (!0) {
                        *bptr-- = code[iter] >> 24;
                        if (!(code[iter] & 0xFFF000))
                            break;
                        iter = code[iter] & 0xFFF;
                    }
                    bptr += prev + 2;
                    if (curr >= ctbl)
                        *bptr++ = code[iter] >> 24;

                    /** adding new code to the code table **/
                    if (ctbl < GIF_CLEN)
                        code[ctbl] |= code[iter] & 0xFF000000;
                }
                /** does the code table exceed its bit limit? **/
                if ((ctbl == mask) && (ctbl < GIF_CLEN - 1)) {
                    /** yes; extending **/
                    mask += mask + 1;
                    ccsz++;
                }
                prev = curr;
                curr = load;
                load >>= ccsz;
                bszc -= ccsz;
            }
        }
        *buff += bseq;
    } while ((bseq = *(*buff)++));
    (*size)--;
    return 0;
    #undef EMT_TYPE
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
    };
    struct GIF_FGCH *fgch = 0;
    #pragma pack(pop)

    const    /** GIF header constant **/
    uint32_t GIF_HEAD = 'G' + 'I' * 0x100 + 'F' * 0x10000 + '8' * 0x1000000,
             GIF_BLEN = (1 << 12) * sizeof(uint32_t); /** ctbl byte size  **/
    const uint16_t GIF_TYP7 = '7' + 'a' * 0x100,      /** older GIF type  **/
                   GIF_TYP9 = '9' + 'a' * 0x100;      /** newer GIF type  **/
    const  uint8_t GIF_EHDM = 0x21, /** extension header mark             **/
                   GIF_FHDM = 0x2C, /** frame header mark                 **/
                   GIF_EOFM = 0x3B, /** end-of-file mark                  **/
                   GIF_FGCM = 0xF9; /** frame graphics control mark       **/

    GIF_GHDR *ghdr = (GIF_GHDR*)data;
    GIF_FHDR *curr, *prev = 0;
    GIF_RGBX *cpal;

    long desc, blen, ifrm, nfrm = 0;
    uint8_t *buff, *bptr;

    /** checking if the stream is not empty and has a valid signature,
        the data has sufficient size and frameskip value is non-negative **/
    if (!ghdr || (size <= sizeof(*ghdr)) || (ghdr->head != GIF_HEAD)
    || ((ghdr->type != GIF_TYP7) && (ghdr->type != GIF_TYP9)) || (skip < 0))
        return 0;

    /** skipping the global header **/
    buff = (uint8_t*)(ghdr + 1);
    /** skipping the global palette (if there is any) **/
    if (ghdr->flgs & GIF_FPAL)
        buff += (2 << (ghdr->flgs & 7)) * sizeof(*cpal);
    if ((size -= buff - (uint8_t*)ghdr) <= 0)
        return 0;

    /** counting frames **/
    ifrm = size;
    bptr = buff;
    while ((desc = *bptr++) != GIF_EOFM) {
        ifrm--;
        if (desc == GIF_FHDM) {
            if (GIF_LoadFrameHeader(&bptr, &ifrm, ghdr, &curr, &cpal) <= 0)
                break;
            nfrm++;
        }
        if (!GIF_SkipChunk(&bptr, &ifrm))
            break;
    }
    if (desc != GIF_EOFM)
        nfrm = -nfrm;

    /** extracting frames **/
    blen = ghdr->xdim * ghdr->ydim * sizeof(*bptr) + GIF_BLEN;
    GIF_MGET(bptr, blen, 1);
    bptr += GIF_BLEN;
    ifrm = 0;
    while (skip < (nfrm < 0)? -nfrm : nfrm) {
        size--;
        desc = *buff++;
        /** found a frame **/
        if (desc == GIF_FHDM) {
            desc = GIF_LoadFrameHeader(&buff, &size, ghdr, &curr, &cpal);
            /** DESC != GIF_EOFM because GIF_EOFM & (GIF_EOFM - 1) != 0 **/
            if (++ifrm > skip) {
                if ((desc > 0) && (GIF_LoadFrame(&buff, &size, bptr) >= 0)) {
                    /** writing extracted frame to its persistent location;
                        see also TransColor in the description of FGCH.flgs **/
                    gwfr(ghdr, curr, prev, cpal, desc, bptr, anim, nfrm,
                        (fgch && (fgch->flgs & 0x01))? fgch->tran : -1,
                        (fgch)? fgch->time : 0, ifrm - 1);
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
                else {
                    /** failed to extract ITER-th frame; exiting **/
                    desc = GIF_EOFM;
                    ifrm--;
                }
            }
        }
        /** found an extension **/
        else if (desc == GIF_EHDM)
            if (*buff == GIF_FGCM) {
                /** 1 byte for FGCM, 1 for chunk size (must equal 0x04)  **/
                fgch = (struct GIF_FGCH*)(buff + 1 + 1);
            }
        /** found a valid GIF ending mark (or there`s no more data left) **/
        if ((desc == GIF_EOFM) || !GIF_SkipChunk(&buff, &size))
            break;
    }
    bptr -= GIF_BLEN;
    GIF_MGET(bptr, blen, 0);
    return (nfrm < 0)? -ifrm + skip : ifrm;
}

#endif /** GIF_LOAD_H **/
