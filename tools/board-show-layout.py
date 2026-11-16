"""Offline compact application envelope; no device I/O or installable OTA stage."""
import struct
import zlib

APP_END = 0x90000
SHOW_END = 0x93000
APP_SLOT = 0x8AFBC


def compact_flash(pack, app, key):
    """Repack app + config below the journal; never truncate a legacy image."""
    if (pack.APP_SLOT, pack.FLASH_SIZE) != (0x8DFBC, 0x93000):
        raise ValueError("Unexpected package-builder layout")
    if len(app) > APP_SLOT:
        raise ValueError("Application exceeds the compact slot")
    try:
        pack.APP_SLOT, pack.FLASH_SIZE = APP_SLOT, APP_END
        flash = pack.flash_image(app, key)
    finally:
        pack.APP_SLOT, pack.FLASH_SIZE = 0x8DFBC, 0x93000
    inspect_flash(pack, flash, key, app)
    return flash


def inspect_flash(pack, flash, key, app, *, app_end=APP_END, app_slot=APP_SLOT):
    if len(flash) != app_end or (app_end,app_slot) not in ((APP_END,APP_SLOT),(0x93000,0x8DFBC)):
        raise ValueError("Unexpected application envelope extent")
    if not 0<len(app)<=app_slot:
        raise ValueError("Application exceeds its declared slot")
    area = bytearray(flash[0x4000:])
    pack.sfc(area, 0, len(area), 0, key)

    def entry(offset, base=0):
        row = area[offset:offset+32]
        if len(row) != 32 or pack.crc16(row[2:]) != struct.unpack_from('<H',row)[0]:
            raise ValueError("Invalid compact directory header")
        crc, address, size = struct.unpack_from('<HII',row,2)
        return crc, base+address, size, row[16:].split(b'\0',1)[0]

    crc, address, block, name = entry(0)
    if name != b'app_area_head' or address != 0x02000120 or not 0x120 <= block < len(area):
        raise ValueError("Invalid compact application extent")
    if pack.crc16(area[32:block]) != crc:
        raise ValueError("Invalid compact application checksum")
    for at, expected in [(32,b'app.bin'),(64,b'cfg_tool.bin')]:
        crc, offset, size, name = entry(at)
        if name != expected or not 0x120 <= offset < offset+size <= block:
            raise ValueError("Compact app/config overlaps protected storage")
        if pack.crc16(area[offset:offset+size]) != crc:
            raise ValueError("Compact app/config checksum mismatch")
        if at == 32 and (offset != 0x120 or size != app_slot or
                        area[offset:offset+len(app)] != app or
                        area[offset+len(app):offset+size] != b'\xff'*(size-len(app))):
            raise ValueError("Compact application or padding differs")
        if at == 64 and (offset != 0x120+app_slot or offset+size != block):
            raise ValueError("Configuration must immediately follow the application slot")
    crc, offset, size, name = entry(block,block)
    if name != b'cfg' or offset != block+32 or size<64 or block+size > len(area):
        raise ValueError("Compact nested config exceeds application extent")
    if pack.crc16(area[offset:block+size]) != crc:
        raise ValueError("Invalid compact nested config checksum")
    eq_crc, eq_off, eq_size, eq_name = entry(offset,block)
    if eq_name != b'eq_cfg_hw.bin' or eq_off != block+64 or eq_off+eq_size != block+size:
        raise ValueError("Invalid nested EQ extent")
    if pack.crc16(area[eq_off:eq_off+eq_size]) != eq_crc:
        raise ValueError("Invalid nested EQ checksum")
    return {"appWriteWindow":[0x4000,app_end],"preservedShowWindow":[APP_END,SHOW_END] if app_end==APP_END else None,
            "appSlotBytes":app_slot,"appBytes":len(app),"appSlotFreeBytes":app_slot-len(app),
            "configEnd":0x4000+block+size}


def newest_show(flash):
    """Require complete CPS2 records in the retained virtual show fixture."""
    if len(flash)!=0x100000 or flash[0x92000:0x92008]!=b'CMPASIM1':
        raise ValueError("Expected provisioned one-megabyte virtual show")
    records=[]
    for offset in (0x90000,0x91000):
        magic,generation,crc=struct.unpack_from('<III',flash,offset)
        payload=flash[offset+256:offset+256+3776]
        if magic==0x32535043 and zlib.crc32(struct.pack('<I',generation)+payload)==crc:
            records.append((generation,payload))
    if not records: raise ValueError("No complete CPS2 show record")
    if len(records)==2 and 0<(records[1][0]-records[0][0])%2**32<2**31:
        return records[1]
    return records[0]
