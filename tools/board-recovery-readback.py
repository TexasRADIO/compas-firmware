"""Check two saved Big Beat recovery dumps against pinned Compás and BB1 packages."""
import argparse
import json
from pathlib import Path
import runpy
import struct

TOOLS = Path(__file__).resolve().parent
PLAN = runpy.run_path(str(TOOLS/'board-install-plan.py'))
START, END, SIZE = 0x4000, 0x93000, 0x100000
SCORE_CUES_SHA = '64c7fafa9a5c17ed373398e7e676c9a0ec1bd4385f841ffc984a7023e0d4c719'
SCORE_CUES_APP_SHA = '19478eb836167dbab6a6d6dfef79d2a1346e5331553944518d528da27f01aaf8'
NAVIGATION_SHA = 'f73ddfc3ace5e9e67c68cf6fa2d02791f728ea38d22843a51b358a49944a827b'
NAVIGATION_APP_SHA = '2b1d813c6e0bdb77f09d5738626d180c9dcad07a10e9296f838be9d6ab37efdd'
NAVIGATION_LIBRARY_SHA = '8c2c3edb14bcbf4d91ff8b80ccf172f659c7ea837875d88819b78f8fe8ccfe2a'
K4_SCOPE_SHA = '30a084895e129b14cc0b597ff91b6e82600bb5fa03d4bd9afba97ee2bb99346b'
K4_SCOPE_APP_SHA = 'df86ee0bf936ad326971edb2e2d7b867820563dd0c5f26d336f0d2c4d4d1099c'
RETURN_CANDIDATES = {
    'navigation': ('navigation', NAVIGATION_SHA, NAVIGATION_APP_SHA, 580960),
    'k4-scope': ('k4Scope', K4_SCOPE_SHA, K4_SCOPE_APP_SHA, 580228),
}
LIBRARY_START, LIBRARY_END = 0xA0000, 0xDC000
SPL_PAYLOADS = {
    'fcaf033c5e10526353ecb11279a7556ac8645026d370c0267912cb4bfa7c7e4d': 'stock-v15',
    'b57bd7b4e112731fda43d0f29ac072ec8ce6e3832c1f41a8bfb337ceaf308415': 'felucca-sdk',
}


def inspect_spl(pack, flash):
    """Identify the bounded BANKCB payload in either archived representation."""
    row = bytearray(flash[32:64])
    pack.enc(row, 0, len(row))
    row_crc, data_crc, offset, size = struct.unpack_from('<HHII', row)
    if (pack.crc16(row[2:]) != row_crc or row[16:].split(b'\0', 1)[0] != b'uboot.boot'
            or row[12] != 0 or offset < 0xA0 or size < 16 or offset + size > START):
        return {'format': 'unrecognized'}
    container = flash[offset:offset+size]
    for form in ('rom-encoded', 'plain'):
        decoded = bytearray(container)
        if form == 'rom-encoded':
            pack.enc(decoded, 0, 16)
            pack.enc(decoded, 16, len(decoded)-16)
        header, payload = decoded[:16], decoded[16:]
        banks, length, load, start, payload_crc, header_crc = struct.unpack('<HHIIHH', header)
        if ((banks, length, load, start) == (1, len(payload), 0x01C02000, 16)
                and pack.crc16(header[:14]) == header_crc and pack.crc16(payload) == payload_crc
                and pack.crc16(decoded) == data_crc):
            digest = PLAN['sha'](payload)
            return {'format': form, 'payloadSHA256': digest,
                    'knownPayload': SPL_PAYLOADS.get(digest, 'unidentified')}
    return {'format': 'unrecognized'}


def pinned_envelope(pack, host, path, expected, product):
    raw, _ = PLAN['read_saved'](path, SIZE)
    if PLAN['sha'](raw) != expected or host['product_of'](raw) != product:
        raise ValueError(f'{path.name} does not match the retained package pin')
    logical = host['logical_image'](raw)
    envelope = logical[0x400:0x400+END]
    if len(envelope) != END:
        raise ValueError('Incomplete legacy flash envelope')
    PLAN['inspect_logical'](pack, logical, envelope)
    return envelope


