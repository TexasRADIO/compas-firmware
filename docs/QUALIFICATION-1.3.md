# Compás 1.3 qualification · 24 November 2026

Compás 1.3 promotes the frozen 1.3-rc.4 English and Spanish packages without
changing their bytes. Native source commit `3842d29` is the code point used to
produce both applications. `RELEASE.json` binds the public package and XIP
hashes to that source.

## Qualified scope

| Check | Result | Scope |
| --- | --- | --- |
| Full source test suite | PASS | Engine, editor, screen, store, package, converter and Node checks. Optional fixture-dependent skips are not counted as passes. |
| Self-contained public rebuild | PASS | English and Spanish XIPs reproduced byte for byte from the included licensed inputs. |
| Package safety guard | PASS | The application-only envelope was accepted; protected-region and malformed-package mutations were rejected. |
| Native maximum-effects load | PASS | 12/8 study, Kumbia, four layers, ten PCM voices, 180 pulse BPM, USB and sample-clock corners. |
| Screen R1 / R2 / R3 | PASS | Both languages on Band, Mixer, Effects and Kumbia, including completed and superseded renders. |
| Editor protocol | PASS | All 38 groove selections, range checks, snapshot CRC, transactions and PLAY/STOP. |
| Virtual package boot | PASS | Both FWSC packages on the two supported flash-chip models. |
| Physical English and Spanish packages | PASS, scoped | Identity, all 38 groove selections, transport, editor snapshots, straight and compound MIDI clock and approximately two minutes of maximum-effects playback for each package. |
| Physical return path | PASS | A compatible installed firmware was restored and its normal identity verified. |
| Exact 1.3 to stock-V15 round trip | NOT RUN | The verified application-only recovery and return routes remain documented, but this exact cycle was not repeated for 1.3. |
| Physical Kumbia knob, sample-clock, master and save/power-cycle gestures | NOT RUN | USB edits and virtual controls do not substitute for these panel checks. |
| Analogue output voltage and PA acceptance | UNMEASURED | The digital output conversion is proven; no analogue capture interface was available. |
| Regional musician review | OPEN | The added entries are source-labelled accompaniment studies with authored support and proxy voices documented where used. |

## Timing and memory

The figures below are instruction-timed native-emulator results, not physical
IRQ or analogue measurements. Each language column is the maximum observed
across the relevant scenarios; maxima need not come from the same frame.

| Measurement | English | Spanish |
| --- | ---: | ---: |
| Peak complete IRQ window | 1,639.5 µs | 1,684.7 µs |
| Audio deadline | 2,902.5 µs | 2,902.5 µs |
| Deadline misses / late halves / clips / voice sheds | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| R1 compute / transfer / complete | 2.713 / 6.359 / 9.254 ms | 2.833 / 6.340 / 9.254 ms |
| R2 compute / transfer / complete | 1.712 / 19.171 / 28.951 ms | 2.407 / 19.139 / 28.951 ms |
| R3 compute / transfer / complete | 17.903 / 87.742 / 112.961 ms | 18.179 / 87.244 / 112.982 ms |
| Largest foreground paint pass | 13.658 ms | 13.644 ms |

Each application is 565,852 bytes, leaving 11,396 bytes below the 577,248-byte
journal-safe limit. Static RAM outside the effects pool is 25,164 bytes. The
effects pool uses 162,240 of 368,640 bytes. The conservative linked foreground
and audio stack sum is 3,112 of the 7,936-byte supervisor reservation.

The retained 228-run musical PCM parity receipt covers 38,946,816 stereo
frames, three Energy states and both palettes. `make test` also checks every
16-bit DAC input. These tests prove digital behavior; they do not claim a
particular PA loudness.

## Update and recovery boundary

The application write envelope is 0x4000–0x92FFF and staging is
0xE0000–0xE4FFF. The boot head and device identity are outside those ranges.
The documented recovery gesture is independent of the Compás application.
Firmware updates clear the CPS6 show journal, so export the show with Big Beat
Editor before installing.

The remaining panel, analogue and local-musician checks are reported openly.
They are follow-up acceptance work, not silently converted into passes.
