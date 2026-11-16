#!/bin/sh
# Offline core object only: no board files, USB, network or firmware package.
set -eu
cd "$(dirname "$0")/.."
root="$(pwd -P)"
sdk="${COMP_JIELI:?Point COMP_JIELI at the retained JieLi Linux toolchain}"
docker="${COMP_DOCKER:-docker}"
image=sha256:2d0e361d19a6da61a8587d6c2f2c1517d0389fd72d64930cd16980f8662afac3
python3 tools/score.py
mkdir -p build/target
"$docker" run --rm --pull=never --platform linux/amd64 --network none --cap-drop ALL --pids-limit 64 \
    --mount "type=bind,source=$sdk,target=/opt/jieli,readonly" \
    --mount "type=bind,source=$root/src,target=/src,readonly" \
    --mount "type=bind,source=$root/build/score.inc,target=/generated/score.inc,readonly" \
    --mount "type=bind,source=$root/build/target,target=/out" \
    "$image" sh -ec '
        cd /opt/jieli
        sha256sum common/bin/clang common/bin/objdump > /out/toolchain.sha256
        pi32v2/bin/clang --version > /out/compiler.txt
        pi32v2/bin/clang -target pi32v2 -mcpu=r3 -mfprev1 -std=c11 -Oz -Werror -Wall \
            -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
            -I/opt/jieli/pi32v2/include -I/generated -c /src/engine.c -o /out/engine.o
        common/bin/objdump -h -t -d /out/engine.o > /out/engine.dump
    '