def check_readback(bsp, first, second, score_cues, bb1_package, candidate_name='navigation',
                   post_write=None):
    prefix, package_sha, app_sha, app_bytes = RETURN_CANDIDATES[candidate_name]
    raw, identity = PLAN['read_saved'](first, SIZE, exact=True)
    repeated, second_identity = PLAN['read_saved'](second, SIZE, exact=True)
    if identity == second_identity:
        raise ValueError('Supply two separately saved reads, not aliases of one file')
    if raw != repeated:
        raise ValueError('The two saved reads differ')

    pack = PLAN['pinned_packager'](bsp)
    host = runpy.run_path(str(bsp/'tools/fm1_install.py'))
    installed = pinned_envelope(pack, host, score_cues, SCORE_CUES_SHA, 'FM-1_92210001')
    candidate = pinned_envelope(pack, host, bb1_package, package_sha, 'FM-1_92200201')
    key = PLAN['capture_key'](pack, raw)
    installed_key = PLAN['capture_key'](pack, installed)
    candidate_key = PLAN['capture_key'](pack, candidate)
    plain = bytearray(raw[START:END])
    pack.sfc(plain, 0, len(plain), 0, key)
    executable = plain[0x120:0x120+487732]
    executable_matches = PLAN['sha'](executable) == SCORE_CUES_APP_SHA
    exact_window = raw[START:END] == installed[START:END]
    if exact_window and not executable_matches:
        raise ValueError('Score Cues bytes are present but fail to decode with this dump header')

    app = bytearray(candidate[START:END])
    pack.sfc(app, 0, len(app), 0, candidate_key)
    target_executable = app[0x120:0x120+app_bytes]
    if PLAN['sha'](target_executable) != app_sha:
        raise ValueError(f'{candidate_name} executable does not match its build receipt')
    PLAN['LAYOUT']['inspect_flash'](pack, candidate, candidate_key, target_executable,
                                   app_end=END, app_slot=pack.APP_SLOT)

    library_sha = PLAN['sha'](raw[LIBRARY_START:LIBRARY_END])

    head_matches = raw[:START] == candidate[:START]
    captured_spl = inspect_spl(pack, raw)
    package_spl = inspect_spl(pack, candidate)
    report = dict(scope='saved file comparison only', dumpBytes=SIZE,
                  matchingSeparateFiles=True, dumpSHA256=PLAN['sha'](raw),
                  protectedHeadSHA256=PLAN['sha'](raw[:START]),
                  preservedTailSHA256=PLAN['sha'](raw[END:]),
                  scoreCuesPackageSHA256=SCORE_CUES_SHA,
                  scoreCuesExecutableMatches=executable_matches,
                  scoreCuesAppWindowMatches=exact_window,
                  scoreCuesEncryptionCompatible=key == installed_key,
                  capturedSPL=captured_spl, candidatePackageSPL=package_spl,
                  splPayloadMatches=bool(captured_spl.get('payloadSHA256') and
                                         captured_spl['payloadSHA256'] == package_spl.get('payloadSHA256')),
                  retainedSoundLibrarySHA256=library_sha,
                  physicalRecoveryQualified=False, writeAuthorized=False)
    # Both retained BB1 builds use the same pinned library.
    report.update({
        f'{prefix}PackageSHA256': package_sha,
        f'{prefix}ProtectedHeadMatches': head_matches,
        f'{prefix}EncryptionCompatible': key == candidate_key,
        f'{prefix}SoundLibraryMatches': library_sha == NAVIGATION_LIBRARY_SHA,
    })
    can_model = exact_window and executable_matches and key == candidate_key
    if can_model:
        expected = raw[:START] + candidate[START:END] + raw[END:]
        expected_sha = PLAN['sha'](expected)
        report['bootPreservingModel'] = {
            'replacementWindow': [START, END],
            'expectedFullFlashSHA256': expected_sha,
        }
        if head_matches:
            model = f'modeled{prefix[0].upper()}{prefix[1:]}'
            report[f'{model}AppWindow'] = [START, END]
            report[f'{model}FullFlashSHA256'] = expected_sha
    if post_write is not None:
        if not can_model:
            raise ValueError('Post-write comparison requires a verified saved-file model')
        observed, observed_identity = PLAN['read_saved'](post_write, SIZE, exact=True)
        if observed_identity in (identity, second_identity):
            raise ValueError('Post-write read must be a separate file from both baseline reads')
        if observed != expected:
            changed = [name for name, begin, end in (
                ('protected head', 0, START), ('replacement app window', START, END),
                ('preserved tail', END, SIZE),
            ) if observed[begin:end] != expected[begin:end]]
            raise ValueError('Post-write read differs from model in ' + ', '.join(changed))
        report['postWriteComparison'] = {
            'scope': 'saved read before starting the restored application',
            'fullFlashMatchesModel': True,
            'fullFlashSHA256': PLAN['sha'](observed),
        }
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ('bsp', 'first', 'second', 'score_cues', 'bb1_package'):
        parser.add_argument(argument, type=Path)
    parser.add_argument('--candidate', choices=RETURN_CANDIDATES, default='navigation')
    parser.add_argument('--post-write', type=Path,
                        help='compare a separate full flash read saved before booting BB1')
    args = parser.parse_args()
    try:
        report = check_readback(args.bsp, args.first, args.second, args.score_cues,
                                args.bb1_package, args.candidate, args.post_write)
    except (ValueError, OSError) as error:
        parser.exit(1, f'Compás recovery review: {error}\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
