#!/bin/sh
# Link a RAM-only recovery loader for offline inspection. Retain only evidence.
set -eu
cd "$(dirname "$0")/.."
root="$(pwd -P)"
bsp="${COMP_BSP:?Set COMP_BSP}"
sdk="${COMP_JIELI:?Set COMP_JIELI}"
docker="${COMP_DOCKER:-docker}"
if [ -n "${COMP_BB1_INSTALLER:-}" ]; then
    : "${COMP_LOADER_EMU:?Set the emulator for BB1 host replay}"       "${COMP_LOADER_REHEARSAL:?Set the candidate strict rehearsal receipt for BB1 host replay}"
fi
mkdir -p build/board
evidence="$root/build/board"
profile_flags=""
if [ "${COMP_LOADER_PROVISION_WIRE:-0}" = 1 ]; then export COMP_LOADER_PROVISION=1; fi
if [ "${COMP_LOADER_PROVISION:-0}" = 1 ]; then
    : "${COMP_PROVISION_EMU:?Set pinned emulator}" "${COMP_PROVISION_PRIOR:?Set prior board}" \
      "${COMP_PROVISION_PRIOR_RECEIPT:?Set prior receipt}" "${COMP_PROVISION_RECEIPT:?Set current receipt}"
    export COMP_LOADER_SHOW=1
fi
if [ "${COMP_LOADER_SHOW:-0}" = 1 ]; then
    evidence="$root/build/board/show-loader"
    mkdir -p "$evidence"
    profile_flags="-DCOMPAS_PRESERVE_SHOW"
fi
work="$(mktemp -d "$root/build/board/.loader-link.XXXXXX")"
trap 'rm -rf "$work"' EXIT
python3 - "$bsp" "$work" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

