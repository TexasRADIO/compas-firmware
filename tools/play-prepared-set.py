"""Open a Compás set or saved show in the local FM-1 emulator."""
import argparse
import fcntl
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile


EMU_SHA256 = "8a58e43fe66ed1194a546915782c531a3248ed96cd3a262f9097f0f84aa56d6a"
CHOOSER = """
const app = Application.currentApplication();
app.includeStandardAdditions = true;
app.chooseFile({withPrompt: "Choose a Compás set or saved show", ofType: ["json", "cpshow"]}).toString();
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def choose_set():
    result = subprocess.run(["/usr/bin/osascript", "-l", "JavaScript", "-e", CHOOSER], capture_output=True, text=True)
    if result.returncode:
        if "(-128)" in result.stderr:
            return None
        raise SystemExit(result.stderr.strip())
    return Path(result.stdout.rstrip("\n"))


def play(args, source):
    image = args.image.resolve()
    emulator = args.emulator.resolve()
    for path, expected in ((image, args.image_sha), (emulator, EMU_SHA256)):
        if digest(path.read_bytes()) != expected:
            raise SystemExit(f"Preview checksum mismatch for {path.name}")
    with source.open("rb") as file:
        data = file.read(65537)
    if len(data) > 65536:
        raise SystemExit("Compás set or show export is too large")
    is_show = source.suffix.lower() == ".cpshow"
    if is_show and not args.show_wasm:
        raise SystemExit("Opening a .cpshow needs the paired browser WASM and its checksum.")
    label = re.sub(r"[^A-Za-z0-9 -]", "-", source.stem).strip(" -")[:32] or "Compas Set"
    identity = f"{label}-{digest(data)[:16]}"
    folder = args.state_dir.resolve() / (f"{identity}-show" if is_show else identity)
    folder.mkdir(parents=True, exist_ok=True)
    flash = folder / "Compas Virtual Set.bin"
    with (folder / "Compas Virtual Set.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit("This Compás set or show is already open.")
        if not flash.exists():
            with tempfile.TemporaryDirectory(prefix=".compas-prepare-", dir=folder) as temporary:
                temporary = Path(temporary)
                snapshot = temporary / ("input.cpshow" if is_show else "set.json")
                snapshot.write_bytes(data)
                staged = temporary / "prepared.bin"
                if is_show:
                    converter = Path(__file__).with_name("board-show-flash.mjs").resolve()
                    subprocess.run(["node", str(converter), str(snapshot), str(staged),
                                    str(args.show_wasm.resolve()), args.show_wasm_sha],
                                   check=True, stdout=subprocess.DEVNULL)
                else:
                    compile_set = Path(__file__).with_name("board-set.py").resolve()
                    subprocess.run([sys.executable, str(compile_set), "--virtual-flash",
                                    str(snapshot), str(staged)], check=True)
                staged.rename(flash)
        with flash.open("rb") as saved:
            saved.seek(0x92000)
            if flash.stat().st_size != 0x100000 or saved.read(8) != b"CMPASIM1":
                raise SystemExit("The prepared set's virtual flash failed its format check.")
        if args.prepare_only:
            print(flash)
            return
        print("Turn up MASTER and press PLAY. Turn PRESETS to cue another song.", flush=True)
        subprocess.run([str(emulator), "--bin", str(image), "--entry", "0x02000120",
                        "--midi", "none", "--flash", str(flash)], cwd=folder, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?")
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--image-sha", required=True)
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--show-wasm", type=Path, help="paired native show validator")
    parser.add_argument("--show-wasm-sha", help="SHA-256 of the paired validator")
    parser.add_argument("--prepare-only", action="store_true", help="create or verify the virtual set without opening it")
    args = parser.parse_args()
    if bool(args.show_wasm) != bool(args.show_wasm_sha):
        parser.error("Supply --show-wasm and --show-wasm-sha together.")
    if args.show_wasm_sha and not re.fullmatch(r"[0-9a-f]{64}", args.show_wasm_sha):
        parser.error("Expected a lowercase SHA-256 for --show-wasm-sha.")
    source = args.source or choose_set()
    if source is not None:
        try:
            play(args, source)
        except OSError as error:
            parser.exit(1, f"Compás: {error}\n")
        except subprocess.CalledProcessError as error:
            parser.exit(error.returncode)


if __name__ == "__main__":
    main()
