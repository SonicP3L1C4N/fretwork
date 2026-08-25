<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Fretwork

**A tablature program for Linux that plays every track as its own stem.**

Reads Guitar Pro files, converts them to a format of its own, and renders each
track through a separate synthesised instrument — so a track can be soloed, sent
through its own amplifier simulation, and exported as an independent audio file.

That last part is the reason the project exists. Linux already has a tablature
editor: TuxGuitar has been at it for twenty years and is not going to be beaten
at being TuxGuitar. What no tablature program on Linux does is treat the score as
a multitrack session — one synth per track, per-track LV2 effects, real stems out
the other end. Fretwork is that program, and everything else it does is in
service of it.

## Status

**P0 — spike.** There is no application yet. There is a throwaway Python script
that proves the pipeline end to end, and there are the documents that say what
gets built next and why.

What works today, in `spike/`:

```
$ python3 spike/gp2midi.py "Slomosa-Horses.gp"
Horses — Slomosa
  Guitar Pro 8.1.3, 176 bars notated, 176 played
  tempo   150 bpm at bar 1, 145 bpm at bar 103, 100 bpm at bar 170
  [0] Guitar I               electricGuitar   prog 29   1429 notes   tuning 36 41 46 51 55 60
  [1] Guitar II              electricGuitar   prog 29   1476 notes   tuning 36 41 46 51 55 60
  [2] Electric Bass (pick)   electricBass     prog 34    872 notes   tuning 24 29 34 39
  [3] Drums                  drumKit          prog 0    1292 notes   tuning 0 0 0 0 0 0
```

— and `--stems DIR` writes one WAV per track plus a mix. Eleven Guitar Pro 8
files parse, play at the right tempo for the right length, and come out as
separate stems.

## Documents

- **[docs/architecture.md](docs/architecture.md)** — what gets built, in what
  order, on what stack, and why each choice beat the alternative. Read this
  first.
- **[docs/gpif-format.md](docs/gpif-format.md)** — how a `.gp` file is actually
  put together, measured against a real corpus rather than assumed. There is no
  vendor specification for any of this.

## The stack, briefly

C++20 with Qt 6 and KDE Frameworks 6 for the application; Rust behind a C ABI
for the file importers, because that is the only code eating untrusted binary
input; FluidSynth for synthesis, one instance per track; lilv and suil for
per-track LV2 chains; PipeWire out. CMake and Ninja, KDE's own conventions,
GPL — the reasoning for every line of that is in
[docs/architecture.md](docs/architecture.md).

## The plan

| | | |
|---|---|---|
| **P0** | Spike — parse, play, render stems | **done**, and it plays |
| P1 | Headless converter: importers, model, `.fw` format, technique translation, stem export. No window | next |
| P2 | The player: tab rendering, transport, mixer, live playback | |
| P3 | The editor: fret entry, undo, copy and paste | |
| P4 | Per-track LV2 chains, guitarix, SFZ sampling with round-robins | |
| P5 | Standard notation, PDF, MusicXML, GP6 | |

P1 is the one that matters: at the end of it the program is useful with no user
interface at all, which is the only honest definition of a foundation.

## Running the spike

Needs `python3` and, for anything audible, `fluidsynth` with a SoundFont:

```
sudo apt install fluidsynth fluid-soundfont-gm

python3 spike/gp2midi.py FILE.gp                 # write a MIDI file
python3 spike/gp2midi.py FILE.gp --play          # play it now
python3 spike/gp2midi.py FILE.gp --stems out/    # one WAV per track, plus a mix
python3 spike/gp2midi.py FILE.gp --no-repeats    # as notated, not as played
```

No dependencies beyond the standard library, on purpose: a spike that needs a
virtualenv is a spike nobody runs twice.

## Names and law

"Guitar Pro" is a trademark of Arobas Music. Fretwork is not affiliated with
them, not derived from their software, and ships none of their soundbanks or
artwork. It reads a file format — which reverse engineering for
interoperability expressly permits (EU Software Directive 2009/24/EC, Article
6), and which is not the same thing as copying a program.

The test corpus is transcriptions the author owns. It is not redistributed and
not committed.

## Licence

GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL, which is KDE's
preferred expression for application code. REUSE-compliant; every file says so
itself.
