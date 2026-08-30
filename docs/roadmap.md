<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Long-term roadmap

Three directions past P5, written down with their dependencies, their prices
and their decision points. This sits between the two documents that already
exist: the phase table in [architecture.md](architecture.md) is what is being
built now, [wishlist.md](wishlist.md) is what has been thought of and not
promised, and this is the middle — directions committed to in principle, with
the order and the prerequisites worked out, but not yet scheduled.

Written **2026-08-28**, against the model in `src/model/score.h`, the editor
API in `src/edit/editor.h`, and a real `.feedpak` unpacked from
`~/.local/share/feedback/library`.

## What these three do to the project

Each of the three pushes a different boundary, and it is worth naming which
before any of them is started, because wishlist.md already refuses one
direction outright:

- **fee[dB]ack integration** makes Fretwork an *authoring tool* — something
  whose output another program consumes.
- **Scales and chords** makes it a *composition tool* — something that helps
  write music that did not exist, rather than reproducing music that did.
- **MIDI control** makes it a *performance instrument* — something with
  hardware attached that a person plays rather than operates.

None of them is "becoming a DAW", which stays refused. But the third is the
closest to that line, and the answer to why it is not a DAW has to stay
"because there is no recording, no arrangement of audio, and no timeline that
is not the score" — which is a promise about what the MIDI work will *not*
grow into, and it is worth making now rather than arguing about later.

## The prerequisite all three share

**A fretboard solver: given a pitch, a tuning and a capo, which string and
fret.** **Done**, in `src/model/fretboard.{h,cpp}`, written **2026-08-31** and
tested by `tests/fretboardtest.cpp` against no corpus at all.

When this was written the only pitch-to-fret arithmetic in the tree was one
line — `editor.cpp:1910`, `const int fret = note.midi - tuning.at(string)` —
which answered "what fret is this pitch *on this string*" and was used by
`moveNoteAcross`. Nothing chose the string.

That line is also wrong, and finding out how is the best argument for having
built this that the project is going to get. It leaves the capo out. So does
`Editor::midiFor`, which is what typing a fret number goes through. The two of
them disagree with `Editor::setCapo`, which moves every note's pitch by the
capo and leaves the fret numbers alone — so on a track with a capo on it, a
note that is typed sounds a capo's worth flat against every note that was
already there, and `Alt`+arrow writes a fret that changes the pitch, which is
the one thing that edit exists not to do. Neither site is fixed here: this
document is where the fix was worked out and the fix itself is an editor
change with its own tests to write. Both are one call each once it is made.

That is the missing primitive under two and a half of the three goals: MIDI
note entry has to pick a string for every key pressed; chord insertion has to
pick a shape; and the feedpak exporter has to write `s` and `f` for notes that
came in as pitches. Building it once, properly, before any of the three is the
difference between one good component and three private half-versions of it.

What "properly" means here is that it is not arithmetic, it is a preference
with a hand attached. The same pitch is available in up to six places, and the
right one depends on where the hand already is, what else is sounding in the
same beat, and whether the string is already taken. The honest shape is a small
cost function over candidates — distance from the previous position, distance
from the previous *chord shape*, strings already occupied in the beat, a
penalty for open strings in a phrase that is not using them, and a hard refusal
where nothing fits. wishlist.md already describes half of this under moving a
phrase to different strings, which is the same solver asked a different
question.

What that came out as is four weights in a fixed order, which is the only
honest way to write a preference down: **moving the hand** dominates, because
it is the expensive physical act; **an unwanted open string** is worth moving
the hand two frets to avoid and not three, being a break in the sound rather
than an effort; **a crossed pair** is worth avoiding for free and never worth
moving the hand for, since those voicings are unusual rather than wrong; and
**height up the neck** is a tie-break that can never outweigh any of them, so
that a note with nothing else said about it lands in the first position.

