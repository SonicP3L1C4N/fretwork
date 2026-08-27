<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Architecture

Written before the code, so that the decisions are arguable rather than
accidental. Everything here is subject to being wrong; nothing here is subject
to being unstated.

## What Fretwork is

A tablature program for Linux that reads Guitar Pro files, converts them to a
format of its own, and plays each track as a separate stem through synthesised
instruments.

The emphasis on stems is the point of the project rather than a feature of it.
Existing tablature software plays a score; Fretwork renders each track to its
own audio bus, so a track can be soloed, routed through its own amplifier
simulation, and exported as an independent audio file. That is a thing no
tablature program on Linux does, and it is the reason to write another one.

## What Fretwork is not

Not a notation engraver. Standard notation is a stated non-goal until P5, and
possibly for good: MuseScore's layout engine is the work of many people over
many years, and tablature is a far smaller problem that answers most of the
question. Fretwork renders tab.

Not a digital audio workstation. It has a mixer because stems need one, not
because it is trying to become Ardour.

Not a Guitar Pro clone, and not affiliated with Arobas Music. See
[Legal](#legal-position) below.

## The stack, defined

Measured on the development machine: Ubuntu 26.04 LTS, KDE Plasma 6.6.6 on
Wayland, PipeWire 1.6.2.

| Layer | Choice | Version here | Why |
|---|---|---|---|
| Application language | C++20 | GCC 15 | Every library below is a C API; the audio thread's rules are what C++ expresses natively; it is the language the rest of this author's KDE work is in |
| Binary-format importers | Rust, behind a C ABI | not yet written | For GP3–GP5 and GPX only — hand-rolled binary with no specification, where `cargo fuzz` finds what review does not. GP7/8 is a ZIP and XML, so it is C++ |
| UI toolkit | Qt 6 + KDE Frameworks 6 | Qt 6.10.2, KF6 6.24.0 | Custom-painted dense canvas, mature; matches the platform the program is for |
| Build | CMake + Ninja, ECM | CMake 3.20+ | KDE's own conventions, so the project stays proposable upstream |
| Synthesis | FluidSynth | 2.4.8 | SoundFont playback, one instance per track, offline rendering built in |
| Sample format | SFZ, read and played here | ours, ~600 lines | Round-robins and per-string articulation are what make a guitar sound like a guitar rather than like a General MIDI patch. **Not sfizz, as this row used to say.** It is not in the Ubuntu archive, and vendoring a large C++ library with its own dependency tree to use a dozen opcodes of a format was the worse trade — a parser and a voice allocator for the subset a plucked-string library uses is a few hundred lines that this project can read, test and fix. The cost is stated where it lands: linear interpolation, and no filters or LFOs. If a library ever needs more of the format than this reads, vendoring sfizz is still there to be done |
| Plugin hosting | lilv + suil | 0.26.2 / 0.10.24 | Per-track LV2 chains — the amplifier simulation that makes stems worth having |
| Amp simulation | guitarix LV2 | packaged | Already good, already free, already installed on the target machine |
| Audio I/O | PipeWire native, JACK fallback | 1.6.2 | The target machine already runs a pro-audio PipeWire profile |
| Audio files | libsndfile | 1.2.2 | Stem export |
| Containers | our own central-directory reader, on zlib | zlib 1.3 | Neither KArchive nor a local-header reader opens a Guitar Pro 8.1.4 file — see [gpif-format.md](gpif-format.md#the-container-changed-in-814) |
| Notation font | Bravura (SMuFL) | vendored, SIL OFL | Only when standard notation arrives; tab needs almost none of it |
| Tests | Qt Test + a file corpus | — | The corpus is the specification |

### Why C++ and not the alternatives

**Rust for everything** was the closest call. It wins on the importers and
loses on the rest: every library in the table above is a C API, which is an
`#include` from C++ and a maintained binding from Rust; and the editor is a
custom-painted scrolling canvas with hit-testing, which is exactly the case Qt
is good at and cxx-qt is merely adequate at. For a solo project, one toolchain
is worth more than any language feature. The compromise in the table — Rust for
the importers only — puts the borrow checker where the malformed input is and
nowhere else, behind an FFI surface of one function: bytes in, model out.

**Java** is where TuxGuitar already lives. Rewriting the incumbent in the
incumbent's language is not a project.

**Python** is right for the P0 spike and wrong for everything after it. The
audio callback cannot allocate, and a notation canvas cannot be redrawn from a
GC'd language at scroll rate.

**TypeScript** would inherit alphaTab's excellent tab rendering and lose every
reason the program exists: LV2 hosting, JACK/PipeWire routing, per-track stems.

## Components

### 1. Importers

Four formats hide behind "a Guitar Pro file", and they are different problems:

| Format | Container | Status |
|---|---|---|
| `.gp` (GP7/GP8) | ZIP holding `Content/score.gpif`, an XML document | **First.** See [gpif-format.md](gpif-format.md) |
| `.gp3` `.gp4` `.gp5` | Proprietary binary, length-prefixed, version string in the header | Second. Tedious but well mapped |
| `.gpx` (GP6) | BCFS/BCFZ — a custom bit-level compression around the same XML | Third. Needs a decompressor first |
| `.ptb`, `.tg`, MusicXML | Adjacent formats | Later, if ever |

None of these are documented by their vendor. Three GPL-compatible
implementations have already done the reverse engineering and disagree in
instructive places:

- **TuxGuitar** (LGPL) — twenty years of real files; the most battle-tested.
- **PyGuitarPro** (LGPL) — by far the most readable description of the GP3–GP5
  binary layout that exists.
- **alphaTab** (MPL-2.0) — the best handling of GPX and the GP7 XML.

All three are one-way compatible with GPL-3, which is what Fretwork ships
under. They are read as the specification that does not exist. Nothing is
copied without saying so in the file that copies it.

**On which of these is written in Rust.** The stack says Rust for the
importers, and the argument is untrusted binary input. GP7/8 is not that: it is
a ZIP holding an XML document, and the parsing that matters is done by zlib and
by Qt's XML reader, both of which have been fed malformed input by more people
than this project will ever have users. So `Gpif` is C++, and the case for Rust
is banked for GP3–GP5 and GPX, which are hand-rolled binary formats with no
specification — where a fuzzer finds in an afternoon what review does not find
at all. Writing it in Rust now would be following the letter of a decision
while missing what the decision was for.

Two rules that pay for themselves:

- **Keep unknown fields verbatim.** An importer that discards what it does not
  understand makes a document that can never be written back out.
- **The corpus is the test.** Every file the author owns parses without
  throwing, and that assertion runs on every commit. Malformed and
  edge-case files are the entire difficulty of this component.

### 2. The document model

```
Score → Track → Staff → Bar → Voice → Beat → Note
```

with tab data on the `Note` (string, fret, bend curve, slide, hammer-on/pull-off,
palm mute, harmonic, ghost, dead, let ring, vibrato, trill) and on the `Beat`
(duration, tuplet, chord, text, dynamic, stroke). Per track: tuning, capo,
instrument, MIDI programme. Per score: a timeline of tempo, time signature, key
and **repeat structure**.

The model is deliberately not the on-disk format and deliberately not the
playback timeline. Three representations, two conversions, each testable.

### 3. Fretwork's own format (`.fw`)

A ZIP container holding JSON: `manifest.json` carrying a `format-version` that
is never broken, `score.json` carrying the document, and room for embedded
assets later.

JSON rather than a bespoke binary, on purpose. The size cost is real and small;
what it buys is a format that diffs in git, migrates with readable code, and
can be inspected when a bug report arrives with a file attached. If profiling
ever says otherwise, CBOR is a change of encoder and not of design.

### 4. The playback engine

The component where a naive implementation is audibly wrong, and the reason to
be careful about the language.

**Repeat expansion.** The notated score is not the played score. Repeats,
alternate endings, D.S. al Coda and Fine are flattened into a linear event
timeline in an explicit pass. The audio thread never interprets a jump.

**One MIDI channel per string.** Pitch bend is per-channel: bend one string on a
single-channel track and every ringing note bends with it. Guitar Pro solves
this internally, and its own file format carries a `UseOneChannelPerString`
flag saying so. Fretwork allocates six channels per guitar track and sets the
pitch-bend range explicitly by RPN — the default of ±2 semitones cannot express
a whole-tone bend on a held note, let alone a dive. This decision reaches the
mixer and the stem layout, so it is made now rather than discovered later.

**Every technique is a translation decision.** Hammer-ons and pull-offs must not
re-attack. Palm mutes shorten the note and want a different sample layer.
Harmonics change pitch and timbre. Let ring means overlapping releases tracked
per string. Tremolo and trills expand into note streams. Ties extend rather than
restrike. This layer — notation to events — is the difference between sounding
like a guitar and sounding like a MIDI file from 1998, and it is where the
project's effort should go.

**One rule above all others: the engine is `fill(frames, at_sample_position)`
and nothing more.** Live playback drives it from the PipeWire callback; stem
export drives it from a loop, faster than real time, producing bit-identical
output. An engine that knows about wall-clock time makes offline rendering a
rewrite.

**Real-time discipline.** No allocation, no locks, no logging, no file I/O in
the audio callback. The UI edits a document; the engine is handed an immutable
timeline snapshot through a lock-free ring buffer and swaps it in at a bar line.

### 5. Rendering

Tablature only: numbers on six lines, beams, technique glyphs, and the
horizontal spacing algorithm — duration-proportional with minimum widths, then
line breaking and justification. That last part is the piece worth getting
genuinely right, because standard notation, if it ever arrives, shares it.

Drawn with `QPainter` into a `QQuickItem` (or a `QWidget` — decided when the
first real score is on screen and scrolling is measured, not before).

### 6. Export

MIDI, per-track WAV stems, a mixed WAV, PDF, and eventually MusicXML. Stems come
free from the engine rule above; everything else is a separate afternoon.

## Phases

| | Deliverable | Ends when |
|---|---|---|
| **P0** | Spike: parse a `.gp`, emit MIDI, play it through FluidSynth, render per-track stems. Python, throwaway | A real file from the corpus makes the right noise |
| **P1** | Headless converter: importers, model, `.fw` format, repeat expansion, technique translation, stem export. A CLI tool, no GUI, fully tested | The program is useful with no window |
| **P2** | The player: Qt6/KF6 window, tab rendering, transport, per-track mixer, live PipeWire playback | You would rather use it than TuxGuitar to read a tab |
| **P3** | The editor: fret entry, selection, undo/redo, copy/paste, transposition | **done.** Scope did double, as predicted, and then some: everything a bar or a part *is* became editable too -- tempo, time signature, section names, tuning, capo, and the set of parts. It ends where it does because a score can now be made from nothing rather than only changed |
| **P4** | The differentiator: per-track LV2 chains, guitarix presets, SFZ sampling with round-robins | It sounds like a guitar |
| **P5** | The long tail: standard notation, PDF, MusicXML, GP6 `.gpx` | Never, probably |

P1 is the milestone that matters: at the end of it the project is useful to its
author even if it stops there, which is the only honest definition of a
foundation.

## Legal position

Reverse engineering a file format for interoperability is expressly permitted
in the EU and UK under the Software Directive (2009/24/EC, Article 6), and the
implementations named above are used as reference under licences compatible
with this project's own.

"Guitar Pro" is a trademark of Arobas Music. Fretwork is not affiliated with,
endorsed by, or derived from their software, does not describe itself as a
Guitar Pro product, and ships none of their soundbanks, RSE data or artwork. It
reads a file format, which is not the same thing as copying a program.

The test corpus is transcriptions the author owns. Not redistributed, not
committed, and `.gitignore`d by extension.

## Open questions

- Is `.fw` worth having at all before the editor exists, or should P1 read `.gp`
  and write MIDI, deferring the format until something needs to save?
- QWidget or QQuickItem for the score canvas — settle with a scrolling
  measurement on a 250-bar score, not in advance.
- Whether the per-track LV2 chain is hosted in-process with lilv or delegated to
  Carla. In-process is more work and fewer moving parts.
- How much of the RSE mixer state in a `.gp` file is worth importing when the
  effects it names are Arobas's and cannot be reproduced.
