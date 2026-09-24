# gif_load test suite

Nothing here is needed to use `gif_load.h`; the loader is still a single
header with no dependencies. This directory only exists to keep the corner
cases that were fixed in the past from coming back.

```bash
./run.sh          # or: CC=clang ./run.sh
```

The suite needs a C compiler and a POSIX shell, nothing else. `valgrind` and
a compiler that can target 32 bits are used when they are around, and the
checks that need them are skipped when they are not, so `run.sh` is safe to
run anywhere.

### Files

| file | purpose |
|:--|:--|
| `run.sh` | the suite itself, exits with 0 when everything passed |
| `gif_test.c` | test driver: prints one line per decoded frame, may dump raw pixel indices |
| `expected-synthetic.txt` | known-good output of the driver for everything in `synthetic/` |
| `expected-real.txt` | the same for everything in `real/` |
| `sizes32.c` | compiled (never linked) for a 32-bit target, where it asserts that a 65535x65535 frame buffer — twice what a `long` holds there — plus the code table cannot wrap an `unsigned long`. `GIF_Load()` relies on that instead of checking, so this is the only place the invariant is stated |
| `valgrind.supp` | hides the `realloc(m, 0)` the default `GIF_MGET` frees with |
| `real/` | real-world GIFs, checked against `expected-real.txt` and under valgrind. They cover what hand-built files cannot: real encoders' output, hundreds of local palettes, interlacing in the wild, and sheer size |
| `synthetic/` | hand-built GIFs, each aimed at one corner case, checked against `expected-synthetic.txt` and under valgrind |

### What the GIFs cover

| file | case |
|:--|:--|
| `normal.gif` | two 4x4 frames, global palette, delays, transparency, a local palette and `GIF_BKGD` |
| `interlaced.gif` | one 64x64 interlaced frame of 256-color noise: exercises LZW code size growth; its pixel indices have to come back out bit for bit, see `interlaced.raw` |
| `truncated.gif` | the first 3/5 of `normal.gif`: one frame plus a negative frame count |
| `short.gif` | a 16x16 frame whose LZW stream ends after a single pixel; the other 255 must read back as zeroes instead of uninitialized heap |
| `stale.gif` | a full 4x4 frame followed by one that decodes a single pixel: the 15 that are missing must come back as zeroes, not as the pixels the previous frame left in the buffer |
| `straddle.gif` | a 5x1 frame whose last string runs past its final pixel: such a string is skipped whole, and the pixels it would have covered have to be padded rather than left as they were |
| `nohdr.gif` | a GIF that ends on the frame marker, with none of the nine header bytes after it: reading the flags out of a header that is not there runs past the end of the caller buffer, and segfaults when the data ends on a page boundary |
| `zerowidth.gif` | a frame that declares no width at all: it carries no pixel the caller could ever look at, and must not upset the file around it |
| `localpal.gif` | no global palette, but a local one, so the loader has to fall back to the palette that is actually there |
| `nulcomment.gif` | a comment holding a NUL byte: sub-blocks are walked by length, so a zero inside one is data rather than the end of the chain |
| `gif87a.gif` | several frames under a GIF87a signature, which has no graphics control extensions to delimit them |
| `manyclears.gif` | a clear code before every single pixel, so the code table is dropped as often as it is grown |
| `extradata.gif` | another sub-block after the end-of-information code, which the loader refuses: that is the only case producing its "no end-of-stream mark after ED" error |
| `hiddendata.gif` | junk riding inside the very sub-block the stream ended in, which the loader accepts and steps over |
| `staletbl.gif` | one 512x512 frame that fills the code table, then a 2x2 one that overruns its four pixels and only then names codes 4095 and 4094, which belong to the frame before it: cross-frame table state must not reach the output |
| `mangled.gif` | a 2x2 frame carrying 8 pixels and then a code the table never defined, which the decoder ends up using as an index into that table; only valgrind catches this one, the decoded output looks the same either way |
| `nopal.gif` | no global and no local palette, which the standard permits: the frame has to decode anyway, reported as `clrs == 0` with a NULL `cpal`, leaving the caller with bare pixel indices (issue #10) |
| `overflow.gif` | frames of 65535x1 and 1x65535: 64 KB of frame buffer, unless it gets sized as the largest width times the largest height, which would be 4 GB and would not even fit into a 32-bit `long` (issue #14) |
| `huge.gif` | one 65535x65535 frame, which really does need those 4 GB: under `ulimit -v` the allocation fails and `GIF_Load()` has to return 0 instead of dereferencing NULL (issue #14) |

`short.gif` usually looks fine without valgrind, because a fresh `malloc()`
tends to hand out zeroed pages; the check that matters there is the valgrind
one, which sees the uninitialized read whatever the heap happens to contain.

### Adding a case

Drop the GIF into `real/` or `synthetic/`, describe it in the table above,
and refresh the matching expected file. Both are picked up by a glob, so
`run.sh` itself needs no editing:

```bash
cc -O2 -o /tmp/gif_test gif_test.c
for GIF in synthetic/*.gif; do
    case $GIF in */huge.gif) continue;; esac
    echo "### ${GIF#synthetic/}"; /tmp/gif_test "$GIF"
done > expected-synthetic.txt
for GIF in real/*.gif; do
    echo "### ${GIF#real/}"; /tmp/gif_test "$GIF"
done > expected-real.txt
```