Two ways in, because two of the three callers are different questions.
`choose()` is causal — it knows what has been played and nothing about what
comes next, which is all a note arriving from a keyboard can know. `phrase()`
reads the whole line first and searches every path through it, which is what a
player does before playing one, and it answers differently: B3 followed by E5
puts the B at the ninth fret rather than the fourth, because the fourth is
cheaper for the B alone and leaves the hand nowhere near the E. `chord()` is
the shape question, and refuses whole rather than dropping a note, on the same
grounds the editor refuses a transposition that would run off the neck.

The tests it is worth having an opinion about are the two that a guitarist can
check by eye: the notes of an open E come out as 0-2-2-1-0-0, and the notes of
an F as the barre, 1-3-3-2-1-1. No account of a cost function is worth reading
if it gets those wrong.

---

## 1. Scales, chords and the circle of fifths

Put second in the request and first in the plan, because it is the cheapest of
the three, it depends on nothing external, and — the reason that matters — its
output is a **prerequisite for the notation half of fee[dB]ack integration**.

### What already exists

More than it looks. `Note::midi` is the sounding pitch, stored on every note
alongside the string and fret, so every harmonic question the feature wants to
ask is askable over data already in the model. `Track::tuning` is MIDI pitch
per string. `Beat::notes` is already a list, so a chord is not a new concept —
it is a beat with more than one note in it, and `Editor::setFret` already
writes into exactly that structure.

### What is missing

There is **no key signature anywhere in the model**, no pitch class, no
spelling. `grep` for it finds nothing. Fretwork currently knows that a note is
MIDI 63 and does not know whether that is D♯ or E♭ — which is fine for playing
it, and is the whole problem for writing it down.

### The four layers, smallest first

**Layer 1 — pitch classes and spelling.** A key signature on the score
(defaulting to none), a pitch class type, and spelling rules that turn MIDI 63
into D♯ in B major and E♭ in B♭ minor. Small, pure, entirely testable, and it
is the thing standard notation needs before it can draw a single accidental.

**Layer 2 — analysis, read-only.** What key is this in, from a pitch-class
histogram over the score or the selection, weighted by duration. Which scale
does the selection fit, and which notes are outside it. This is the layer that
can be built and shown before anything writes anything, which makes it the
right place to find out whether the feature is actually wanted.

**Layer 3 — the fretboard overlay.** The scale drawn on the tab: the notes of
the current key marked on the strings, root notes distinguished, and the
positions a hand can reach without moving highlighted. For a guitarist this is
the payload — it is what turns "this song is in E minor" into "these are the
frets I can play". It needs the solver, and it needs a display decision: an
overlay on the score, or the band-across-the-bottom pattern the tuner already
established. The tuner's argument applies again — this is a thing done *to the
instrument*, not to the document — which suggests a second band and reuse of a
layout that already works.

**Layer 4 — the circle of fifths, and writing.** The circle as a control rather
than a diagram: pick a key, get its diatonic chords, drop one into the score at
the caret as a real beat with real notes on real strings. Borrowed chords and
the relative minor come free from the same table. This is the layer that
justifies the phrase "for music creation", and it is the only one of the four
that can put a wrong note in somebody's score, so it is last.

### Risks

The refusal principle is harder to hold here than anywhere else in the program.
Everything Fretwork currently refuses is refused because it is *unambiguous* —
a note off the neck, a tempo of 1100, a bar that does not add up. Harmony is
not like that: a chord outside the key is a decision, not an error, and a
program that marks it as one is a program that argues with its user about
music. The rule that keeps this honest is that **the theory layer describes and
never corrects** — it may say "this is outside E minor", it may never
adjust anything, and nothing it knows may ever refuse an edit.

Second risk: scope. Chord *diagrams* are read past on import and not kept
(`fwformat.h` says so), so drawing chord grids is two features — keep them in
the model, then draw them — and it is a different feature from knowing what a
chord is. Do not let one become the other.

---

## 2. MIDI control

The hardware is present and its shape decides the design. `Minilab3` appears on
ALSA as **four ports**: `Minilab3 MIDI`, `DIN THRU`, `MCU/HUI`, and `ALV`. A
`MiniFuse 2` is also there with a DIN MIDI port of its own, and PipeWire is
already exposing the sequencer as UMP-MIDI2 (`client 144: PipeWire-System`).

