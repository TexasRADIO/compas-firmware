#!/usr/bin/env python3
"""Clang/wasm-ld launcher for Compás's freestanding browser build.

Compás does not depend on the Emscripten JavaScript runtime or sysroot. The
browser build is freestanding C, so its only WASM-specific dependency is the
linker. This wrapper resolves an explicit ``COMPAS_WASM_LD`` first and then a
``wasm-ld`` on PATH, replacing ``-fuse-ld=lld`` with the absolute path. Set
``COMPAS_WASM_CC`` to choose the compiler; otherwise clang on PATH is used.
Firmware reproduction does not require this browser-only tool.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import sys


def fail(message: str) -> None:
    raise SystemExit("Compás WASM toolchain: " + message)


def selected_linker() -> Path:
    found = os.environ.get("COMPAS_WASM_LD") or shutil.which("wasm-ld")
    if not found:
        fail("wasm-ld is not on PATH; set COMPAS_WASM_LD to an executable linker")
    # Keep a wasm-ld symlink intact: the LLVM multi-call driver selects the
    # WebAssembly frontend from argv[0]. Resolving it to the generic `lld`
    # binary makes the driver refuse the link.
    linker = Path(found).expanduser().absolute()
    if not linker.is_file() or not os.access(linker, os.X_OK):
        fail(f"COMPAS_WASM_LD is not executable: {linker}")
    return linker


def main() -> None:
    linker = selected_linker()
    compiler = os.environ.get("COMPAS_WASM_CC") or shutil.which("clang")
    if not compiler or not Path(compiler).is_file() or not os.access(compiler, os.X_OK):
        fail(f"COMPAS_WASM_CC is not executable: {compiler}")
    args = [f"-fuse-ld={linker}" if arg.startswith("-fuse-ld=") else arg
            for arg in sys.argv[1:]]
    os.execv(compiler, [compiler, *args])


if __name__ == "__main__":
    main()
