<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Long-term roadmap

Three directions past P4, written down with their dependencies, their prices
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
the one thing that edit exists not to do. Both are fixed, and both are now one
call into this: `pitchAt` where a typed fret becomes a pitch, `fretFor` where a
note crosses a string. Neither was caught by a test, because both were tested
on parts with no capo on them, and there is no capo anywhere in the corpus
either — so this is also the answer to how a bug like that survived a release
pass that found four other faults and an afternoon spent trying to break the
importer on purpose.

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

There was **no key signature anywhere in the model**, no pitch class, no
spelling: `grep` for it found nothing. Fretwork knew that a note was MIDI 63
and did not know whether that was D♯ or E♭ — which is fine for playing it, and
is the whole problem for writing it down. Layer 1 below is now built and the
rest of this section is written as it was, against what came before it.

### The four layers, smallest first

**Layer 1 — pitch classes and spelling.** **Done**, in `src/model/key.{h,cpp}`,
written **2026-08-31**. A key signature on every master bar (defaulting to
none, which is what a score that never says means), a spelling of a letter and
an accidental and an octave, and the rule between them: MIDI 63 is the D♯ of B
major and the E♭ of B♭ minor, and neither needed anything but the five
accidentals at the head of the staff. The mode names a key and does not spell
it — B major and G♯ minor are five sharps either way.

A note *outside* the key is where a spelling layer either stays honest or
starts inventing, so the rule there is the plainest one that does not produce
nonsense: the smallest accidental that reaches the note, and where a sharp and
a flat are equally small, the one the key is already written in. That gives F♯
in C major, G♭ in F major, and — the case that rules out following the key's
direction alone — a plain D in C♯ major rather than the C double sharp that a key with
every letter already sharpened would otherwise produce. It is a default and
not an analysis: which spelling a chromatic note actually wants depends on
what it is doing, and that needs a layer that can see the music around it.

The invariant it stands on is that whatever gets written sounds what it was
spelled from, tested over every pitch on a piano against all fifteen
signatures in both modes. A spelling layer that is merely plausible and does
not round-trip is one that will put a wrong note on a page.

The signature is read from gpif, kept on the master bar the way the time
signature is, round-tripped through `.fw` as an optional field — so no format
version moved — and printed by `--info` where a score has one. It is *not*
editable and nothing acts on it yet: the theory layer describes and never
corrects, and this is the description.

**Layer 2 — analysis, read-only.** **Done**, in `src/model/analysis.{h,cpp}`,
written **2026-08-31**. How long each of the twelve pitch classes sounds,
correlated against Krumhansl and Kessler's profiles of what each key sounds
like; best match wins. Duration and not note count, because a semibreve is not
a passing quaver and counting them the same is how a key gets decided by
ornaments. Over the whole score or over a `Passage` — a run of beats in one
voice, which is what a selection is — and the notes outside the key come back
by id from the same population, so a caller cannot be told about a note that
was never counted. Drum kits and dead notes are not pitches and are not in it.

The question this was put here to answer has been answered: **it is wanted**.

Two things it turned out to be worth saying out loud. The **signature is solid
and the mode is a judgement**: the accidentals fall straight out of the pitch
content, while telling a key from its relative minor is a reading of where the
weight sits. Every test here is written so that the runner-up is visible, and
`--info` offers both readings where they share a signature rather than picking
one and sounding certain. And **the analysis is usually the only thing in a
tablature file that says anything at all** about key, since the signature is
Guitar Pro's untouched default in all five transcriptions in the corpus — the
opposite of the situation printed music is in.

It was checked against a second implementation of the same method written from
the `.fw` JSON, which agreed on the note counts and the rankings. Worth doing:
the first version of the major profile here had the tritone missing from it,
which shifted the fifth's weight onto the wrong degree and handed every major
key to its relative minor. The answers stayed entirely plausible, because the
relative minor is exactly where a bad key guess lands.

**Layer 3 — the fretboard overlay.** **Done**, in `ScoreView::paintFretboard`,
written **2026-08-31**. The notes of the key marked on the strings, roots
distinguished, and the frets under the hand lit. For a guitarist this is the
payload — it is what turns "this song is in C minor" into "these are the frets
I can play".