There is **no MIDI input in the tree at all**. `src/export/midi.cpp` writes
files and reads nothing; the only MIDI that has ever entered this program came
out of a `.gp`.

That four-port layout matters because it splits the goal cleanly into two
features that share almost no code, and the split should be made explicit
rather than discovered halfway:

### 2a. Control surface — the easy half, and worth doing first

`MCU/HUI` is Mackie Control, which is a published protocol every DAW speaks.
Transport buttons, the eight encoders, the pads. Mapped onto things Fretwork
already has:

- Transport → `play`, `stop`, `seek` on `Session`, which already exist and are
  already driven by the graph transport, so this is a third caller of a path
  that has two.
- The eight encoders → the **LV2 chain's declared controls**, which is the
  natural fit and the reason this is worth doing. The mixer already draws every
  knob from what the plugin says about itself — its name, its range, whether it
  is a switch or a list — and `--knob 0:0:Drive=8` already addresses one by
  track, position and published symbol. An encoder is one more way to send the
  value that path already carries. Turning a real knob and hearing an amplifier
  change with no window in the way is the single most convincing demo the
  project could produce.
- Faders and the S/M buttons → the mixer, which already takes them live.

The cost is a MIDI input source and a mapping table. Almost nothing new has to
be *reasoned* about, which is the mark of the right first step.

**Architecture decision to make once:** ALSA sequencer directly, or PipeWire's
MIDI ports. PipeWire is the consistent answer — the program already owns a
PipeWire node with a considered position on clocks and callbacks, and adding an
ALSA seq client alongside it means two ideas of the graph. But it is worth
checking what PipeWire's MIDI ports cost in latency and in packaging before
committing, the way the lilv-versus-Carla question was settled.

### 2b. Note entry — the harder half

A keyboard is a fast way to type pitches, and a guitar score is not written in
pitches. Every key pressed has to become a string and a fret, which is the
solver again, now running live with a hand position to respect. Press a C major
triad and the program has to choose a *shape* — and a shape is not three
independent choices, which is exactly the cost function described above.

Two modes, and they are different features:

- **Step entry.** A key press writes a note at the caret and advances, the way
  `typeDigit` does now. This is a genuine speed improvement for anyone who
  plays keys, it is bounded, and it fits the editor's existing undo model with
  no new ideas.
- **Real-time recording.** Play along and have it transcribed. This needs
  quantisation, which needs a policy for what to do with a note that is 40ms
  early, and that policy is where every notation program has its worst
  arguments. It also breaks the one-note-at-a-time assumption the tuner's YIN
  detector holds — though not the same code path, since this is MIDI and not
  audio.

Recommend step entry, and treat real-time recording as a separate decision
taken after the solver has been used in anger.

### The line not to cross

MIDI in is the feature most likely to drag the project toward being a DAW.
The boundary that keeps it honest: **MIDI enters as an edit or as a control,
and never as a recording.** There is no MIDI track, no captured performance
sitting beside the score, and no timeline that is not the score's own. If a
feature request needs one of those, it is a different program.

---

## 3. fee[dB]ack integration

Put last in the plan and first in strategic value, because it is the one that
gives Fretwork a reason to exist to people who do not want a tab editor — and
because it depends on more of the other work than it looks like it does.

### What fee[dB]ack is, from its own files

An Electron app — "integrated audio engine, VST hosting, and amp modeling" per
its desktop entry — with a library of `.feedpak` files, tutorials named
`intro-bends` and `reading-the-highway`, and a `.sloppak` diagnostic. The
shape is a practice program: a scrolling note highway, your guitar listened to
and scored.

### Why the fit is unusually good

A `.feedpak` is a ZIP containing:

```
manifest.yaml                    title, artist, duration, per-arrangement tuning and capo
stems/full.ogg                   audio, one file per stem id
arrangements/keys.json           notes as {t, s, f, sus} plus ~20 technique flags
notation_keys.json               measures, time signatures, tempo, staves, voices, beats, midi
```

