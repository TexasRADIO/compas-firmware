#!/bin/sh
# Link the Compas board ELF. This recipe has no packaging or device steps.
set -eu
cd "$(dirname "$0")/.."
root="$(pwd -P)"
sdk="${COMP_JIELI:?Set COMP_JIELI to the PI32v2 JieLi toolchain directory}"
bsp="${COMP_BSP:-$root/release-inputs/felucca}"
studio="${COMP_STUDIO:-$root/release-inputs/studio}"
docker="${COMP_DOCKER:-docker}"
language="${COMPAS_LANG_DEFAULT:-0}"
case "$language" in
    0|1) ;;
    *) echo "COMPAS_LANG_DEFAULT must be 0 (English) or 1 (Spanish)" >&2; exit 1 ;;
esac
python3 tools/score.py
python3 tools/board-bank.py "$studio"
if [ "${COMP_USB:-0}" = 1 ]; then
    python3 tools/board-piano.py --compact
else
    python3 tools/board-piano.py
fi
python3 tools/board-set.py "${COMP_SET:-board/set.json}"
hanan="${COMP_HANAN:-$root/release-inputs/hanan}"
python3 tools/board-hanan.py "$hanan"
python3 - "$bsp" <<'PY'
from pathlib import Path
import hashlib,json,sys
for file,digest in json.loads(Path('board/BSP.json').read_text())['files'].items():
    if hashlib.sha256((Path(sys.argv[1])/file).read_bytes()).hexdigest()!=digest:
        raise SystemExit('BSP hash mismatch: '+file)
PY
"$docker" run --rm --pull=never --platform linux/amd64 --network none --cap-drop ALL --pids-limit 64 \
    -e "COMPAS_EMU_STORE_BUILD=${COMP_EMU_STORE:-0}" \
    -e "COMPAS_USB_BUILD=${COMP_USB:-0}" \
    -e "COMPAS_LANG_DEFAULT=$language" \
    -e "COMPAS_HANAN_BUILD=1" \
    -e "COMPAS_RELEASE_BUILD=${COMP_RELEASE:-0}" \
    --mount "type=bind,source=$sdk,target=/opt/jieli,readonly" \
    --mount "type=bind,source=$bsp,target=/bsp,readonly" \
    --mount "type=bind,source=$root/src,target=/src,readonly" \
    --mount "type=bind,source=$root/board,target=/board,readonly" \
    --mount "type=bind,source=$root/build/score.inc,target=/generated/score.inc,readonly" \
    --mount "type=bind,source=$root/build/panel-score.inc,target=/generated/panel-score.inc,readonly" \
    --mount "type=bind,source=$root/build/board-grooves.inc,target=/generated/board-grooves.inc,readonly" \
    --mount "type=bind,source=$root/build/board/piano.inc,target=/generated/piano.inc,readonly" \
    --mount "type=bind,source=$root/build/board/set.inc,target=/generated/set.inc,readonly" \
    --mount "type=bind,source=$root/build/board,target=/out" \
    --mount "type=bind,source=$root/build/board/727.pack,target=/generated/727.pack,readonly" \
    --mount "type=bind,source=$root/build/board/727.inc,target=/generated/727.inc,readonly" \
    sha256:2d0e361d19a6da61a8587d6c2f2c1517d0389fd72d64930cd16980f8662afac3 sh -exc '
        bin=/opt/jieli/pi32v2/bin
        sha256sum /opt/jieli/common/bin/clang "$bin/ld" /opt/jieli/common/bin/objdump > /out/toolchain.sha256
        cd /out
        if [ "$COMPAS_USB_BUILD" = 1 ] && [ "$COMPAS_EMU_STORE_BUILD" != 1 ]; then
            echo "USB review builds require COMP_EMU_STORE=1" >&2
            exit 1
        fi
        cflags="-target pi32v2 -mcpu=r3 -mfprev1 -Oz -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -DCOMPAS_BOARD -DCOMPAS_LANG_DEFAULT=$COMPAS_LANG_DEFAULT"
        if [ "$COMPAS_HANAN_BUILD" = 1 ]; then cflags="$cflags -DCOMPAS_HANAN -I/out"; fi
        for unit in /src/engine.c /board/board.c /board/piano.c /board/panel.c /board/fm1.c; do
            "$bin/clang" $cflags -I/opt/jieli/pi32v2/include -I/generated -I/board -I/src -I/bsp/firmware/hal -I/bsp/firmware/src -c "$unit" -o "$(basename "$unit" .c).o"
        done
        for unit in /board/bank.S /bsp/firmware/crt0.S /board/irq.S /bsp/firmware/hal/fm1_vec.S; do
            "$bin/clang" -target pi32v2 -mcpu=r3 -I/bsp/firmware/hal -c "$unit" -o "$(basename "$unit" .S).o"
        done
        "$bin/ld" --gc-sections -T /bsp/firmware/app.ld -Map=compas.map -o compas-offline.elf \
            crt0.o irq.o fm1_vec.o fm1.o panel.o board.o piano.o engine.o bank.o
        "$bin/clang" $cflags -DCOMPAS_EMU_STRESS -I/opt/jieli/pi32v2/include -I/generated -I/board -I/src -I/bsp/firmware/hal -I/bsp/firmware/src -c /board/fm1.c -o emu-stress.o
        "$bin/ld" --gc-sections -T /bsp/firmware/app.ld -Map=compas-stress.map -o compas-stress.elf \
            crt0.o irq.o fm1_vec.o emu-stress.o panel.o board.o piano.o engine.o bank.o
        "$bin/objdump" -h -t compas-stress.elf > compas-stress.sections
        "$bin/objdump" -d compas-offline.elf > compas.disassembly
        "$bin/objdump" -h -t compas-offline.elf > compas.sections
        if [ "$COMPAS_EMU_STORE_BUILD" = 1 ]; then
            store_flags="-DCOMPAS_EMU_STORE"
            if [ "$COMPAS_USB_BUILD" = 1 ]; then store_flags="$store_flags -DCOMPAS_USB"; fi
            # COMP_RELEASE=1: the installable release layout, with no saved show.
            if [ "$COMPAS_RELEASE_BUILD" = 1 ]; then store_flags="$store_flags -DCOMPAS_NO_SHOW_STORE"; fi
            for name in board piano fm1 store; do
                unit_flags=
                if [ "$name" = piano ] && [ "$COMPAS_USB_BUILD" = 1 ]; then
                    unit_flags=-fmerge-all-constants
                fi
                "$bin/clang" $cflags $store_flags $unit_flags -I/opt/jieli/pi32v2/include -I/generated -I/board -I/src -I/bsp/firmware/hal -I/bsp/firmware/src -c "/board/$name.c" -o "$name-store.o"
            done
            "$bin/ld" --gc-sections -T /bsp/firmware/app.ld -Map=compas-store.map -o compas-store.elf \
                crt0.o irq.o fm1_vec.o fm1-store.o panel.o board-store.o piano-store.o store-store.o engine.o bank.o
            "$bin/objdump" -h -t compas-store.elf > compas-store.sections
            "$bin/objdump" -d compas-store.elf > compas-store.disassembly
            /opt/jieli/common/bin/objcopy -O binary compas-store.elf compas-store.xip
        fi
    ' >build/board/link.log 2>&1 || { cat build/board/link.log; exit 1; }
