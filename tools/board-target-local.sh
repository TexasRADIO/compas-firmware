#!/bin/sh
# Link the release-configuration board ELF without Docker, for code-size and
# stack work during development. Same compiler flags and link order as
# tools/board-target.sh (COMP_USB=1 COMP_EMU_STORE=1 COMP_RELEASE=1 + Kumbia).
#
#   COMP_JIELI=/path/to/jieli-toolchain COMP_BSP=/path/to/Felucca \
#   COMP_HANAN=/path/to/Hanan-extraction [COMP_STUDIO=/path/to/site-checkout] \
#   tools/board-target-local.sh OUTDIR
#
# COMP_STUDIO (the texas-radio-and-the-big-beat checkout) builds the real 727
# bank from public/bigbeat/audio/collection, every WAV checked against
# board/727-sources.json. Without it, build/standin's stand-in bank is linked
# and only code deltas are exact. Release packages still come from
# tools/board-target.sh.
set -eu
cd "$(dirname "$0")/.."
out="${1:?output directory}"
sdk="${COMP_JIELI:?Set COMP_JIELI}"
bsp="${COMP_BSP:?Set COMP_BSP}"
hanan="${COMP_HANAN:?Set COMP_HANAN}"
language="${COMPAS_LANG_DEFAULT:-0}"
bin="$sdk/pi32v2/bin"
mkdir -p "$out" build/board
python3 tools/score.py >/dev/null
python3 tools/board-piano.py --compact >/dev/null
python3 tools/board-set.py board/set.json >/dev/null
python3 tools/board-hanan.py "$hanan" >/dev/null
if [ -n "${COMP_STUDIO:-}" ]; then
    python3 tools/board-bank.py "$COMP_STUDIO" >/dev/null
else
    [ -f build/standin/727.pack ] || python3 tools/standin-bank.py build/standin >/dev/null
    cp build/standin/727.pack build/standin/727.inc build/board/
    echo "stand-in 727 bank (set COMP_STUDIO for the real one)" >&2
fi
python3 - "$bsp" <<'PY'
from pathlib import Path
import hashlib,json,sys
for file,digest in json.loads(Path('board/BSP.json').read_text())['files'].items():
    if hashlib.sha256((Path(sys.argv[1])/file).read_bytes()).hexdigest()!=digest:
        raise SystemExit('BSP hash mismatch: '+file)
PY
cflags="-target pi32v2 -mcpu=r3 -mfprev1 -Oz -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -DCOMPAS_BOARD -DCOMPAS_LANG_DEFAULT=$language -DCOMPAS_HANAN"
inc="-I$sdk/pi32v2/include -Ibuild -Ibuild/board -Iboard -Isrc -I$bsp/firmware/hal -I$bsp/firmware/src"
store="${COMP_STORE_FLAGS:--DCOMPAS_EMU_STORE -DCOMPAS_USB -DCOMPAS_UNIT_STORE}"
"$bin/clang" $cflags $inc -c src/engine.c -o "$out/engine.o"
"$bin/clang" $cflags $inc -c board/panel.c -o "$out/panel.o"
for name in board fm1 store; do
    "$bin/clang" $cflags $store $inc -c "board/$name.c" -o "$out/$name-store.o"
done
"$bin/clang" $cflags $store -fmerge-all-constants $inc -c board/piano.c -o "$out/piano-store.o"
sed "s#/generated/#$(pwd -P)/build/board/#" board/bank.S > "$out/bank.S"
for unit in "$out/bank.S" "$bsp/firmware/crt0.S" board/irq.S "$bsp/firmware/hal/fm1_vec.S"; do
    "$bin/clang" -target pi32v2 -mcpu=r3 -I"$bsp/firmware/hal" -Ibuild/board -c "$unit" -o "$out/$(basename "$unit" .S).o"
done
"$bin/ld" --gc-sections -T "$bsp/firmware/app.ld" -Map="$out/compas-store.map" -o "$out/compas-store.elf" \
    "$out/crt0.o" "$out/irq.o" "$out/fm1_vec.o" "$out/fm1-store.o" "$out/panel.o" "$out/board-store.o" \
    "$out/piano-store.o" "$out/store-store.o" "$out/engine.o" "$out/bank.o"
"$sdk/common/bin/objdump" -h -t "$out/compas-store.elf" > "$out/compas-store.sections"
echo "linked $out/compas-store.elf"
# 1.3 size gate: with the show kept on the unit, the app must end before 0x91000.
if grep -q compas_unit_store_end "$out/compas-store.sections"; then
    python3 - "$out/compas-store.sections" <<'PY'
import sys
size=sum(int(f[2],16) for f in (l.split() for l in open(sys.argv[1])) if len(f)>3 and f[1] in ('.text','.ram_text','.data'))
limit=0x91000-0x4120
print(f"app {size} B of {limit} (show kept at 0x91000): {limit-size} B free")
if size>limit: raise SystemExit("app overlaps the saved show at 0x91000")
PY
fi
