#!/bin/sh
# R1 safety net on the STAND-IN 727 bank (no COMP_STUDIO needed).
#
#   1. build the browser board into build/standin (never build/board, which
#      holds the real-bank build);
#   2. run the compiled-board tests (tests/board-*.mjs, except the packaging
#      check board-regional.mjs) against it;
#   3. check the golden frames (tests/golden-frames.json);
#   4. replay every golden session natively with $CC, and under ASan/UBSan
#      when the compiler supports it, and require identical pixels;
#   5. run tests/stage-host.c and check its stage-only screens.
#
# Usage: tools/standin-check.sh            (CC and WASM_CC from the environment)
#        tools/standin-check.sh --record   (re-record golden frames: R7 only)
set -eu
cd "$(dirname "$0")/.."
cc="${CC:-cc}"
wasm_cc="${WASM_CC:-clang}"
out=build/standin
python3 tools/score.py
python3 tools/board-piano.py
python3 tools/board-set.py
python3 tools/standin-bank.py "$out"

exports="encoders boot song_title cue input master audio snapshot snapshot_size read_view splash draw
 set_buffer set_size playing pull_saved_set update_set_display rhythm_buffer rhythm_size rhythm_export
 rhythm_import rhythm_slots_buffer rhythm_slots_size show_export lang rails fx_shed"
flags=""
for name in $exports; do flags="$flags -Wl,--export=bench_$name"; done
# Same flags as tools/board-browser.py; build/standin comes first so its 727.inc wins.
# shellcheck disable=SC2086
"$wasm_cc" --target=wasm32 -fuse-ld=lld -Oz -std=c11 -Wall -Wextra -Werror \
    -Wno-unused-function -ffreestanding -fno-builtin -nostdlib -DCOMPAS_BOARD -DCOMPAS_EMU_STORE \
    -Iboard -Isrc -I"$out" -Ibuild -Ibuild/board -Wl,--no-entry -Wl,--strip-all \
    -Wl,-z,stack-size=8192 $flags \
    web/board-bench.c src/engine.c board/board.c board/panel.c board/piano.c \
    "$out/browser-bank.c" -o "$out/browser-board.wasm"

COMPAS_BOARD_WASM="$out/browser-board.wasm"
export COMPAS_BOARD_WASM
# board-regional.mjs also checks the startup sets inside the generated HTML
# pages, so it runs after tools/board-browser.py with the real bank.
for test in tests/board-*.mjs; do
    [ "$test" = tests/board-regional.mjs ] && continue
    node "$test" >/dev/null
    echo "pass  $test"
done

if [ "${1:-}" = "--record" ]; then
    node tools/golden-frames.mjs
    node tools/golden-frames.mjs --lang es
else
    node tools/golden-frames.mjs --check
    node tools/golden-frames.mjs --check --lang es
fi

# Native replay. Waivers: stage.c's cameo statics are used only by fm1.c, and
# piano.c:308 is a known sign-compare (both on the R3/R5 list).
host_flags="-std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -Wno-sign-compare
 -D_POSIX_C_SOURCE=200809L -DCOMPAS_BOARD -DCOMPAS_EMU_STORE -Iboard -Isrc -I$out -Ibuild -Ibuild/board"
sources="tests/golden-replay.c src/engine.c board/board.c board/panel.c board/piano.c $out/browser-bank.c"
# shellcheck disable=SC2086
"$cc" $host_flags -O2 $sources -o "$out/golden-replay"
hosts="$out/golden-replay"
# The sanitizer replay is used when this compiler has the runtime: the probe
# (empty stdin) must reach the driver's own usage exit, 2.
rm -f "$out/golden-replay-asan"
# shellcheck disable=SC2086
if "$cc" $host_flags -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
        $sources -o "$out/golden-replay-asan" 2>/dev/null; then
    status=0; "$out/golden-replay-asan" </dev/null >/dev/null 2>&1 || status=$?
    if [ "$status" -eq 2 ]; then hosts="$hosts $out/golden-replay-asan"
    else echo "note  ASan/UBSan runtime unusable with $cc; native replay without sanitizers"; fi
else
    echo "note  $cc cannot link ASan/UBSan; native replay without sanitizers"
fi
# shellcheck disable=SC2086
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 node tools/golden-host.mjs $hosts

# tests/stage-host.c: stage-only states (synthetic BoardViews, no board owner),
# with bounds-checked LCD writes; its 42 screens are hashed as a second golden set.
screens="$out/stage-screens"
rm -rf "$screens"; mkdir -p "$screens"
stage_flags="-std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -DCOMPAS_BOARD -Iboard -Isrc -Ibuild -Ibuild/board"
case "$hosts" in
    *asan*) stage_flags="$stage_flags -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all" ;;
    *) stage_flags="$stage_flags -O2" ;;
esac
# shellcheck disable=SC2086
"$cc" $stage_flags tests/stage-host.c board/panel.c -o "$out/stage-host"
"$out/stage-host" "$screens"
if [ "${1:-}" = "--record" ]; then
    node tools/golden-host.mjs --screens "$screens" --record
else
    node tools/golden-host.mjs --screens "$screens"
fi
echo "STAND-IN check passed (bank: $out/727.pack, not the 727)."