**It is not drawn on the tab, and this section used to say it would be.** The
horizontal axis of a stave of tablature is *time*. A fret is a number written
along that axis, not a place on it, so there is nowhere on the staff that means
"the fifth fret" for a scale to be drawn at — the fifth fret is wherever a 5
happens to have been typed, which is a fact about the music and not about the
instrument. Marking the staff can answer a different and smaller question,
which is *which notes in this piece are outside the key*; the analysis layer
already returns exactly that list and nothing yet draws it. Answering "which
frets is this key" needs a neck, because a neck is the thing that has frets on
it as positions.

So the overlay is a neck lying over the page. **Over** rather than in: it does
not scroll with the music, it is on no page, and it is not something that would
ever be printed — the same reasoning that made the tuner a band of its own,
reaching the opposite conclusion about *where* only because this one is read
while reading the music rather than instead of it.

Smaller decisions worth keeping. Fifteen frets and not twenty-four, since the
useful part is where a hand goes and the dusty end is the same shape again on a
board half as wide per fret. Nearly opaque rather than half, because a neck
with a stave showing through it is two diagrams in the same place and neither
can be read. The key is named on the panel and every root carries its letter,
because an overlay of unnamed dots is a puzzle rather than an answer. Inlays at
the frets an instrument marks, behind the strings where they are on the thing
itself, because that is how a player finds the ninth fret without counting to
it.

And the key it draws is the one the circle of fifths is turned to rather than
the one the piece was analysed as — the same key for both, kept on the session.
A window offering the chords of G major while showing the scale of C minor
would be a window arguing with itself.

The hand position was the weakest part of the first version and is now the
part with the best answer, which arrived from the opposite direction to the
one predicted here. It said a hand is a property of a passage rather than of
one note, and that the honest version would read a phrase and ask the solver.
It is a property of a passage, and the solver is not needed for it at all: the
score already says where the hand is, because it says which frets are being
played. The overlay lights the span of fretted notes in the bar the caret is
in, and open strings are not counted, since a string that needs no hand says
nothing about where one is.

Worth keeping as a note about the roadmap rather than about the code. The
prediction reached for the newest machinery in the project when the answer was
sitting in the document the whole time.

**Layer 4 — the circle of fifths, and writing.** **Done**, in
`src/model/chord.{h,cpp}` with `Editor::insertChord` behind it, written
**2026-08-31**. The circle is a control and not a diagram: turning it picks a
key, the seven chords of that key are the row under it, and pressing one writes
a real beat with real notes on real strings at the caret. Borrowed chords come
from the same table, named in the parallel key they came from so that a flat
sixth in C minor is the A flat it is.

The part worth reading is the voicing rule, because it is the difference
between this being useful and being a chord dictionary nobody trusts: **find
the lowest string that can sound the root under the hand, and from there take
the lowest chord tone in reach on every string above it.** Strings below the
root are left out, since a chord standing on its third is a different chord.
That is one paragraph of code and it produces the shapes that are actually in
the books — C comes out x32010, G 320003, A minor x02210, and F under a hand at
the first fret comes out 133211, which is the barre. The tests assert exactly
those, because no argument about how principled a rule is survives it getting
an open C wrong.

One correction it needed. Everywhere else in the program an open string is free
— a single note on one needs no hand at all — and here it is not: a hand
holding a shape at the first fret is lying across the strings and cannot also
be leaving them open. Without that, an F came out as 103211, which has every
note of an F in it and can be played by nobody.

Writing is the thing this layer was put last for, and the rule that keeps it
honest held: it writes only when asked, it refuses whole and says why where the
instrument cannot hold the chord, and one undo takes the whole chord back
including the beat it made. Nothing here inspects a score or corrects one.

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

**Done**, in `src/audio/mackie.{h,cpp}` and `Session`, written **2026-08-31**.
The decoding is its own thing and knows nothing about tracks or plugins, which
is what lets a published protocol be tested against its own published numbers
with no controller plugged in: transport on notes 91 to 95, the eight encoders
on controllers 16 to 23, solo and mute on notes 8 to 23, and a fader as a whole
channel's pitch bend.

Two things learned by driving it rather than reading about it. An encoder sends
*movement* and not a position -- bit six is the direction and the low six bits
are the count -- which is why a fast spin arrives as one message saying five,
and why the knob it drives moves from where it is rather than jumping to where
the hardware thinks it points. And a surface sends the release as well as the
press, so a decoder that dropped note-offs would make a held button impossible
without anyone noticing until they tried one.

The port is remembered between runs, because choosing a controller every
morning is the sort of thing that makes a feature not worth having.

