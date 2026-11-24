# Compás 1.3 · playing guide

Compás is a rhythm section you conduct from the FM-1. PLAY starts the band.
The white song keys call sections. ALGORITHM changes Energy. SELECT moves
between the five performance pages. The four knobs always match the four
readouts along the bottom of the screen.

## Start here

1. Turn the physical **VOLUME** down, connect the FM-1 and press **PLAY**.
2. The large title is the song or groove. Yellow means playing, pink means
   waiting, and green means a layer is on.
3. Turn **PRESETS** to cue another song. It changes at the musical boundary;
   you do not need to stop first.
4. Raise **VOLUME** for the overall output. This is the MASTER control for the
   complete instrument.

The top lamps follow the sixteenth-note movement. A LIFT drags a short lamp
tail. When stopped, the lamps settle into a slow idle chase. With count-in on,
the screen shows a large 1–2–3–4 before the band begins.

## Band and the conductor

On BAND, the six bright center white keys call **INTRO, VERSE A, VERSE B,
CHORUS, BREAK** and **ENDING**. The current key is yellow. A queued key is
pink; the speaker cones and horn count down until it lands. A double-tap asks
for the next-bar change. Hold **ARP** and press a part key to fill into it.

Turn **ALGORITHM** for **BASE, LIFT** or **BREAK**. This is the main live
Energy move and arrives on the phrase. Turn it back before the phrase ends to
cancel the waiting change. **SEQ** calls the ending. The outer keys remain
playable 727 percussion; the screen separates them from the song-key block.

Band knobs are **TEMPO, FEEL, BASS, PERC**. PERC moves all four percussion
layers together. Black keys switch conga, maraca, timbal and bell layers.

Press **EDIT** for the band step editor. SELECT chooses a lane, OCT−/+ moves
between bars, and the keys toggle steps. Press HOME to return.

## Bass

Turn **SELECT** to BASS. K1 **TONE** darkens or brightens the line. K2 **BITE**
adds edge. K3 **LEVEL** sets the bass balance. K4 **STYLE** moves between the
ensemble line and ACID where the groove supports it. OCT−/+ changes the key.
ALGORITHM and the song-part keys stay live while this page is open.

Some written-score arrangements supply their own bass. The page then says
**SCORE PLAYS THE BASS** rather than pretending the Bass controls are active.

## Percussion

Turn SELECT to PERC. K1–K4 set **CONGA, MARACA, TIMBAL, BELL**. Their faders
and lamps show what is sounding. Use the black keys to switch each layer in or
out. In salsa and son, OCT−/+ chooses 3–2 or 2–3 clave. The screen draws the
clave and lights each stroke as it plays.

## 727 percussion and sequencer

Press **REC** or turn SELECT to 727. Keys 1–15 play bongos, congas, timbales,
agogós, cabasa, maracas, whistles, quijada and chime. Key 16 toggles ACCENT.
The screen pad flashes when its voice sounds.

- **ARP** enters ROLL; hold a pad and turn ALGORITHM for the roll rate.
- **REC** arms the selected pad for real-time recording; press REC again to stop.
- **SEQ** opens the sixteen-step grid. Keys 1–16 enter hits, OCT−/+ changes
  bar, and SCL switches accent entry. Press SEQ again to return to pads.
- **PRESETS** selects the 727 rhythm bank. A running preset changes on the bar.
- K1 chooses **SOUND**, K2 controls **HIT**, K3 **ACCENT**, K4 **LEVEL**.

The rhythm bank and the song arrangements are separate: a 727 preset can be
played alone, edited, or layered with the band.

## Kumbia

KUMBIA uses the four factory sounds and patterns of Hanan Cumbia by Oficina de
Sonido: bombo, güiro, campana and conga. Keys 1–4 play the sounds. Keys 5–8,
or PRESETS, cue WHITE, YELLOW, RED and GREEN on the next four-beat boundary.

Press FX to cycle **OFF, LAYER, DRUMS ONLY**. LAYER follows the band's tempo;
DRUMS ONLY removes the other Compás parts.

- K1 **DRIVE** adds shared saturation. Accented backing steps push it harder.
- K2 **DECAY** shortens or restores the sample tails.
- K3 **PITCH** tunes the kit together.
- K4 **LEVEL** balances Kumbia against the rest of Compás, from 0 to 100.
- OCT−/+ changes the sample **CLOCK**, shown in Hz. It changes grain and pitch,
  not song tempo.

Home settings are DRIVE 0, DECAY 100%, PITCH 0, CLOCK 9,804 Hz and LEVEL
100%. On MIX, OCT−/+ also trims the Kumbia level in five-point steps.

## Mix, effects and Rebajada

Press **FX** from a performance page for MIX. SELECT chooses a lane. The first
six keys mute; the next six solo. MIX knobs are **TEMPO, FEEL, LEVEL, SPEED**.
SPEED slows the whole band, pitch included. Sixty percent is the deepest
REBAJADA setting. Return it to 100 for normal speed.

Press FX again for EFFECTS:

- K1 **ROOM**: stereo room/reverb;
- K2 **ECHO**: echo return level;
- K3 **TIME**: 1/16, 1/8, 3/16, 1/4, 3/8 or 1/2, locked to tempo;
- K4 **REPEAT**: echo feedback;
- OCT−/+ **GLUE**: master-bus compression.

ECHO at zero ends the repeats immediately; raising it starts clean. The echo
lamp walks in time. Effects begin dry. Press FX again to return to MIX.

## Written songs and set list

Press **GLO** for THE SHOW. Keys 1–16 cue a song. PRESETS also moves through
the running order from BAND. NOW is yellow; NEXT is pink. After an ending,
PLAY continues with the next song. On the last song it stays ready there.

Written scores use their own key labels and knob strip. GUIDE plays the melody
reference. HOLD keeps the current section. The first two black keys also
control Guide and Hold. SEQ calls CODA where available. SHIFT transposes the
arrangement for the singer.

To store the show, choose the destination on GLO and press **SAVE**. Compás
keeps shows and user rhythms across power-off in its CPS6 journal. A firmware
update clears that journal: export the show with Big Beat Editor before
installing an update.

## English, Spanish, USB and browser tools

On GLO, turn **ALGORITHM** to switch EN / ES. It can change mid-song.

Compás sends MIDI clock at 24 PPQN plus Start and Stop. Straight grooves send
the displayed quarter-note tempo. In 6/8 and 12/8 the display counts
dotted-quarter pulses, so MIDI quarter-note tempo is one and a half times the
displayed number. REBAJADA changes the outgoing clock. Incoming MIDI Start and
Stop control the band; incoming clock-follow is not included.

**Big Beat Editor** reads and edits tempo, feel, all 38 groove selections,
Energy, key, clave, six band lanes and effects through USB MIDI/SysEx. Export
the show before updating. **Sideman** is a live control reference, not an
audio stream. The browser player runs the matching Compás audio engine but
does not measure the analogue output of an FM-1.

## Recovery and return

Use the web installer in desktop Chrome or Edge. Keep USB connected throughout
the write. If Compás cannot start, use the documented hardware recovery route
shown by the installer, then reconnect and retry. To return to official
firmware, supply an unchanged M-VAVE FM-1 package to the return section of the
installer. Never unplug during a write.

## Credits

Compás is free software under GPL-3.0-only. Hanan Cumbia sounds and patterns:
Oficina de Sonido, CC BY-SA 3.0. The 727 recordings and all other notices are
listed in `LICENSING.md`. The project is a tribute to Johnny Canales: when the
band is ready, take it away.
