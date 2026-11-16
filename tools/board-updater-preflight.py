"""Exercise the pinned FM-1 update stages with inert envelopes and virtual NOR."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
APP_OFFSET = 0x4120
APP_WINDOW = (0x4000, 0x93000)
FIXTURE_KEY = 0x1234
FIXTURE_PRODUCT = "COMPAS-FIXTURE"


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def run(args, directory):
    return subprocess.run(args, cwd=directory, capture_output=True, text=True, timeout=30)


def checked(args, directory):
    result = run(args, directory)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    return result.stdout


def packager_at(path):
    spec = importlib.util.spec_from_file_location("fm1pkg_fixture", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.sdk_file = lambda name: b"HOST ONLY: " + name.encode()
    return module


def fixture_package(pack, app):
    # The OTA parser expects two headers and a signature at the first literals.
    # Use a synthetic body with no loader code or decompression stream.
    body = bytearray(4096)
    struct.pack_into("<II", body, 4, 0x80, 0x01C0A800)
    struct.pack_into("<H", body, 0, pack.crc16(body[2:32]))
    body[0x28:0x38] = b"FELUCCA-LOADER-1"
    ota = pack.entry(pack.crc16(body), 0x20, len(body), 0x41, 0, 0, "HOST-ONLY") + body
    flash = pack.flash_image(app, FIXTURE_KEY)
    lo, hi = APP_WINDOW
    if len(flash) != hi:
        raise SystemExit("Synthetic flash extent differs from the tested app window")
    decrypted = bytearray(flash[lo:hi])
    pack.sfc(decrypted, 0, len(decrypted), 0, FIXTURE_KEY)
    start = APP_OFFSET - lo
    if decrypted[start:start + len(app)] != app:
        raise SystemExit("App bytes differ after fixture pack and decode")
    return pack.ufw(flash, ota, FIXTURE_PRODUCT)


def preflight(args, tmp):
    pins = json.loads((ROOT / "board/BSP.json").read_text())
    sources = (
        "firmware/src/ota.c", "firmware/loader/ldr_core.c", "tests/ota_test.c",
        "tests/ldr_test.c", "tools/fm1pkg_make.py",
    )
    for name in sources:
        source = (args.bsp / name).read_bytes()
        if digest(source) != pins["files"][name]:
            raise SystemExit(f"BSP source differs from the pinned file {name}")
        dest = tmp / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(source)

    board = ROOT / "build/board"
    for name in ("compas-offline.elf", "compas.sections"):
        shutil.copyfile(board / name, tmp / name)
    checked([sys.executable, str(ROOT / "tools/board-emu.py"), str(tmp),
             "rehearsal", "prepare"], tmp)
    image = (tmp / "emu-rehearsal.xip").read_bytes()
    rehearsal = json.loads(args.rehearsal.read_text())
    if digest(image) != rehearsal["imageSHA256"]:
        raise SystemExit("Linked app differs from the strict rehearsal receipt")
    elf = (tmp / "compas-offline.elf").read_bytes()
    if struct.unpack_from("<H", elf, 18)[0] != 0xF1 or struct.unpack_from("<I", elf, 24)[0] != 0x02000120:
        raise SystemExit("Expected the FM-1 PI32 app at 0x02000120")
    app = image[0x120:]
    pack = packager_at(tmp / "tools/fm1pkg_make.py")
    if len(app) > pack.APP_SLOT:
        raise SystemExit("Linked Compás app exceeds the app slot")
    candidate = tmp / "candidate.fixture"
    prior = tmp / "prior.fixture"
    candidate.write_bytes(fixture_package(pack, app))
    prior.write_bytes(fixture_package(pack, b"SYNTHETIC PRIOR APP\0" * 128))

    compile_fixture = [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra",
                       "-fsanitize=address,undefined", "-g"]
    checked([*compile_fixture, "tests/ota_test.c", "-o", "stage1"], tmp)
    stage1 = checked(["./stage1", str(candidate)], tmp)
    checked(["patch", "-s", "-p1", "-i", str(ROOT / "tests/board-loader-fixture.patch")], tmp)
    checked([*compile_fixture, "tests/ldr_test.c", "-o", "stage2-control"], tmp)
    control = run(["./stage2-control", str(prior), str(candidate)], tmp)
    expected_failures = ("verify mismatch stops after one sector write",
                         "missing final confirm keeps both staging records",
                         "only the two known staging sectors are erased on resume")
    if control.returncode != 1 or any(
        not any(line.startswith(name) and line.endswith("FAIL") for line in control.stdout.splitlines())
        for name in expected_failures
    ):
        raise SystemExit("Retained loader did not reproduce the expected failures\n" + control.stdout + control.stderr)
    checked(["patch", "-s", "-p1", "-i", str(ROOT / "board/loader-policy.patch")], tmp)
    checked([*compile_fixture, "tests/ldr_test.c", "-o", "stage2"], tmp)
    stage2 = checked(["./stage2", str(prior), str(candidate)], tmp)
    return {
        "scope": "host-only simulated NOR and protocol, inert SDK and OTA content",
        "bsp": pins["source"],
        "sourceSHA256": {name: pins["files"][name] for name in sources},
        "loaderPolicySHA256": digest((ROOT / "board/loader-policy.patch").read_bytes()),
        "linkedAppSHA256": digest(app),
        "rehearsedXipSHA256": digest(image),
        "appEntry": "0x02000120",
        "appFlashOffset": APP_OFFSET,
        "appBytes": len(app),
        "appSlotBytes": pack.APP_SLOT,
        "appSlotFreeBytes": pack.APP_SLOT - len(app),
        "appWriteWindow": list(APP_WINDOW),
        "stage1": stage1.strip().splitlines(),
        "retainedStage2Control": control.stdout.strip().splitlines(),
        "stage2WithPolicy": stage2.strip().splitlines(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsp", type=Path, required=True)
    parser.add_argument("--rehearsal", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    args.rehearsal = args.rehearsal.resolve()
    with tempfile.TemporaryDirectory(prefix="compas-updater-fixture-") as directory:
        result = preflight(args, Path(directory))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
