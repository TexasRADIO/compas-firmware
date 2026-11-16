"""Build the bounded first-show record for existing OTA address reads.

Offline library, no device transport or install entry point. Musical content
must already have passed the native WASM owner, as in board-show-flash.mjs.
"""
from pathlib import Path
import runpy
import struct
import zlib

OFFSET, HEADER = 0x100000, 64
model = runpy.run_path(str(Path(__file__).with_name('board-show-provision.py')))


def attach(logical, before, repeated, updated, legacy, compact, prepared_show):
    plan = model['prepare'](before, repeated, updated, legacy, compact, prepared_show)
    if not logical or len(logical) > OFFSET:
        raise ValueError('Logical package must end before the provisioning record')
    if logical[0x400:0x400+len(compact)] != compact:
        raise ValueError('Logical package does not carry the expected compact flash envelope')
    original = bytes(before[0x90000:0x93000])
    _, payload = model['layout']['newest_show'](prepared_show)
    body = original + payload
    fields = (1, HEADER+len(body), 0x90000, len(original), len(payload),
              zlib.crc32(updated[:0x4000]), zlib.crc32(updated[0x4000:0x90000]),
              zlib.crc32(body), zlib.crc32(original), zlib.crc32(payload),
              0x4000, 0x90000)
    header = b'CPPRV001' + struct.pack('<12I', *fields)
    header += struct.pack('<II', zlib.crc32(header), 0)
    assert len(header) == HEADER and len(body) == 16064
    return bytes(logical).ljust(OFFSET, b'\xff') + header + body, plan.run()
