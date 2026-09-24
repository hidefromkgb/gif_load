/** Builds the header for a 32-bit target and checks, at compile time, the
    invariant the frame buffer allocation rests on: the largest frame a GIF
    can declare is 65535 x 65535, and adding the code table and the two
    bytes of slack to that must not wrap an unsigned long, which is only
    guaranteed to be 32 bits wide. There is no runtime check for this in
    GIF_Load(), because the GIF format cannot express anything bigger.

    Needs no libc, hence the GIF_MGET override: 32-bit headers are often
    absent on 64-bit machines even when the compiler can target 32 bits.
    Compiled, never linked; see run.sh.
 **/
#define GIF_MGET(m,s,a,c) m = (uint8_t*)my_alloc(m, s, c);
extern unsigned char *my_alloc(unsigned char *m, unsigned long s, int c);
#include "../gif_load.h"

/** C89 compile-time assertion: a negative array size fails the build **/
#define CHECK(name, cond) extern int name[(cond)? 1 : -1]

#define GIF_MAXPX (65535uL * 65535uL)          /** largest frame, in pixels **/
#define GIF_TABLE ((1uL << 12) * sizeof(uint32_t))  /** the LZW code table  **/

CHECK(target_is_32_bit,    sizeof(long) == 4);
CHECK(ulong_is_32_bit,     sizeof(unsigned long) == 4);
CHECK(largest_frame_fits,  GIF_MAXPX <= ~0uL);
/** sic: written as a subtraction, as the addition itself would wrap **/
CHECK(alloc_cannot_wrap,   GIF_MAXPX <= ~0uL - GIF_TABLE - 2uL);
/** and all of that above the 2 GB a signed long would have stopped at **/
CHECK(reaches_beyond_2gb,  GIF_MAXPX > 2147483647uL);