Proved end to end on this machine rather than by inspection: a Mackie encoder
message sent into the graph moved a guitarix amplifier's MasterGain from 20 to
8.75, and the change reached the rig on disk.

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

**Architecture decision, made once and settled:** **PipeWire**, in
`src/audio/midiinput.{h,cpp}`, written **2026-08-31**. It was checked rather
than assumed, the way the lilv-versus-Carla question was. What settled it is
that nothing is given up by choosing it: PipeWire's bridge already exposes
every hardware port on this machine under its own name — `Midi-Bridge:Minilab3
MCU/HUI`, `Midi-Bridge:Minilab3 MIDI` and the rest — so a controller with four
ports is four things to link to rather than one thing to demultiplex, which is
exactly the split this section says the goal has. It adds no dependency, since
PipeWire is already an optional one and this is optional in the same way. And
an ALSA sequencer client alongside the node the program already owns would have
been a second idea of the graph.

Three things it turned out to need, none of them obvious from the outside:

- **Nothing links a MIDI capture stream.** `PW_STREAM_FLAG_AUTOCONNECT` does
  not, and `target.object` names a *node* — but what somebody wants to listen
  to is a port, since a controller is four of them on one node. So it watches
  the registry and makes the link itself, which is what `PortedOutput` already
  does at the other end of the graph and for the same stated reason.
- **A stream's node id is not assigned when its ports are announced**, so
  "which of these ports is mine" cannot be answered as they arrive. Every port
  is kept and sorted out afterwards by node *name*, which does not race.
- **PipeWire carries universal MIDI packets now**, not raw bytes. They are
  converted back to MIDI 1.0 on the way in, because nothing above wants a
  second way to say "note on"; the deprecated raw-byte control is still
  accepted, since it is the same parser with nothing in front of it. That
  conversion is behind a header check rather than assumed, and the reason is
  worth keeping: **this machine has a newer PipeWire than the distributions the
  CI builds against**, so the first version of it compiled here and broke the
  build for everybody on an older one. Where the header is missing, so is the
  control type that needs it, and MIDI arrives as raw bytes — which is exactly
  what those servers send.

### 2b. Note entry — the harder half

A keyboard is a fast way to type pitches, and a guitar score is not written in
pitches. Every key pressed has to become a string and a fret, which is the
solver again, now running live with a hand position to respect. Press a C major
triad and the program has to choose a *shape* — and a shape is not three
independent choices, which is exactly the cost function described above.

Two modes, and they are different features:

- **Step entry.** **Done**, written **2026-08-31**. A key press writes a note
  at the caret, and the caret moves on when the hand comes off — which is the
  whole of the timing policy, and is why this half was worth doing first. A
  chord is simply the keys held down together: no quantisation, no argument
  about a note that arrived forty milliseconds early, and nothing that needs a
  clock.

  The solver does the work the roadmap said it would. Played E, G and B in
  turn, the notes land on the top string at frets 0, 3 and 7, the hand
  following up the neck; the C major triad held after them comes out as fret 5
  on the G string, 5 on the B and 3 on the top — a real close-position shape at
  the fifth fret, chosen because that is where the hand already was. Three
  independent choices would not have produced it.

  A held chord is one undo. The beat is rewritten as each key lands, so a triad
  arrives as three commands, and three presses of undo to take back one chord
  would be three too many. The merge has a trap in it worth knowing about: it
  has to adopt the notes the *newer* command wrote, or undoing leaves a chord
  behind.
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
| `sl`, `slu` | slides | imported, played and drawn — **still written as "not stated"**, see below |
| `vb` | vibrato | **have** |
| `hm`, `hp` | harmonics, pinch harmonics | **have**; `hp` only where gpif says pinch |
| `tr` | tremolo picking | **have** |
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
The architecture lists standard notation as "possibly never" because a layout
engine is many people over many years. Emitting the data another program lays out costs a
fraction of that, and it is most of the value.

**One field could not be settled at all.** The sample writes `dot` as 1 on 922
of its 992 beats, which cannot mean "dotted" — that would make Für Elise almost
entirely dotted, and it contradicts the `dur` values sitting beside it. No
second file exists to check against. This writes `dot` as the number of
augmentation dots, which is the only reading that makes musical sense and the
one the field's name gives, and records here that the single available example
disagrees in a way one example cannot resolve.

