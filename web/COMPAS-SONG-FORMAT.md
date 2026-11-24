# compas-song/1: the readable Compás show

A plain-text twin of a `.cpshow` file, for people and coding agents to read,
write, diff and share. It's browser-only: the FM-1 never sees it. The browser
turns it back into the exact show bytes and recomputes every checksum
(`web/show-text.mjs`, `showToText` / `textToShow`).

## Shape

```text
compas-song/1
# Lines starting # are notes.

slot 1 "BANQUEÑA 727"
  groove 23  bpm 94  feel 0  root 0  clave 0  seed 1129270608
  kit 0  energy 0  phrases 0  score 0  guide 0
  players 0 0 0 0 0 0
  role low    level 100  on     ................................
  role skin   level 100  on     ................................
  role hand   level 100  on     ................................
  role shake  level 100  on     ................................
  role scrape level 100  on     ................................
  role lead   level 100  on     ................................
  band origin 19  bass 0 44 45  overlays 0  levels 100 100 100 100  accent 0  part 6
  727 steps 32  bpm 94  feel 0  hit 92  accent 124  level 100  mode 2  sound 1
    high-bongo      ................................
    low-bongo       ..X...X...X...X...X...X...X...X.
    …all 15 sounds, in this order…
    star-chime      ................................

slot 2 hidden
  band-raw ff00000000000000000000
…
```

A file always has exactly 16 slots, numbered 1–16 in order.

## Rules

1. **Steps:** `X` accent, `x` hit, `.` rest. Role lanes are always 32
   characters (16-step grooves use the first 16). 727 lanes are as long as
   `steps` (16 or 32).
2. **Titles:** up to 19 characters. Á É Í Ó Ú Ñ Ü are allowed.
3. **Ranges:**
   - `groove` 0–28, `bpm` 60–180, `feel` 0–100;
   - `root` 0–11 (C to B), `clave` 0 = 3-2 and 1 = 2-3;
   - `energy` 0–2, `kit` 0–1, levels 0–100.

   The unit re-checks every value when the show loads, and refuses the whole
   show if one is out of range.
4. **The 727 sounds, in order:**
   - high-bongo, low-bongo;
   - muted-hi-conga, open-hi-conga, low-conga;
   - high-timbale, low-timbale;
   - high-agogo, low-agogo;
   - cabasa, maracas;
   - short-whistle, long-whistle;
   - quijada, star-chime.
5. **Role order:** low, skin, hand, shake, scrape, lead.
6. **`raw` slots:** a slot this format can't express exactly is written as
   hex (`slot N raw` with `song` and `band` lines), so nothing is ever lost.
   Leave those lines alone.

## For coding agents

Change values and grids, never the line order or the names. Write all 16
slots. After writing, load the file in the Compás browser page: it rebuilds
the `.cpshow` and refuses anything out of range, with a reason.
