CC ?= cc
WASM_CC ?= clang
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -pedantic
EXPORTS = init command status step level snapshot restore render pcm blob state_bytes
WASM_FLAGS = $(foreach f,$(EXPORTS),-Wl,--export=cp_$(f))

.PHONY: all test serve clean board-check package-check
all: build/engine.wasm build/compas
build/score.inc: src/score.json repertoire/cr8000-conga-excerpts.json repertoire/afro-cuban-727-cells.json repertoire/merengue-727-cells.json repertoire/bolero-727-cells.json repertoire/tr727-step-write.json repertoire/colombian-cumbia-cells.json tools/score.py
	python3 tools/score.py
build/engine.wasm: src/engine.c src/engine.h build/score.inc
	"$(WASM_CC)" --target=wasm32 -fuse-ld=lld $(CFLAGS) -Oz -ffreestanding -fno-builtin -nostdlib -Ibuild -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=4096 -Wl,--initial-memory=65536 -Wl,--max-memory=65536 $(WASM_FLAGS) $< -o $@
build/compas: src/host.c src/engine.c src/engine.h build/score.inc
	$(CC) $(CFLAGS) -Ibuild -Isrc src/host.c src/engine.c -o $@
build/test: tests/engine.c src/engine.c src/engine.h build/score.inc
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -Ibuild -Isrc tests/engine.c src/engine.c -o $@
build/test-provision: tests/board-provision.c board/provision.c board/provision.h
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -Iboard tests/board-provision.c board/provision.c -o $@
build/test-provision-transfer: tests/board-provision-transfer.c board/provision-transfer.c board/provision-transfer.h board/provision.h
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -Iboard tests/board-provision-transfer.c board/provision-transfer.c -o $@
build/test-recovery-guard: tests/board-recovery-guard.c board/recovery.h
	@mkdir -p build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -Iboard tests/board-recovery-guard.c -o $@
test: all build/test build/test-provision build/test-provision-transfer build/test-recovery-guard
	UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 ./build/test-recovery-guard
	UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 ./build/test-provision-transfer
	UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 ./build/test-provision
	./build/test
	node --test tests/*.test.mjs
	python3 -B tests/test_repertoire_midi.py
	python3 -B tests/test_board_piano.py
	python3 -B tests/test_board_restore_plan.py
	python3 -B tests/test_board_show_layout.py
	python3 -B tests/test_board_show_provision.py
	python3 -B tests/test_board_provision_wire.py
	python3 -B tests/test_board_install_plan.py
	python3 -B tests/test_board_candidate_inventory.py
	python3 -B tests/test_board_loader_package.py
	CC="$(CC)" WASM_CC="$(WASM_CC)" sh tools/standin-check.sh
# Compiled-board tests, golden frames and the native replay on the STAND-IN bank
# (build/standin). Needs no COMP_STUDIO and never touches the real build/board.
board-check:
	python3 tools/strings.py
	python3 -B tests/test_board_usb_messages.py
	$(CC) -std=c11 -Wall -Wextra -Werror -Iboard tests/board-sideman.c -o build/test-sideman && build/test-sideman
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -Iboard -Isrc -Ibuild tests/board-fx-unit.c -lm -o build/test-fx-unit && build/test-fx-unit
	CC="$(CC)" WASM_CC="$(WASM_CC)" sh tools/standin-check.sh
	for flag in "" -DCOMPAS_NO_SHOW_STORE; do \
	  $(CC) -std=c11 -w -D_POSIX_C_SOURCE=200809L -DCOMPAS_BOARD -DCOMPAS_EMU_STORE -DCOMPAS_USB $$flag \
	    -Iboard -Isrc -Ibuild/standin -Ibuild -Ibuild/board tests/board-release-save.c src/engine.c \
	    board/board.c board/panel.c board/piano.c build/standin/browser-bank.c -o build/standin/release-save && \
	  build/standin/release-save || exit 1; done
# Protected flash regions, on a built package (needs the pinned BSP and BB1 r85):
#   make package-check COMP_BSP=... COMPAS_PKG=compas-1.2-en.fwsc BB1_R85=BB1-r85.fwsc
package-check:
	python3 -B tools/compas-pkg-assert.py "$(COMP_BSP)" "$(COMPAS_PKG)" "$(BB1_R85)"
	python3 -B tests/compas-pkg-assert-test.py "$(COMP_BSP)" "$(COMPAS_PKG)" "$(BB1_R85)"
serve: build/engine.wasm
	python3 -m http.server --bind 127.0.0.1 8765
clean:
	rm -f build/engine.wasm build/compas build/test build/test-provision build/test-provision-transfer build/test-recovery-guard build/score.inc
	rm -rf build/standin build/golden-trace.txt

.PHONY: review board-recovery-review firmware-release
review: test
	python3 tools/offline.py
	python3 tools/set-editor.py

# Run after a board-target.sh build with COMP_EMU_STORE=1.
board-recovery-review: build/test-recovery-guard
	UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 ./build/test-recovery-guard
	python3 tools/board-startup-recovery.py "$(COMP_EMU)"

# Firmware packaging uses the explicit recipe in BUILDING.md.
firmware-release:
	@echo "Use the explicit public packaging recipe in BUILDING.md." >&2
	@echo "This target does not create a firmware package." >&2
	@exit 1