**What the slide fields taught, which was not what was expected.** All four
wishlist techniques are built, and one of them still cannot be exported. `sl`
and `slu` are integers rather than flags -- every note of every pack in the
library carries -1 -- so a value would have to mean something, and nothing here
says what. Not one pack in the library contains a slide to read the convention
off. So they stay at -1, which is the format's own way of saying nothing.

That is worth writing down because it is the opposite lesson to the one this
document keeps recording. Everywhere else, the answer was to go and measure
rather than assume. Here the measurement was taken -- every note of every
available pack -- and it came back empty, and the right response to an empty
measurement is to keep saying nothing rather than to fall back on the guess
that was going to be made anyway. A number invented here would not sit in a
file doing no harm: a practice program would mark a learner against it.

The same reasoning, more finely, on harmonics. `hm` is written because the
field is a boolean in every real pack and the note either is a harmonic or is
not. `hp` is claimed only for the kind gpif itself calls a pinch -- a `semi`
harmonic is played like one here, and saying so in somebody else's format would
be an inference recorded as a fact.

### The staging

**Stage 1 — the walking skeleton.** **Done**, in `src/export/feedpak.{h,cpp}`
behind `--feedpak`, written **2026-08-31**. A manifest, an arrangement per
fretted part, a stem per part plus the mix from the existing renderer, no
notation file, and only the techniques this program can honestly claim.

It was built the way gpif-format.md was, by unpacking real files, and it
answered the two questions this section raised:

- **String numbering is the same as Fretwork's — nought is the lowest.**
  Measured, not assumed. Every pack here is in standard tuning and writes six
  zeros, which says nothing, so the check was to read a known melody off the
  numbers: the Ode to Joy pack's `s:4 f:7` and `s:5 f:5` come out as F♯ and A —
  the tune — only if nought is the low string. Counting from the other end puts
  a leap down to A2 in the middle of a stepwise melody.
- **`tuning` is semitone offsets from standard, not pitches.** Six zeros cannot
  be pitches, and the melody reads correctly when zero is taken to mean
  standard. Stated as the inference it is: no pack on this machine is in a
  non-standard tuning, so nothing here proves it.

And it found a bug that only a real transcription could show. A drum kit
imports with a tuning of **six zeros rather than none**, so the first version —
which asked whether a part had strings — wrote the kit an arrangement of frets
on a drum. Asked of `isPercussion()` now, which is the same correction the
importer already documents about reading a kit from its programme number.

What the skeleton does not do yet, beyond the techniques: the stems are WAV, so
a pack of a five-minute piece is about 280 MB where a real one is a few. Packs
use Ogg for the mix, and encoding it needs a dependency this does not have.

**Stage 2 — honest techniques.** Slides, vibrato, harmonics, tremolo. Wishlist
items with a reason attached, which is what a wishlist item is waiting for.

**Done, on 2026-09-01.** Vibrato and tremolo landed on 2026-08-31, slides and
harmonics the day after. Taking the first pair: they are imported, played, drawn
and exported. Each needed a different mechanism, and the difference is the
useful part: a vibrato is one note with something done to its pitch, so it is a
bend curve and reuses everything the program already knows about moving a
pitch; a tremolo is the same note struck again and again, so it is several note
events and can reuse none of it.

Two things learned that outlive the code. A vibrato has to be measured in
hertz, not in beats — it is a gesture of the hand and does not speed up because
the piece does — which is why `notesFor` now builds a clock. And gpif's tremolo
value is a note against a *semibreve* while everything in this model is counted
in quarters, so the file's `1/8` is a half here; reading it unchanged made every
tremolo four times too fast, and the tests caught it as four times too many
strikes.

**Slides and harmonics followed on 2026-09-01.** This paragraph said, the day
before, that neither appeared anywhere in the test corpus and that both were
therefore waiting on evidence rather than on time. Half of that was false, and
the way it was found out is worth keeping.

On 2026-09-01 two more transcriptions arrived — ZZ Top's *Sharp Dressed Man* and
Amon Amarth's *Twilight Of The Thunder God* — and counting the techniques in
them meant counting the techniques in everything. **The five files that were
already here carry 42 slides between them**, in four of the five files, using
flag values 1, 2, 4 and 16. There was never an evidence problem. There was a
claim made without a measurement, in a document whose whole argument is that
measurements beat assumptions, and it stood for a day because it sounded
plausible.

