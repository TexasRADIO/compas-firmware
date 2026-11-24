# Kumbia Kit

KUMBIA adds four percussion pads and four factory patterns to the private Compás review build. The sample bytes and pattern data come from Oficina de Sonido's Hanan Cumbia factory build `hanan26Taller` dated 21 July 2026. The build checks their hashes before generating the bank. [Factory image](https://hanancumbia.oficinadesonido.org/firmware_studio.js?v=17) · [Factory preview](https://hanancumbia.oficinadesonido.org/studio.js?v=27)

Hanan Cumbia is based on Bleep Labs’ Bleep Drum. The four unmodified factory sounds and four patterns retain their [CC BY-SA 3.0 licence](https://creativecommons.org/licenses/by-sa/3.0/). [Oficina de Sonido’s Hanan Cumbia manual](https://www.oficinadesonido.org/wp-content/uploads/2026/07/Hanan-Cumbia.pdf)

Open `build/Compas-Kumbia.html`, or turn SELECT to KUMBIA in the FM-1 emulator. Raise MASTER and press PLAY to start the kit alone at 90 BPM. Keys 1–4 play Bombo, Güiro, Campana and Conga. Keys 5–8 or PRESETS cue White, Yellow, Red and Green. While playing, the cue lands at the next four-beat boundary. FX switches Off, Layer and Drums Only. Set Layer to play the kit with a Compás groove. Set the groove tempo on Band.

| Knob | English | Español | Home | Range |
| --- | --- | --- | ---: | --- |
| K1 | DRIVE | SATURA | 0 | 0–100 |
| K2 | DECAY | CAÍDA | 100% | 5–100% |
| K3 | PITCH | AFINA | 0 | −12 to +12 semitones |
| K4 | LEVEL | NIVEL | 100% | 0–100% |

**Balance Kumbia against the band:** on **KUMBIA**, turn **KNOB4 · LEVEL**
(Spanish: **NIVEL**). On **Mixer**, **OCT−/+** changes the same level by 5%.
At 0 the
kit is silent; its pattern keeps running in time. At 100 the factory level is
unchanged. The physical VOLUME knob is MASTER for the whole instrument.
On Kumbia, **OCT−** lowers the sample clock and **OCT+** raises it; its
**CLOCK / RELOJ** readout shows Hz (4,400–9,804). On Effects, OCT−/+ still
adjusts GLUE.

**ES · Para equilibrar Kumbia con la banda:** en **KUMBIA**, gira
**KNOB4 · NIVEL**. En **Mixer**, **OCT−/+** cambia ese mismo nivel de 5 en 5.
En 0 el kit queda en silencio, pero el patrón sigue a tiempo. En 100 conserva
el nivel original. VOLUME regula todo el instrumento. En Kumbia, **OCT−**
baja el reloj de muestreo y **OCT+** lo sube; **RELOJ** indica su valor en Hz.

**EN** · CLOCK sets the kit’s sample clock. Lower it for a rougher sound. The pattern keeps the same tempo.

**ES** · RELOJ ajusta el reloj de muestreo del kit. Bájalo para que suene más áspero. El patrón sigue al mismo tempo.

Drive acts on the kit mix. It applies up to about +30 dB before a level-compensated asymmetric clipper. In Layer mode, accented backing steps raise the drive input by up to about 6 dB, proportional to K1. Decay shortens the samples and fades the cut tail. Pitch transposes all four voices while retaining the factory pitch of each conga and campana step. Home settings reproduce the four factory preview loops after mapping the unsigned audio to Compás’s output format.

One set of controls applies to the whole kit. The browser retains its four sound
controls and level for this origin when local storage is available. Press STOP,
then SAVE on Kumbia to retain them in the on-unit journal on a 1.3 build. The
new CPS6 record adds one level byte inside the same A/B sectors. Earlier CPS3/
CPS5 records keep their four controls and default the new level to 100; older
four-value browser saves do the same. No song or rhythm-file format changes.
The legacy 1.0 layout does not save on the unit. Firmware updates still clear
the journal under the unchanged r85 updater envelope; export your show first.

Set COMP_HANAN to the extracted factory sounds and presets directory and use
the existing board-browser.py and board-target.sh recipes. Run
`tools/kumbia-level-check.sh` after the real-bank browser build. Current
release-layout timing uses `tools/board-live-load-emu.py` and
`tools/board-rc3-emu.py`; package boot uses `tools/board-package-smoke.py`.
The older `board-kumbia-rehearsal.py` retains its historic K4 CLOCK and
pre-release journal fixture and must not be used to qualify this control map.
