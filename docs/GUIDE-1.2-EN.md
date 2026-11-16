# Compás 1.2 — effects, clock and Sideman

This supplements the existing Compás guide. The song, bass, percussion, 727 and Kumbia controls retain their existing roles.

Press FX/MIX for the mixer, then press it again for Effects. Pressing FX again returns to Mix. K1 is ROOM, K2 ECHO, K3 TIME and K4 REPEAT. OCT− and OCT+ lower and raise GLUE. You can continue playing parts while adjusting the effects.

ROOM adds stereo reverb. ECHO is a tempo-synced return level. TIME selects 1/16, 1/8, 3/16, 1/4, 3/8 or 1/2 and follows REBAJADA. REPEAT controls echo feedback. GLUE compresses the mix. Start low and increase one effect at a time. Setting ECHO to zero stops its repeats immediately; turning it back up starts fresh. ROOM, ECHO and GLUE start at zero, TIME at 3/16 and REPEAT at 35. Effects reset at restart. The protective output curve applies to the effects path; with effects off the tested dry grooves are unchanged.

Compás sends MIDI clock at 24 pulses per quarter note while playing, with Start and Stop. Its outgoing clock follows the band's effective tempo, including REBAJADA. Incoming MIDI Start/Stop control transport; incoming clock-follow is not implemented. Select Compás as the clock source in the receiving application's settings.

Sideman is a browser control reference. In supported desktop Chrome/Edge, connect over USB and allow MIDI/SysEx. It follows reported pages, the last encoder, language, tempo, transport and groove-part context. It is not a remote editor, audio stream or set librarian. No account or upload is needed. The endpoint may retain an older CoreMIDI name; firmware identity determines the product.

SAVE on the FM-1 keeps a session copy, not a durable flash save. Working edits and effects reset on power-off; browser files remain the way to retain supported shows and rhythms. 1.2 does not include the proposed cross-platform editor, external clock follow, new persistent storage, retro capture, Tighten or the Pará expansion.

After a firmware transfer, a missing startup reply is not proof that the write failed. Keep USB connected and use Check connection to retry the identity query. This recheck does not reflash. Only an exact identity reply reports success. An interrupted transfer and a completed-but-unverified restart are different states.