The lesson is narrower than "check things", which nobody ever acts on. It is
that **a claim about the absence of something is a measurement like any other,
and it is the one most likely to be made by not looking.** Vibrato was counted
(eighty). Tremolo was counted (eight). Slides were *asserted* to be zero, and a
census would have taken the same two minutes it eventually took.

The same day, and in the same document, it happened again: P8.4 was written down
as a few days of work to add PDF export to a program that has had `--pdf` since
P2. Two absence claims in one morning, both wrong, both cheap to check — and
the second one was contradicted by a sentence in architecture.md that the same
session had edited an hour earlier, which listed PDF among the things export
already does. **Reading past the answer is the failure mode, not missing it.**

Harmonics were genuinely absent, and are absent no longer: seven natural
harmonics in the ZZ Top file and five marked `semi` in the Amon Amarth one.
That was enough to settle the question this paragraph used to pose. **A harmonic
note's `Midi` carries the fretted pitch, not the sounding one** — for all twelve
harmonics in both files, `Midi` equals `tuning[String] + Fret` exactly, the same
formula that holds for all 32,140 plain notes in the corpus. Playing `Midi` as
it stands therefore sounds a harmonic at the pitch of the note under the finger,
which is the confident wrong octave that was worth being afraid of.

What a harmonic *does* sound is in `src/model/harmonic.{h,cpp}`, and the good
thing about it is that it is physics rather than convention: a partial n sounds
12·log₂(n) semitones above the open string and has nodes at 12·log₂(n/(n−k))
frets along it, which is true of a guitar and not merely true of a file format.
So it can be derived and then checked against what a real file happens to carry,
instead of being read out of one file and hoped about. Three details earned
their comments — the offsets are not rounded, because the seventh partial is 31
cents flat of a minor seventh and rounding belongs at the edge; where nodes
coincide the lowest partial wins, because that is what the string does; and the
tolerance for matching a stored node cannot be made unambiguous, since nodes of
high partials sit 0.077 of a fret apart, so the comment says so rather than
picking a round number that hides it.

The slides needed no new physics and one new shape: a pass over the finished
events, because a connecting slide arrives at the *next* note on its string and
nothing knows what that is until the whole track exists. It runs before let
ring, so a note held into the next bar slides when the hand moved rather than
when the sound stopped. What could not be derived was how far an unwritten slide
travels — gpif says that one happens and never says how far — so three semitones
is invented, and the constant defining it says the word "invented" out loud.

And a bug the slides exposed on their way through: a pack's `bn` field was
taking the largest value on the bend curve, which was fine while only written
bends made curves. Vibrato already leaked into it, quietly, at 0.3 of a
semitone. A slide would have leaked five. `bn` now reports only what was written
as a bend, because a practice program marks a learner against that number.

**Stage 3 — notation. Done on 2026-09-01**, in `src/export/notation.{h,cpp}`.
Measures, signatures, tempo, a staff with a clef, voices, and beats carrying
`dur`, `dot` and `midi`, written as `notation_<part>.json` with the manifest
pointing at it. Notation *data*, not engraving: nothing here decides where a
note head sits.

Three things it taught, none of them predicted.

**Clef assignment is not a rule about ranges.** That was the plan, and the plan
was wrong. The corpus holds a guitar in B standard whose highest open string is
MIDI 59, and a six-string bass whose highest is 64 — so the ranges overlap, and
a rule reading them puts each instrument in the other's clef. The file already
says which instrument it is. There was nothing to infer.

**The sample this schema was read off is a transcription of a recording, not of
a page.** Its beats do not tile its bars, its rests are absent rather than
written, and its onsets are a performer's. Everything read from a `.gp` was
written down rather than played, so what this emits is better formed than what
it was learnt from — rests included, bars adding up. That is asserted rather
than hoped for.

