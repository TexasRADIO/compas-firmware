#!/bin/sh
# Compile and exercise an offline RAM-only provisioning fixture; no devices.
set -eu
cd "$(dirname "$0")/.."
root="$(pwd -P)"
: "${COMP_BSP:?Set COMP_BSP}" "${COMP_JIELI:?Set COMP_JIELI}" "${COMP_PROVISION_EMU:?Set the pinned emulator}" \
  "${COMP_PROVISION_PRIOR:?Set the prior board directory}" "${COMP_PROVISION_PRIOR_RECEIPT:?Set its receipt}" \
  "${COMP_PROVISION_RECEIPT:?Set the current receipt}"
work="$(mktemp -d "$root/build/board/.provision-link.XXXXXX")"
trap 'rm -rf "$work"' EXIT
python3 tools/board-provision-input.py "$work"
"${COMP_DOCKER:-docker}" run --rm --pull=never --platform linux/amd64 --network none --cap-drop ALL --pids-limit 64 \
  --mount "type=bind,source=$COMP_JIELI,target=/opt/jieli,readonly" \
  --mount "type=bind,source=$work,target=/work" -w /work \
  sha256:2d0e361d19a6da61a8587d6c2f2c1517d0389fd72d64930cd16980f8662afac3 sh -exc '
    bin=/opt/jieli/pi32v2/bin
    sha256sum /opt/jieli/common/bin/clang "$bin/ld" > toolchain.sha256
    "$bin/clang" -target pi32v2 -mcpu=r3 -c firmware/loader/crt0_ldr.S -o crt0.o
    for name in provision fixture; do
      "$bin/clang" -target pi32v2 -mcpu=r3 -Os -std=c11 -ffunction-sections -fno-builtin \
        -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -I. -Ifirmware/hal -c "$name.c" -o "$name.o"
    done
    "$bin/ld" --gc-sections -e _start -T firmware/loader/loader.ld -Map=provision.map crt0.o provision.o fixture.o -o provision.elf
    "$bin/objdump" -h -t -d provision.elf > provision.dis
    /opt/jieli/common/bin/objcopy -O binary -j .text provision.elf loader.bin
    "$bin/clang" -target pi32v2 -mcpu=r3 -c entry.S -o entry.o
    "$bin/ld" -Ttext 0x020BC000 -e _start entry.o -o entry.elf
    /opt/jieli/common/bin/objcopy -O binary -j .text entry.elf entry.bin
  ' > "$work/link.log" 2>&1 || { cat "$work/link.log"; exit 1; }
python3 tools/board-provision-target-emu.py "$work" "$COMP_PROVISION_EMU" > "$work/receipt.json"
mkdir -p build/board/provision-target
for name in receipt.json provision.map provision.dis toolchain.sha256 link.log; do
  cp "$work/$name" "build/board/provision-target/$name"
done
cat "$work/receipt.json"