Read that list against what Fretwork already is. **A `.fw` is a ZIP holding
readable JSON.** Fretwork already renders one audio file per track. Fretwork's
timeline already resolves bars into seconds with tempo changes and swing
applied, which is exactly the arithmetic that turns a notated duration into
`t` and `sus` in seconds. And a `.gp` carries the notation that a practice pack
otherwise has to be authored by hand.

Nothing else on Linux holds both halves. TuxGuitar has the notation and cannot
render stems per track; a DAW has the stems and knows nothing about frets.
**The pack format needs precisely the intersection that this program is.**

### The field mapping, and what it reveals

From a real arrangement file, a note is `{"t","s","f","sus","sl","slu","bn",
"ho","po","hm","hp","pm","mt","tr","ac","tp","ln","vb","fhm","plk","slp","rh",
"pkd","ig"}`. Against `Note` in `score.h`:

| feedpak | Fretwork | State |
|---|---|---|
| `t`, `sus` | Timeline, in seconds | **have** |
| `s`, `f` | `Note::string`, `midi - tuning[string]` | **have** |
| `pm`, `ln` | palm mute, let ring | **have** |
| `mt` | `Note::muted` — dead note | **have** |
| `ho`, `po` | `Note::hammerOrigin` — one flag for both | **have**, needs splitting by pitch direction |
| `ac` | `Dynamic` | **have** |
| `bn` | bends, imported and played | **have** |
| `sl`, `slu` | slides | *wishlist* |
| `vb` | vibrato | *wishlist* |
| `hm`, `hp` | harmonics, pinch harmonics | *wishlist* |
| `tr` | tremolo picking | *wishlist* |
| `tp`, `plk`, `slp`, `pkd` | tapping, pick direction | not in the model at all |

This is the useful part of the exercise: **the export target names the missing
techniques, and they are the same ones the README already admits to.** Four
wishlist items stop being wishes and become requirements the moment this is
committed to — and a practice program is the worst place for them to be
missing, because "reading-the-highway" means a learner is being marked on
playing a slide the pack did not say was there.

The notation file is the second finding. It wants measures, time signatures,
tempo, staves with clefs, voices, and beats with `dur`, `dot` and `midi`.
Fretwork has bars, signatures, tempos, voices, beats and rhythms already. What
it lacks is clef assignment and **pitch spelling** — and spelling is layer 1 of
the scales work. That is the dependency that sets the order of this whole
document.

It is also worth noticing what this is *not*: notation **data**, not engraving.
P5 lists standard notation as "possibly never" because a layout engine is many
people over many years. Emitting the data another program lays out costs a
fraction of that, and it is most of the value.

### The staging

**Stage 1 — the walking skeleton.** Manifest, one arrangement, stems from the
existing renderer, no notation file, and only the techniques Fretwork can
honestly claim. Proves the round trip and is worth building early, because it
is how the format's undocumented corners get found — the same method that
produced gpif-format.md.

**Stage 2 — honest techniques.** Slides, vibrato, harmonics, tremolo. Wishlist
items with a reason attached, which is what a wishlist item is waiting for.

**Stage 3 — notation.** Once spelling exists.

**Stage 4 — import.** Read a `.feedpak` back. Whether this is wanted at all is
an open question, and it runs into the same argument as writing `.gp`: reading
a format is one risk, and handing people files to open in someone else's
program is a different one. The difference is that a feedpak is a *practice
pack*, not a document — it is closer to a render than to a save — which is
probably enough to make writing it fine and importing it unnecessary.

### The risks, which are real

- **The format is at 1.2.0 and the app is at 0.3.0.** Both numbers are going to
  move. An exporter written against one unpacked file is an exporter that
  breaks, and unlike gpif there is no corpus of a hundred files in the wild to
  measure against yet.
- **String numbering must be verified, not assumed.** Fretwork's string 0 is
  the lowest. The sample pack is a piano arrangement with an all-zero tuning
  and notes on `"s":2`, which says nothing about the convention for a guitar.
  Getting this backwards produces a pack that is playable, wrong, and wrong in
  a way that looks like a transcription error.
