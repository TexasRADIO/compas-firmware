#!/bin/sh
# Host model uses the exact linked board IRQ C code and verified PCM bytes.
set -eu
cd "$(dirname "$0")/.."
root="$(pwd -P)"
studio="${COMP_STUDIO:?Set COMP_STUDIO}"
cc="${CC:-cc}"
python3 tools/score.py
python3 tools/board-bank.py "$studio"
python3 tools/board-piano.py
python3 tools/board-set.py
cd build/board
cat > host-bank.S <<'ASM'
#if defined(__APPLE__)
    .section __TEXT,__const
    .balign 2
    .globl _compas_pcm727
_compas_pcm727:
#else
    .section .rodata,"a",@progbits
    .balign 2
    .globl compas_pcm727
compas_pcm727:
#endif
    .incbin "727.pcm"
#if defined(__APPLE__)
    .globl _compas_packed727
_compas_packed727:
#else
    .globl compas_packed727
compas_packed727:
#endif
    .incbin "727.pack"
ASM
"$cc" -c host-bank.S -o host-bank.o
for mode in timing stack sanitize; do
    case "$mode" in
        timing) flags="-fstack-usage" ;;
        stack) flags="-finstrument-functions -fno-omit-frame-pointer" ;;
        sanitize) flags="-fsanitize=address,undefined -g" ;;
    esac
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -DCOMPAS_BOARD $flags \
        -I.. -I. -I"$root/src" -I"$root/board" "$root/tests/board-host.c" \
        "$root/src/engine.c" "$root/board/board.c" "$root/board/panel.c" "$root/board/piano.c" host-bank.o -o "host-$mode"
    "./host-$mode" >"host-$mode.json"
    cat "host-$mode.json"
done
