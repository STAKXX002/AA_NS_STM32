#!/bin/sh
# Host-side logic tests: builds the real firmware modules against a fake HAL
# and runs them. Needs only gcc.   Usage: ./host_tests/run.sh
#
# The sources are staged into build/host first because a quoted
# #include "main.h" always finds the CubeMX main.h next to the file that
# includes it; staging leaves that header out so the stub is used instead.
set -e
cd "$(dirname "$0")/.."
rm -rf build/host && mkdir -p build/host/inc build/host/src
for h in Core/Inc/*.h; do
    [ "$(basename "$h")" = main.h ] || cp "$h" build/host/inc/
done
cp host_tests/stubs/main.h build/host/inc/
for c in axis alignment commands hatch relay system; do cp Core/Src/$c.c build/host/src/; done
cp host_tests/test_aa.c build/host/src/
# --wrap lets test_aa.c run the tick ISR between two accessor calls made by
# alignment.c (see t_isr_between_reads), the way a real interrupt can land.
gcc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -Ibuild/host/inc build/host/src/*.c -o build/host/test_aa \
    -Wl,--wrap=axis_done -Wl,--wrap=axis_stop_reason \
    -Wl,--wrap=axis_min_active -Wl,--wrap=axis_max_active
./build/host/test_aa
