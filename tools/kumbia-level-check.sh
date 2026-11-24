#!/bin/sh
# Run after the real-bank browser build (COMP_HANAN required for source parity).
set -eu
cd "$(dirname "$0")/.."
: "${COMP_HANAN:?Set COMP_HANAN to the retained factory extraction}"
node tests/kumbia-kit.mjs
node tests/kumbia-level.mjs
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DCOMPAS_BOARD -DCOMPAS_EMU_STORE -DCOMPAS_UNIT_STORE -DCOMPAS_HANAN \
  -Iboard -Isrc -Ibuild -Ibuild/board tests/kumbia-level-store.c src/engine.c \
  board/board.c board/panel.c board/piano.c board/store.c build/board/browser-bank.c \
  -o build/test-kumbia-level-store
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 build/test-kumbia-level-store
