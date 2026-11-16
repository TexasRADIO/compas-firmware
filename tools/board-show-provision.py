"""Offline first-install NOR plan/model. No device access or installer entry point."""
from dataclasses import dataclass
import hashlib
import runpy
from pathlib import Path
import struct
import zlib

START, END, MARKER = 0x90000, 0x93000, b'CMPASIM1'
FLASH_BYTES, SECTOR, PAGE, PAYLOAD = 0x100000, 4096, 256, 3776
layout = runpy.run_path(str(Path(__file__).with_name('board-show-layout.py')))


@dataclass(frozen=True)
class Operation:
    kind: str
    offset: int
    data: bytes


@dataclass(frozen=True)
class Plan:
    original: bytes
    operations: tuple

    def run(self, cut=None):
        """Model a sequential byte-prefix power cut; never mutate input files."""
        total = sum(len(op.data) for op in self.operations)
        if cut is not None and not 0 <= cut <= total:
            raise ValueError('Invalid interruption byte')
        left = total if cut is None else cut
        image = bytearray(self.original)
        for op in self.operations:
            n = min(left, len(op.data))
            if op.kind == 'program' and any((a & b) != b for a, b in
                    zip(image[op.offset:op.offset+n], op.data[:n])):
                raise ValueError('NOR programming requires erase')
            image[op.offset:op.offset+n] = op.data[:n]
            left -= n
            if n != len(op.data):
                break
        return bytes(image)

    def resume(self, image):
        """Accept only an exact prefix state of this immutable plan, or its end."""
        if len(image) != FLASH_BYTES or image[:START] != self.original[:START] or image[END:] != self.original[END:]:
            raise ValueError('Interrupted image changed outside the show reservation')
        observed = image[START:END]
        state = bytearray(self.original[START:END])
        for op in self.operations:
            a, b = op.offset-START, op.offset-START+len(op.data)
            if observed[:a] == state[:a] and observed[b:] == state[b:]:
                n = 0
                while n < len(op.data) and observed[a+n] == op.data[n]:
                    n += 1
                if observed[a+n:b] == state[a+n:b]:
                    # Restart the marker-first plan, preserving the immutable app.
                    return self.run()
            state[a:b] = op.data
        raise ValueError('Interrupted image is not a prefix of this provisioning plan')

    def receipt(self):
        return {'scope':'offline byte-prefix NOR model',
                'beforeSHA256':hashlib.sha256(self.original).hexdigest(),
                'afterSHA256':hashlib.sha256(self.run()).hexdigest(),
                'writeWindow':[START,END], 'markerLast':True,
                'operations':[{'kind':o.kind,'offset':o.offset,'bytes':len(o.data)} for o in self.operations],
                'bytePrefixCuts':sum(len(o.data) for o in self.operations)}


def prepare(before, repeated, updated, legacy, compact, prepared_show):
    """Require known legacy/compact envelopes and a previously validated show.

    The caller must validate both envelopes with the pinned package builder,
    and the show's musical fields through the native WASM owner. This function
    checks their structural identity and ownership; it does not infer either
    from arbitrary device contents. All inputs are immutable in-memory copies.
    """
    before, repeated, updated = map(bytes, (before, repeated, updated))
    if any(len(x) != FLASH_BYTES for x in (before, repeated, updated)) or before != repeated:
        raise ValueError('Require two matching complete baseline copies and a complete updated image')
    if len(legacy) != END or len(compact) != START:
        raise ValueError('Require matching legacy and compact envelope extents')
    if before[0x4000:END] != legacy[0x4000:END] or updated[0x4000:START] != compact[0x4000:START]:
        raise ValueError('Application does not match the expected complete envelope')
    if before[:0x4000] != updated[:0x4000] or before[START:] != updated[START:]:
        raise ValueError('Update changed a protected region or the unprovisioned tail')
    if before[0x92000:0x92008] == MARKER:
        raise ValueError('Already provisioned; use the show-preserving update path')
    _, payload = layout['newest_show'](prepared_show)
    if len(payload) != PAYLOAD:
        raise ValueError('Expected complete CPS2 show')
    header = struct.pack('<III', 0x32535043, 1, zlib.crc32(struct.pack('<I',1)+payload))
    operations = [Operation('erase', off, b'\xff'*SECTOR) for off in (0x92000,0x91000,0x90000)]
    operations += [Operation('program',START+PAGE+off,payload[off:off+PAGE]) for off in range(0,PAYLOAD,PAGE)]
    operations += [Operation('program',START,header),Operation('program',0x92000,MARKER)]
    assert all(START <= o.offset < o.offset+len(o.data) <= END for o in operations)
    assert all(o.kind == 'erase' or len(o.data) <= PAGE-o.offset%PAGE for o in operations)
    return Plan(updated,tuple(operations))
