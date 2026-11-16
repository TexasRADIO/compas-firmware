"""Check saved FM-1 raw flash captures and plan an app-only restore."""
import argparse
import hashlib
import json
from pathlib import Path
import stat


FLASH_BYTES = 0x100000
SECTOR_BYTES = 0x1000
APP_START = 0x4000
APP_END = 0x93000


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_image(path, size, seen):
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_size != size:
        raise ValueError(f"{path} must be a regular {size}-byte file")
    identity = (info.st_dev, info.st_ino)
    if identity in seen:
        raise ValueError("Each input must be a separately saved file")
    with path.open("rb") as source:
        data = source.read(size + 1)
    if len(data) != size:
        raise ValueError(f"{path} changed size during the read")
    seen.add(identity)
    return data


def restore_plan(before_path, repeat_path, current_path=None, window_path=None):
    seen = set()
    before = read_image(before_path, FLASH_BYTES, seen)
    repeat = read_image(repeat_path, FLASH_BYTES, seen)
    if before != repeat:
        raise ValueError("The two baseline captures differ")

    window = before[APP_START:APP_END]
    result = {
        "scope": "saved files only",
        "flashBytes": FLASH_BYTES,
        "sectorBytes": SECTOR_BYTES,
        "appWriteWindow": [APP_START, APP_END],
        "baselineSHA256": digest(before),
        "baselineAppSHA256": digest(window),
    }
    if window_path is not None:
        candidate = read_image(window_path, APP_END - APP_START, seen)
        if candidate != window:
            raise ValueError("The candidate app window differs from the baseline")
        result["candidateAppMatchesBaseline"] = True

    if current_path is not None:
        current = read_image(current_path, FLASH_BYTES, seen)
        if current[:APP_START] != before[:APP_START] or current[APP_END:] != before[APP_END:]:
            raise ValueError("Current capture differs outside the app window. App-only restore refused")

        restored = bytearray(current)
        sectors = []
        for offset in range(APP_START, APP_END, SECTOR_BYTES):
            stop = offset + SECTOR_BYTES
            old = current[offset:stop]
            original = before[offset:stop]
            if old != original:
                sectors.append({"offset": offset, "currentSHA256": digest(old),
                                "restoreSHA256": digest(original)})
                restored[offset:stop] = original

        if restored != before:
            raise ValueError("Restore simulation did not reproduce the baseline")
        result.update(currentSHA256=digest(current), sectors=sectors,
                      simulatedRestoreSHA256=digest(restored))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("repeat", type=Path)
    parser.add_argument("--current", type=Path, help="full capture of the state to restore")
    parser.add_argument("--restore-window", type=Path, help="optional exact raw app window to compare")
    args = parser.parse_args()
    try:
        result = restore_plan(args.baseline, args.repeat, args.current, args.restore_window)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