- **Stems mean the corpus problem returns.** A pack contains audio. A pack made
  from a transcription of a copyrighted song and shared is a different legal
  object from a `.gp` on somebody's disk, and the project's whole position
  rests on that line. Fretwork should write packs; it should never gain
  anything that distributes them.

### The question that changes this section

Whether fee[dB]ack is a third-party program or one of yours. The config
directory is `feedback-desktop` and holds `slopsmith-desktop.json`, with a
`~/.slopsmith` alongside — which reads like an internal name rather than a
downloaded app.

If it is external, all of the above stands: write against the format, expect it
to move, keep the exporter isolated behind one module. If it is yours, this
stops being integration and becomes **one product in two halves** — Fretwork
authors, fee[dB]ack practises — and the right move is not an exporter at all
but a shared format defined once, with the pack as an output of the same model
rather than a translation of it. That is a materially different piece of work
and a materially better one.

---

## Order, and why

| | Work | Depends on | Rough size |
|---|---|---|---|
| **P6.0** | Fretboard solver | nothing | **done** |
| **P6.1** | Pitch classes, key signature, spelling | nothing | days |
| **P6.2** | Key and scale analysis, read-only | P6.1 | days |
| **P6.3** | Scale overlay on the fretboard | P6.0, P6.2 | a week |
| **P7.0** | MIDI input plumbing (PipeWire, decided once) | nothing | days |
| **P7.1** | Control surface: transport, encoders → LV2 controls | P7.0 | a week |
| **P6.4** | Circle of fifths, chord insertion | P6.0, P6.3 | a week+ |
| **P7.2** | Step note entry from the keyboard | P6.0, P6.4 | a week |
| **P8.0** | feedpak export, walking skeleton | stems (done) | a week |
| **P8.1** | Slides, vibrato, harmonics, tremolo | — | weeks |
| **P8.2** | Notation data export | P6.1 | a week |
| — | Real-time MIDI recording | P7.2, a quantisation policy | open |
| — | feedpak import | a decision, not code | open |

Three observations about that order:

1. **The solver and the spelling layer are the whole foundation**, and neither
   is large. Two small pieces of pure, testable, dependency-free code unblock
   everything else in the table. Written first, they are also the best possible
   answer to the review's finding that the newest code is the least tested —
   these can be test-first in a way the LV2 host never could. Half of that is
   now measured rather than predicted: the solver is 550 lines of header and
   implementation against 278 of test, 22 cases, and it needs no fixture, no
   corpus and no window — the whole suite runs in under a millisecond.
2. **The control surface jumps the queue.** It sits in the middle of the table
   despite being goal 3, because it depends on nothing but its own plumbing and
   produces the most persuasive demonstration per day spent. If only one item
   on this page gets built, it should be that one.
3. **feedpak export is last to start and first in value**, which is
   uncomfortable but correct: it is the only item whose quality depends on
   things not yet built, and shipping a pack exporter that silently omits every
   slide would break the one principle the project is actually known for.

## What this costs, honestly

Every line of the table is a week that is not spent on the crashes in
[review.md](review.md), on GP3–GP5 import, or on getting the program packaged
so anyone can install it. This roadmap is three new frontiers opened by a
project with a bus factor of one, four days of history and eight unrecorded
segfaults.

The recommendation is not to narrow the ambition, which is right, but to gate
it: **nothing in this document starts until 0.1.0 is out, the LV2 crashes are
recorded and fixed, and someone other than the author has built the program.**
The three goals are all stronger for being reached by a project people can
install than by one that is still four days old and growing in every direction
at once.

## Open questions

- Is fee[dB]ack yours? It changes goal 1 from an exporter into a shared format.
- Does the theory work end at analysis and overlay, or is chord insertion
  actually wanted? Layers 1–3 are safe and useful; layer 4 is where a program
  starts making musical decisions on someone's behalf.
- MIDI through PipeWire or ALSA sequencer directly — worth measuring before
  choosing, since it is a decision that will not be revisited.
- Should the scale overlay be a band like the tuner, or drawn on the score?
- Is real-time MIDI recording wanted at all, or is step entry the whole of it?
