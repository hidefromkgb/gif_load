#!/bin/sh
# gif_load test suite. Usage: ./run.sh   (honours $CC, defaults to cc)
# Exits with 0 if everything passed, 1 otherwise.

cd "$(dirname "$0")" || exit 1
CC=${CC:-cc}
WORK=$(mktemp -d) || exit 1
trap 'rm -rf "$WORK"' EXIT
FAIL=0

pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; FAIL=1; }
skip() { echo "SKIP  $1 ($2)"; }

# 1. the driver has to build without a single warning
if $CC -O2 -std=c89 -pedantic -Wall -Wextra -Wno-long-long \
       -o "$WORK/gif_test" gif_test.c 2> "$WORK/cc.log" && \
   [ ! -s "$WORK/cc.log" ]; then
    pass "builds clean with -std=c89 -pedantic -Wall -Wextra"
else
    fail "builds clean with -std=c89 -pedantic -Wall -Wextra"
    cat "$WORK/cc.log"
    [ -x "$WORK/gif_test" ] || exit 1
fi

# 1b. the header has to build for a 32-bit target, where a frame buffer of
#     up to 65535 x 65535 bytes still has to be expressible: that is twice
#     what a long holds there. sizes32.c asserts the invariant that lets
#     GIF_Load() add the code table to that size without checking first;
#     it is compiled, never linked
printf 'int probe;\n' > "$WORK/probe.c"
if $CC -m32 -ffreestanding -c "$WORK/probe.c" -o "$WORK/probe.o" \
       > /dev/null 2>&1; then
    if $CC -m32 -ffreestanding -c -std=c89 -pedantic -Wall -Wextra \
           -Wno-unused-function sizes32.c -o "$WORK/sizes32.o" \
           2> "$WORK/m32.log" && [ ! -s "$WORK/m32.log" ]; then
        pass "32-bit build: frame buffer sizes stay wrap-free past 2 GB"
    else
        fail "32-bit build: frame buffer sizes stay wrap-free past 2 GB"
        cat "$WORK/m32.log"
    fi
else
    skip "32-bit build" "no -m32 support in $CC"
fi

# 3. decoding has to produce exactly the known-good frame descriptions.
#    Both lists come from a glob, so dropping a GIF in and refreshing the
#    matching expected file is all it takes to widen the net. huge.gif is
#    left out here and handled under an address space limit further down
check_dir() { # <directory> <expected file> <label>
    for GIF in "$1"/*.gif; do
        case $GIF in */huge.gif) continue;; esac
        echo "### ${GIF#$1/}" >> "$WORK/$1.out"
        "$WORK/gif_test" "$GIF" >> "$WORK/$1.out" 2>&1
    done
    if diff -u "$2" "$WORK/$1.out" > "$WORK/$1.diff"; then
        pass "$3"
    else
        fail "$3"
        head -40 "$WORK/$1.diff"
    fi
}
check_dir synthetic expected-synthetic.txt "hand-built GIFs decode as expected"
check_dir real expected-real.txt "real-world GIFs decode as expected"

# 4. an interlaced 256-color frame has to survive an encode / decode round trip
"$WORK/gif_test" synthetic/interlaced.gif "$WORK/il.raw" > /dev/null 2>&1
if cmp -s "$WORK/il.raw" synthetic/interlaced.raw; then
    pass "interlaced 256-color round trip"
else
    fail "interlaced 256-color round trip"
fi

# 5. issue #14: a failing frame buffer allocation must not crash the process.
#    huge.gif holds a single 65535x65535 frame, so the allocation does fail
#    under the address space limit below (and on 32-bit platforms the size
#    does not even fit into the signed long it used to be computed in)
if ( ulimit -v 524288 ) 2> /dev/null; then
    OUT=$( ulimit -v 524288; "$WORK/gif_test" synthetic/huge.gif 2>&1 )
    CODE=$?
    if [ $CODE -eq 0 ] && [ "$OUT" = "return 0" ]; then
        pass "issue #14: huge frame buffer is rejected, not dereferenced"
    else
        fail "issue #14: huge frame buffer is rejected, not dereferenced"
        echo "  exit=$CODE output=[$OUT]"
    fi
else
    skip "issue #14: huge frame buffer" "no 'ulimit -v' here"
fi

# 6. valgrind, when available, over every GIF but the 4 GB one
if command -v valgrind > /dev/null 2>&1; then
    ERRS=0
    for GIF in synthetic/*.gif real/*.gif; do
        case $GIF in *huge.gif) continue;; esac
        # valgrind.supp hides the realloc(m, 0) the default GIF_MGET frees with
        valgrind -q --error-exitcode=9 --track-origins=yes \
                 --suppressions=valgrind.supp \
                 "$WORK/gif_test" "$GIF" > /dev/null 2> "$WORK/vg.log" || {
            ERRS=1; echo "  $GIF:"; cat "$WORK/vg.log"; }
    done
    [ $ERRS -eq 0 ] && pass "valgrind is clean" || fail "valgrind is clean"
else
    skip "valgrind is clean" "no valgrind"
fi

[ $FAIL -eq 0 ] && echo "--- all tests passed" || echo "--- FAILURES"
exit $FAIL
