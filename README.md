# Gridworks

VCV Rack 2 modules that play with a monome grid. By Haer.

## GridDrums

Six drum tracks, sixteen steps each, laid out on a monome grid (16 x 8). The grid
lights show the pattern and the playing step; pressing a key turns a step on or off.
The same grid is drawn on the panel and you can click it, so it works without a grid too.

**Grid layout**

| Row | Keys | What they do |
|---|---|---|
| 1 to 6 | 1 to 16 | Tracks 1 to 6, one key per step |
| 7 | 1 to 6 | Mute / unmute tracks 1 to 6 (key flashes when the track fires) |
| 7 | 13, 14 | CLEAR: hold, then press a track's key to empty that track, or a slot key to empty that slot |
| 7 | 15, 16 | COPY: hold, then press a slot key to copy the playing pattern into that slot |
| 8 | 1 to 16 | Memory slots. Press one and it starts at the next bar (blinks while waiting) |

On the panel, click CLEAR or COPY once to arm it, then click the track or slot.

**Jacks**: CLOCK (one pulse per step), RESET (next clock plays step 1, and a waiting slot starts),
SLOT (choose a slot by voltage: 1 semitone = 1 slot, 0 V = slot 1, so a sequencer's notes C, C#, D... pick slots 1, 2, 3...),
TR 1 to TR 6 trigger outs, BAR (a trigger on every step 1).

**Right-click menu**: pick which grid to use, let go of the grid, look for grids again,
gate outputs (as long as the clock pulse instead of short triggers), change slot at once
instead of at the next bar, clear the playing slot, unmute all.

If no clock is running, slot changes happen straight away.

### How it talks to the grid

Like Dewb's [monome-rack](https://github.com/Dewb/monome-rack), Gridworks talks to
**serialosc**, monome's free helper program, which must be installed and running on the Mac.
A new GridDrums takes the first free grid by itself. Only one module (or app) can use a
grid at a time: choosing the grid in GridDrums's menu takes it over from monome-rack
modules or other apps, and choosing it in their menu takes it back.

A 16 x 16 grid uses its top half. An 8-wide grid shows steps 1 to 8 only.

## LessConcepts

A two-voice melody sequencer driven by an 8-bit cellular automaton, after the norns script
[less concepts](https://github.com/vicimity-dndrks/less-concepts) by dan derks and linus schrab
(written fresh for Rack; the ~refrain looper and MIDI/crow outputs are left out). Same grid map
as the script: bits and octaves for each voice on rows 1-2, randomise keys on row 3, the 32-note
LOW/HIGH range on rows 4-5, snapshot keys and cycle modes on row 6, 16 snapshots on rows 7-8,
time keys on row 8. Panel: SEED and RULE (with CV), SCALE (41 norns scales), TRANSPOSE,
gate PROB per voice. Outputs: V/OCT and GATE for each voice, SEED as 0-10 V, STEP trigger.
See `docs/LessConcepts-Guide.pdf`.

## GridKeys

A Push-style note keyboard. The bottom-left key is the root in the starting octave; notes rise
to the right and each row starts a 4th (or 3rd, 5th, octave, or sequential) higher, so the same
note appears in several places. In Key or Chromatic, Push's 26 scales, WIDTH 4-16 columns
(default 8) with grid controls in column 16 (octave, latch, layout, row interval, chord keys,
held sustain). Chord keys play an in-key triad or 7th, with STRUM. CHORD input (poly V/OCT)
turns a chord into the scale; SHOW input lights notes another module plays. WIDTH 4-10 leaves
columns 11-15 for chord mode: 25 modifier pads (shape, extensions, inversion, voicing) and a
10-slot chord memory, playable from the SLOT (1 semitone per slot) and PLAY inputs. Outputs: poly
V/OCT + GATE, mono V/OCT + GATE with glide, TRIG. See `docs/GridKeys-Guide.pdf`.

## Building

Builds for Apple Silicon Macs run on GitHub Actions on every push (see `.github/workflows`).
Panel: `python3 res/make_panel.py`. Logic tests: `g++ -std=c++11 -I src test/core_test.cpp -o core_test && ./core_test` and the same for `test/lc_test.cpp` and `test/gk_test.cpp`.