bsp, work = map(Path, sys.argv[1:])
pins = json.loads(Path("board/BSP.json").read_text())
sources = (
    "firmware/loader/crt0_ldr.S", "firmware/loader/loader.ld", "firmware/loader/loader.c",
    "firmware/loader/ldr_core.c", "firmware/src/libc.c", "firmware/src/usb.c", "firmware/src/ota.c",
    "firmware/hal/fm1_cc.h", "firmware/hal/fm1_flash.h", "firmware/hal/fm1_sys.h",
    "firmware/hal/fm1_time.h", "firmware/hal/fm1_usb.h", "firmware/hal/fm1_xip.h",
    "tools/fm1pkg_make.py",
)
for name in sources:
    data = (bsp / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != pins["files"][name]:
        raise SystemExit(f"BSP hash mismatch: {name}")
    dest = work / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
PY
patch -s -p1 -d "$work" -i "$root/board/loader-policy.patch"
patch -s -p1 -d "$work" -i "$root/board/loader-target.patch"
cp board/loader-flash-policy.h "$work/firmware/loader/"
if [ "${COMP_LOADER_SHOW:-0}" = 1 ]; then
    patch -s -p1 -d "$work" -i "$root/board/loader-show-policy.patch"
fi
if [ "${COMP_LOADER_PROVISION:-0}" = 1 ]; then
    evidence="$root/build/board/first-show-loader"
    mkdir -p "$evidence"
    profile_flags="$profile_flags -DCOMPAS_FIRST_SHOW_FIXTURE"
    python3 tools/board-provision-input.py "$work"
    patch -s -p1 -d "$work" -i "$root/board/loader-provision.patch"
    cp board/loader-provision.h "$work/firmware/loader/"
    if [ "${COMP_LOADER_PROVISION_WIRE:-0}" = 1 ]; then
        evidence="$root/build/board/first-show-wire"
        if [ "${COMP_PROVISION_PREWRITE_ONLY:-0}" = 1 ]; then evidence="$root/build/board/first-show-wire-prewrite"; fi
        mkdir -p "$evidence"
        profile_flags="$profile_flags -DCOMPAS_FIRST_SHOW_WIRE"
        cp board/provision-transfer.c board/provision-transfer.h "$work/"
    fi
fi
if [ -n "${COMP_LOADER_EMU:-}" ]; then
    cp tests/board-loader-entry.S "$work/entry.S"
fi
if [ "${COMP_LOADER_PACKAGE:-0}" = 1 ]; then
    evidence="$evidence-packaged"
    mkdir -p "$evidence"
fi
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror $profile_flags -Iboard tests/board-loader-ranges.c -o "$work/test-ranges"
"$work/test-ranges" > "$work/ranges.txt"
rm "$work/test-ranges"

tool_image=sha256:2d0e361d19a6da61a8587d6c2f2c1517d0389fd72d64930cd16980f8662afac3
"$docker" run --rm --pull=never --platform linux/amd64 --network none --cap-drop ALL --pids-limit 64 \
    -e "COMP_LOADER_FLAGS=$profile_flags" \
    --mount "type=bind,source=$sdk,target=/opt/jieli,readonly" \
    --mount "type=bind,source=$work,target=/work" -w /work "$tool_image" sh -exc '
        bin=/opt/jieli/pi32v2/bin
        sha256sum /opt/jieli/common/bin/clang "$bin/ld" > toolchain.sha256
        "$bin/clang" -target pi32v2 -mcpu=r3 -c firmware/loader/crt0_ldr.S -o crt0.o
        debug=""
        if [ -f firmware/loader/loader-provision.h ]; then debug="-g"; fi
        "$bin/clang" -target pi32v2 -mcpu=r3 -Os $debug -std=c11 $COMP_LOADER_FLAGS -ffunction-sections -fno-builtin \
            -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -I. -Ifirmware/hal \
            -Ifirmware/src -c firmware/loader/loader.c -o loader.o
        extra=""
        if [ -f firmware/loader/loader-provision.h ]; then
            "$bin/clang" -target pi32v2 -mcpu=r3 -Os -g -std=c11 -ffunction-sections -fno-builtin -Wall -Wextra -Werror -I. -c provision.c -o provision.o
            extra="provision.o"
            if [ -f provision-transfer.c ]; then
                "$bin/clang" -target pi32v2 -mcpu=r3 -Os -g -std=c11 -ffunction-sections -fno-builtin -Wall -Wextra -Werror -I. -c provision-transfer.c -o provision-transfer.o
                extra="$extra provision-transfer.o"
            fi
        fi
        "$bin/ld" --gc-sections -e _start -T firmware/loader/loader.ld -Map=loader.map \
            crt0.o loader.o $extra -o loader.elf
        "$bin/objdump" -h -t -d loader.elf > loader.dis
        if [ -f entry.S ]; then
            /opt/jieli/common/bin/objcopy -O binary -j .text loader.elf loader.bin
            "$bin/clang" -target pi32v2 -mcpu=r3 -c entry.S -o entry.o
            entry=0x02090000
            if [ -n "$extra" ]; then entry=0x020BC000; fi
            "$bin/ld" -Ttext "$entry" -e _start entry.o -o entry.elf
            /opt/jieli/common/bin/objcopy -O binary -j .text entry.elf entry.bin
        fi
    ' >"$work/link.log" 2>&1 || { cat "$work/link.log"; exit 1; }

python3 tools/board-loader-verify.py "$work" "$tool_image" "${COMP_LOADER_SHOW:-0}" > "$work/loader-target.json"
if [ "${COMP_LOADER_PACKAGE:-0}" = 1 ]; then
    python3 tools/board-loader-package.py "$work" "$bsp" > "$work/loader-package.json"
    cp "$work/loader-package.json" "$work/stage-package.log" "$evidence/"
    cp "$work/ota.bin" "$evidence/loader-ota.fixture"
fi
if [ "${COMP_LOADER_PROVISION:-0}" = 1 ]; then
    python3 tools/board-loader-stack.py "$work" > "$evidence/stack.json"
    python3 tools/board-loader-provision-emu.py "$work" "$COMP_PROVISION_EMU" > "$work/loader-emu.json"
    cp "$work/loader-emu.json" "$evidence/"
elif [ -n "${COMP_LOADER_EMU:-}" ]; then
    set -- "$work" "$COMP_LOADER_EMU"
    if [ "${COMP_LOADER_SHOW:-0}" = 1 ]; then
        set -- "$@" --preserve-show "${COMP_LOADER_SHOW_SEED:?Set saved virtual show for preservation test}"
    fi
    if [ -n "${COMP_LOADER_REHEARSAL:-}" ]; then
        set -- "$@" --rehearsal "$COMP_LOADER_REHEARSAL"
    fi
    if [ -n "${COMP_LOADER_RESTORE_BOARD:-}${COMP_LOADER_RESTORE_REHEARSAL:-}" ]; then
        set -- "$@" --restore-board "${COMP_LOADER_RESTORE_BOARD:?Set prior linked board directory}" \
            --restore-rehearsal "${COMP_LOADER_RESTORE_REHEARSAL:?Set prior strict rehearsal receipt}"
    fi
    if [ -n "${COMP_BB1_INSTALLER:-}" ]; then
        set -- "$@" --bb1-installer "$COMP_BB1_INSTALLER"
    fi
    python3 tools/board-loader-emu.py "$@" > "$work/loader-emu.json"
    cp "$work/loader-emu.json" "$evidence/"
fi
for name in loader-target.json loader.map loader.dis; do
    cp "$work/$name" "$evidence/$name"
done
cat "$evidence/loader-target.json"
