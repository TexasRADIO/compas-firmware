# Compás USB updater offline review

This branch starts at the offline lock `6806ccbc4864e0096e3c8102e912fbfd625e2fe1`.
The USB sources came from the r85 corresponding source archive at
`1df20dcf9feda197a6bd668cf24f97d99d4f8ae1`, archive SHA-256
`a1e265ab569305490df39f948631daad5b7a8432ee13cfca05cf7610722a12a8`.
The first commit imports the three firmware files unchanged. The next commit
ports the MIDI updater to Compás. It keeps the BB1 USB endpoint layout and
wire protocol, with the product string `Compas`, plain identity `FM-1_922`,
and exact product identity `FM-1_92210003`.

Hold OCT− and OCT+ at startup for USB Update Mode. Two early watchdog failures
select the same loop. It starts without the instrument or the audio interrupt
and clears the failure streak after ten healthy seconds. PLAY after release
retries normal boot. The OCT pair plus PLAY is the direct software ROM request.
The `7D` SysEx requests the same ROM mailbox from normal and safe mode.

The foreground owns the USB controller. Audio can preempt USB servicing.
A normal update requires a stopped instrument and an empty command queue with
no pending save. The audio engine stays stopped during the transfer.
NOR writes from the updater are constrained to `0xE0000–0xE5000`. The official
loader route additionally compares the boot area byte for byte before staging.
Journal writes wait for an in-progress full redraw to finish.

The brief's 62,004 byte headroom was measured on an earlier revision. The locked
storage app is 569,792 bytes before the 288-byte XIP entry prefix. The compact
slot holds 569,276 bytes. This profile fits by sharing immutable score data,
using one pitch increment table, and storing the backdrop rays as runs. These
changes retain the score events, pitch values, artwork and 727 PCM. No new
display buffer is allocated.

## Offline reproduction

Set `COMP_BSP`, `COMP_JIELI` and `COMP_STUDIO` to the pinned inputs described in
[Offline review start](OFFLINE-REVIEW-START.md). Then run:

```sh
COMP_USB=1 COMP_EMU_STORE=1 tools/board-target.sh
python3 tools/board-emu.py build/board store prepare
python3 tools/board-stack.py build/board store
python3 tools/board-show-rehearsal.py "$COMP_EMU" --store --all-rhythms --usb
python3 tools/board-usb-emu.py "$COMP_EMU" --stock-v15 "$STOCK_V15" --bb1 "$BB1_R85"
python3 tools/board-installer-review.py "$COMP_BSP" build/installer-review
node tests/compas-installer.mjs build/installer-review/Compas-USB-review.fixture "$STOCK_V15" "$BB1_R85"
```

The two input package SHA-256 values are
`db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` (V15)
and `7c3c9524d2b689fb095017a2ee2fb1c1a475a9be3182f5055faf62274d74e367` (r85).
The local installer page validates package bytes and has no MIDI device access.
Its generated fixture has an inert loader and an emulator key. The
`firmware-release` target remains closed.

The ROM mailbox is evidence of a firmware request. Exact-unit USB enumeration
at `4C4A:8057`, recovery tool communication, matching full-flash reads and the
verified return procedure remain mandatory before any physical write. See
[the recovery procedure](BIG-BEAT-RECOVERY-READBACK.md).
