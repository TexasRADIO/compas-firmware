CC ?= cc
WASM_CC ?= clang
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -pedantic
EXPORTS = init command status step level snapshot restore render pcm blob state_bytes
WASM_FLAGS = $(foreach f,$(EXPORTS),-Wl,--export=cp_$(f))

.PHONY: all check generate board clean

all: build/engine.wasm build/compas

build/score.inc: src/score.json repertoire/cr8000-conga-excerpts.json repertoire/afro-cuban-727-cells.json repertoire/merengue-727-cells.json repertoire/bolero-727-cells.json repertoire/tr727-step-write.json repertoire/colombian-cumbia-cells.json tools/score.py
	python3 tools/score.py

build/engine.wasm: src/engine.c src/engine.h build/score.inc
	"$(WASM_CC)" --target=wasm32 -fuse-ld=lld $(CFLAGS) -Oz -ffreestanding -fno-builtin -nostdlib -Ibuild -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=4096 -Wl,--initial-memory=65536 -Wl,--max-memory=65536 $(WASM_FLAGS) $< -o $@

build/compas: src/host.c src/engine.c src/engine.h build/score.inc
	$(CC) $(CFLAGS) -Ibuild -Isrc src/host.c src/engine.c -o $@

generate:
	python3 tools/score.py
	python3 tools/strings.py
	python3 tools/board-bank.py release-inputs/studio
	python3 tools/board-piano.py --compact
	python3 tools/board-set.py board/set.json
	python3 tools/board-hanan.py release-inputs/hanan

check: build/compas generate
	@python3 -c 'import json; json.load(open("fmhub.json"))'
	@python3 -c 'from pathlib import Path; p=Path("docs/media/compas-screen.png"); assert p.stat().st_size <= 300000'

board: generate
	sh tools/board-target.sh

clean:
	rm -rf build