**Tuplets are the honest limitation.** The format's beats carry `t`, `dur`,
`dot` and `notes` and nothing else, so there is no field for a triplet. A tuplet
is written as the value it is notated with, which is what a reader draws, and a
bar holding one adds up to more than it lasts: 296 voice-bars out of 5,478
across the seven files, every one over rather than under. Nothing is dropped and
no time is lost, because `t` is on every beat and is the truth about when it
sounds — which is also how the sample works, its bars not tiling either.

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
| **P6.1** | Pitch classes, key signature, spelling | nothing | **done** |
| **P6.2** | Key and scale analysis, read-only | P6.1 | **done** |
| **P6.3** | Scale overlay on the fretboard | P6.0, P6.2 | **done** |
| **P7.0** | MIDI input plumbing (PipeWire, decided once) | nothing | **done** |
| **P7.1** | Control surface: transport, encoders → LV2 controls | P7.0 | **done** |
| **P6.4** | Circle of fifths, chord insertion | P6.0, P6.3 | **done** |
| **P7.2** | Step note entry from the keyboard | P6.0, P6.4 | **done** |
| **P8.0** | feedpak export, walking skeleton | stems (done) | a week |
| **P8.1** | Slides, vibrato, harmonics, tremolo | — | **done**, 2026-08-31 to 2026-09-01 |
| **P8.2** | Notation data export | P6.1 | **done**, 2026-09-01 |
| **P8.3** | MusicXML export | P8.2 | a week |
| **P8.4** | PDF export | — | **done**, and was already done when this row was written |
| **P8.5** | GP6 `.gpx` import | a BCFS/BCFZ decompressor, in Rust | open |
| — | Real-time MIDI recording | P7.2, a quantisation policy | open |
| — | feedpak import | a decision, not code | open |

Three observations about that order:

1. **The solver and the spelling layer are the whole foundation**, and neither
   is large. Two small pieces of pure, testable, dependency-free code unblock
   everything else in the table. Written first, they are also the best possible
   answer to the review's finding that the newest code is the least tested —
   these can be test-first in a way the LV2 host never could. Both are now
   measured rather than predicted, and the prediction held: the solver is 550
   lines of header and implementation against 278 of test, the spelling layer
   346 against 174, and between them 34 cases that need no fixture, no corpus
   and no window. Both suites run in under a millisecond.
2. **The control surface jumps the queue.** It sits in the middle of the table
   despite being goal 3, because it depends on nothing but its own plumbing and
   produces the most persuasive demonstration per day spent. If only one item
   on this page gets built, it should be that one.
3. **feedpak export is last to start and first in value**, which is
   uncomfortable but correct: it is the only item whose quality depends on
   things not yet built, and shipping a pack exporter that silently omits every
   slide would break the one principle the project is actually known for.

### The long tail, folded in from P5

**P5 was retired on 2026-08-31 and its four contents given a row each above.**
It was one row in the architecture's phase table — "the long tail: standard
notation, PDF, MusicXML, GP6 `.gpx`" — with "never, probably" in the column for
when it ends. P8.2 was already there, scheduled on its own account because the
feedpak notation file depends on it; the other three joined it.

The reason for the move is that a single verdict had stopped fitting four items
that no longer share one. P5 was written before the harmony work, when notation
of any kind meant a layout engine and everything in the bucket was equally
distant. P6.1 changed that without setting out to: **pitch spelling is the piece
notation cannot be faked without** — F sharp against G flat is not cosmetic, it
decides which line the note sits on and whether it carries an accidental — and
it now exists, tested, in `src/model/key.cpp`. Half the bucket became a week's
work while the other half stayed where it was.

Kept apart, they read honestly:

- **P8.2, notation data.** Unblocked, a week, and already the dependency of the
  feedpak notation file. Not engraving: the data another program lays out.
- **P8.3, MusicXML.** The strongest of the four by argument rather than by
  price. It is the only interchange format with no vendor behind it, and
  therefore the honest answer to "can I have this file in something else" — a
  question this program otherwise refuses to answer, because it will not write
  `.gp`. Export only; reading it is a separate decision.
- **P8.4, PDF.** **Already done, and had been for months when this row was
  written on 2026-09-01.** `--pdf` lays a track out and draws it, and has since
  P2. The row was written on the assumption that the paged score view was the
  new thing and printing was the missing half; nobody ran `--pdf --help` before
  writing it down. See the note below, because this is the second time in one
  day.
- **P8.5, GP6.** Unchanged and correctly last. `.gpx` wraps the same document
  in a bit-level compression of its own, so there is a decompressor to write
  before there is any XML to read, and that belongs in Rust with the other
  binary importers. The only thing built so far is a *refusal*: the importer
  recognises a BCFZ container and says which format it is *not*, rather than
  misparsing it.

**Renumbering them does not move any of them closer.** P8.5 is exactly as far
away as it was as part of P5, and an engraver is still probably never. What the
fold buys is that the plan no longer says "never, probably" over a week of work
that is unblocked today, and no longer implies that four items with four
different dependencies will arrive together.

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
