# Compás USB update interface

The USB MIDI driver, update loader and browser transport derive from BB1 BAND
r85, revision `1df20dcf9feda197a6bd668cf24f97d99d4f8ae1`, under GPL-3.0-only.
Their original copyright notices remain in the source.

Compás uses product string `Compas`, updater family `FM-1_922` and exact
product identity `FM-1_92210003`. Hold OCT− and OCT+ at startup for USB Update
Mode. PLAY after releasing the octave buttons retries normal startup.

The foreground owns the USB controller; audio may preempt USB servicing.
A normal update requires stopped playback and an empty command queue.
The instrument stays stopped during the transfer. The updater stages data
at `0xE0000`–`0xE5000`, and the standard loader verifies the boot area before
staging. See [BUILDING.md](../BUILDING.md) for the public packaging path.

The browser installer distinguishes completion of a transfer from a verified
restart and can check the connection again without repeating the write.
