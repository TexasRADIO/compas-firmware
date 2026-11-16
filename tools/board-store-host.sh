#!/bin/sh
# Requires the generated real-bank host object from tools/board-host.sh.
set -eu
cd "$(dirname "$0")/.."
for mode in normal sanitize; do
    flags=""
    if [ "$mode" = sanitize ]; then flags="-fsanitize=address,undefined -g"; fi
    "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror $flags -DCOMPAS_BOARD -DCOMPAS_EMU_STORE \
        -Iboard -Isrc -Ibuild -Ibuild/board tests/board-store.c board/store.c board/board.c \
        board/piano.c src/engine.c build/board/host-bank.o -o "build/board/store-host-$mode"
    "build/board/store-host-$mode"
done
