"""Exercise saved-dump verification using retained packages and synthetic NOR."""
import argparse
import json
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile

TOOL = Path(__file__).resolve().parents[1]/'tools/board-recovery-readback.py'
CHECK = runpy.run_path(str(TOOL))
parser = argparse.ArgumentParser(description=__doc__)
for argument in ('bsp', 'compas', 'bb1', 'library'):
    parser.add_argument(argument, type=Path)
parser.add_argument('--candidate', choices=('navigation', 'k4-scope'), default='navigation')
args = parser.parse_args()
bsp, compas, bb1, library_path = (
    path.resolve() for path in (args.bsp, args.compas, args.bb1, args.library))
if args.candidate == 'navigation':
    prefix, model_key, pin = 'navigation', 'modeledNavigationFullFlashSHA256', CHECK['NAVIGATION_SHA']
else:
    prefix, model_key, pin = 'k4Scope', 'modeledK4ScopeFullFlashSHA256', CHECK['K4_SCOPE_SHA']
plan = CHECK['PLAN']
pack = plan['pinned_packager'](bsp)
host = runpy.run_path(str(bsp/'tools/fm1_install.py'))
start, end, size = (CHECK[name] for name in ('START', 'END', 'SIZE'))
base = CHECK['pinned_envelope'](pack, host, compas, CHECK['SCORE_CUES_SHA'], 'FM-1_92210001')
target = CHECK['pinned_envelope'](pack, host, bb1, pin, 'FM-1_92200201')
lib_start, lib_end = (CHECK[name] for name in ('LIBRARY_START', 'LIBRARY_END'))
library = library_path.read_bytes()
assert len(library) == lib_end - lib_start and plan['sha'](library) == CHECK['NAVIGATION_LIBRARY_SHA']
flash = base + b'\xff'*(lib_start-end) + library + b'\xff'*(size-lib_end)


def run(first, second, package=bb1, candidate=args.candidate, post_write=None):
    command = [sys.executable, str(TOOL), str(bsp), str(first), str(second),
               str(compas), str(package), '--candidate', candidate]
    if post_write is not None:
        command.extend(('--post-write', str(post_write)))
    return subprocess.run(command, capture_output=True, text=True, check=False)


def flip(data, offset):
    copy = bytearray(data)
    copy[offset] ^= 1
    return copy


with tempfile.TemporaryDirectory(prefix='compas-recovery-readback-') as directory:
    a, b = [Path(directory)/name for name in ('a.bin', 'b.bin')]
    a.write_bytes(flash)
    b.write_bytes(flash)
    result = run(a, b)
    assert result.returncode == 0, result.stderr
    matched = json.loads(result.stdout)
    assert matched['scoreCuesExecutableMatches'] and matched['scoreCuesAppWindowMatches']
    assert matched[f'{prefix}PackageSHA256'] == pin
    assert matched[model_key] == plan['sha'](target + flash[end:])
    assert matched[f'{prefix}SoundLibraryMatches']
    assert matched['capturedSPL'] == matched['candidatePackageSPL']
    assert matched['splPayloadMatches'] and matched['capturedSPL']['format'] == 'plain'
    assert not matched['writeAuthorized'] and not matched['physicalRecoveryQualified']
    assert a.read_bytes() == b.read_bytes() == flash

    assert 'separately saved reads' in run(a, a).stderr
    b.write_bytes(flash[:-1])
    assert 'file size' in run(a, b).stderr
    b.write_bytes(flip(flash, end+32))
    assert 'saved reads differ' in run(a, b).stderr

    b.write_bytes(flash)
    assert 'retained package pin' in run(a, b, compas).stderr
    other_candidate = 'k4-scope' if args.candidate == 'navigation' else 'navigation'
    assert 'retained package pin' in run(a, b, candidate=other_candidate).stderr

    # Encode the same SDK container in the representation used by the ROM.
    encoded = bytearray(flash)
    row = bytearray(encoded[32:64])
    pack.enc(row, 0, len(row))
    offset, length = struct.unpack_from('<II', row, 4)
    pack.enc(encoded, offset, 16)
    pack.enc(encoded, offset+16, length-16)
    a.write_bytes(encoded)
    b.write_bytes(encoded)
    result = run(a, b)
    assert result.returncode == 0, result.stderr
    encoded_report = json.loads(result.stdout)
    assert encoded_report['capturedSPL']['format'] == 'rom-encoded'
    assert encoded_report['capturedSPL']['knownPayload'] == 'felucca-sdk'
    assert encoded_report['splPayloadMatches'] and encoded_report['scoreCuesExecutableMatches']
    assert not encoded_report[f'{prefix}ProtectedHeadMatches'] and model_key not in encoded_report

    expected = bytes(encoded[:start]) + target[start:end] + bytes(encoded[end:])
    assert encoded_report['bootPreservingModel']['expectedFullFlashSHA256'] == plan['sha'](expected)
    post = Path(directory)/'post.bin'
    post.write_bytes(expected)
    result = run(a, b, post_write=post)
    assert result.returncode == 0, result.stderr
    post_report = json.loads(result.stdout)
    assert post_report['postWriteComparison']['fullFlashSHA256'] == plan['sha'](expected)
    assert not post_report['writeAuthorized'] and not post_report['physicalRecoveryQualified']
    assert a.read_bytes() == b.read_bytes() == encoded and post.read_bytes() == expected
    assert 'separate file' in run(a, b, post_write=b).stderr
    for offset, region in ((start-1, 'protected head'), (start, 'replacement app window'),
                           (end, 'preserved tail')):
        post.write_bytes(flip(expected, offset))
        result = run(a, b, post_write=post)
        assert result.returncode == 1 and f'differs from model in {region}' in result.stderr
    post.write_bytes(expected[:-1])
    assert 'file size' in run(a, b, post_write=post).stderr

    other_library = flip(flash, lib_start)
    a.write_bytes(other_library)
    b.write_bytes(other_library)
    result = run(a, b)
    assert result.returncode == 0, result.stderr
    library_report = json.loads(result.stdout)
    assert not library_report[f'{prefix}SoundLibraryMatches']
    assert library_report[model_key] == plan['sha'](target + other_library[end:])
    for offset, field in ((start-1,f'{prefix}ProtectedHeadMatches'),
                          (start+0x120,'scoreCuesExecutableMatches')):
        changed = flip(flash, offset)
        a.write_bytes(changed)
        b.write_bytes(changed)
        result = run(a, b)
        assert result.returncode == 0, result.stderr
        report = json.loads(result.stdout)
        assert not report[field] and model_key not in report
        if offset >= start:
            assert 'bootPreservingModel' not in report
            assert 'requires a verified saved-file model' in run(a, b, post_write=post).stderr

print(json.dumps(dict(fixture='synthetic tail with retained package envelope', candidate=args.candidate,
                      appIdentification=True, modeledWindow=True, protectedHeadRefusal=True,
                      wrongExecutableRefusal=True, mismatchedAndAliasedCapturesRefused=True,
                      wrongPackageRefused=True, soundLibraryMatchAndMismatch=True,
                      bootPreservingPostWriteComparison=True, physicalIO=False)))
