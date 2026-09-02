<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Changelog

## Unreleased

- **Notes can be recorded from a MIDI keyboard against the transport.** Arm
  Record beside the click, press Play, and what is played on the keyboard
  port is written into the current part as it lands, a bar at a time, one
  undo per bar. Step entry — a chord at the caret, the caret moving when the
  hand comes off — is still there for writing without a clock; this is for
  playing a part in.

  The part that needed deciding was what to do with a note that is forty
  milliseconds early, and the answer is written down in one place and tested
  rather than left to be discovered: a note rounds to the nearest line of a
  grid somebody chose (a semiquaver unless told otherwise), and the bar it
  belongs to is worked out from the rounded position, so a note early for a
  downbeat is on the downbeat. A note lasts to its release, the next key or
  the bar line, whichever is first; a release at least a cell before the
  next key leaves a rest, and a shorter gap does not. The bar always adds up.
  A bar played into becomes what was played; a bar left alone is left alone.

  Two things underneath had to change to make it accurate. A MIDI message
  now carries the moment it arrived, stamped in the graph's own callback,
  because the poll that drains messages was too coarse to place a fast
  passage. And the player notes the clock beside its position each block, so
  "where was the transport when that key went down" is answered to a
  millisecond. Neither is a recording: the stamp is used to find a bar and
  then dropped, and nothing is kept beyond the bar being played into.

  Stated limits: swung bars are placed on the straight grid, a note held
  across a bar line stops at it, and what was recorded is heard on the next
  play rather than the one it was played against.

  Proved by playing a scripted performance into it from a virtual port
  rather than by inspection: four crotchets, eight quavers, a held triad, a
  crotchet-rest-minim and a note forty milliseconds early for a downbeat all
  came out as written, the onsets landing within a millisecond of each
  other's spacing. That is also how the two entries below were found.

- **The transport runs to the end of the bars while recording is armed.**
  It stops a moment after the last note otherwise, which is right for a
  rendered file and useless for playing into a blank score, where every bar
  is past the last note: the first take stopped three seconds in.

- **Play uses the score as it is now, not as it was opened.** The order the
  bars are heard in and the clock that turns them into seconds were worked
  out once, when a file was opened, and a player built after bars had been
  appended or a repeat added stopped where the opened score had ended. Both
  are worked out again whenever the player is rebuilt.

## 0.3.0 — 2026-09-01

Everything a score can say, and two more ways out of the program.

The first half is the four techniques the practice-pack exporter turned from
wishes into requirements. A pack tells a learner what to play and marks them on
playing it, so a technique the pack cannot name is one nobody can be marked on
— which is what made them worth building, and what made getting them wrong
worse than leaving them out.

The second half is interchange. A practice pack now carries the notation as
well as the performance; a score can be written as MusicXML, which is the
honest answer to "can I have this file in something else"; and the first Rust
in the project sits behind a C ABI so that a file from an older Guitar Pro is
named rather than merely refused.

Two of the entries below are corrections, both of claims that something was
missing, both wrong in the same way. They are here rather than quietly fixed
because the pattern is the useful part.

- **Vibrato is played, drawn and carried.** It was read out of a `.gp`, stored
  on the note, and then dropped on the floor: not heard, not printed, and
  written into a pack as absent. It is played as what it is — a wobble of the
  pitch — which needed no new machinery, because moving a pitch continuously
  and putting it back is what a bend already does. Five and a half times a
  second and thirty cents either side, measured in real time rather than in
  beats, because a vibrato is a gesture of the hand and does not speed up
  because the music does. A note too short to hold one cycle gets none: a
  single lurch of pitch on a semiquaver is not vibrato, it is out of tune. On
  the page it is a wave over the note it belongs to.

- **Tremolo is played, drawn and carried**, and it could not reuse any of that.
  A vibrato is one note with something done to its pitch; a tremolo is the same
  note struck again and again, so each strike is a note of its own. How fast is
  what the file says rather than a guess — a held note repicked in quavers and
  one repicked in demisemiquavers are two different effects — and on the page
  it is slashes across the stem, where printed music puts them.

- **Harmonics sound where they actually sound.** Every other technique leaves
  the pitch alone; a harmonic does not, and the program did. A harmonic note's
  `Midi` in a `.gp` is the pitch under the finger, not the one you hear, so
  every harmonic in every score played two or three octaves low — confidently,
  and looking like a bad transcription rather than a bug. What a harmonic
  sounds is now worked out from where the string is touched, which is physics
  rather than a file format convention and is therefore checkable: the tests
  assert the notes a guitarist would name, and the numbers in them were worked
  out before the code was.

- **Slides move the pitch.** They had been imported since P1 and had never done
  anything at all. The two that connect glide to the next note on the string,
  which is the only case the file gives a destination for. The rest sweep a
  fixed three semitones, because the file says a slide happens and never says
  how far — an invented number, and the comment defining it says so. On the
  page they are the diagonal printed tablature has always used.

- **A pick scrape is no longer dropped in silence.** Slide flag `0x40` fell
  through to nothing, so two notes in the corpus arrived with no technique on
  them. It is the scrape in the intro of *Twilight Of The Thunder God*,
  confirmed by ear rather than inferred from the shape of the data.

- **A pack carries the notation as well as the performance.** An arrangement
  file says a note starts at 12.4 seconds and is fret 5 on string 2, which is
  what a practice program marks against. A notation file says the same note is
  a dotted quaver in the second bar of a piece in 4/4, which is what a reader
  draws. Both are now written, and the manifest points at the second from the
  first. This is notation *data* and not engraving — nothing in it decides
  where a note head sits — and it was only cheap because the pitch spelling it
  needs had already been built for the harmony work.

  The schema came from reading the one real notation file on this machine, and
  reading it carefully turned out to matter: that file is a transcription of a
  *recording*, so its beats do not tile its bars and its rests are missing
  rather than silent. A score read from a `.gp` was written down rather than
  played, so what Fretwork writes is better formed than what it was learnt
  from, and the tests assert bars that add up rather than hoping for them.

  Two things it cannot do, both stated rather than discovered later. A bar
  containing tuplets adds up to more than it lasts, because the format has no
  field for a triplet and a triplet quaver is drawn as a quaver — 296 bars of
  5,478 across the corpus, and no time is lost because every beat carries the
  second it sounds at. And the `dot` field is written as a count of
  augmentation dots, which is the only reading that makes musical sense, while
  the single available example disagrees in a way one example cannot settle.

- **`--musicxml` writes the score for something else to open.** This program
  will not write `.gp` — reading a format is one risk and handing somebody a
  file to open in another company's program is a different one — and that
  refusal is only honest if there is some other way out. MusicXML is the only
  interchange format for music with no vendor behind it, and this is it.

  It is the first thing here to need something MIDI export never did. A note in
  MusicXML is a letter, an accidental and an octave, and no pitch number decides
  which letter: MIDI 66 is an F sharp in G major and a G flat in D flat major,
  and both are right. That was already built, for the harmony work, months
  before anything needed it for this.

  Checked against the corpus and not only against fixtures: all 26,140 notated
  pitches across the seven test files come out at the pitch the transcription
  says. The only differences anywhere are the harmonics, and they differ because
  they are correct.

- **A file from an older Guitar Pro is now named, not just refused.** "This is
  a Guitar Pro 5 file, which Fretwork cannot read yet" instead of "not a ZIP
  container", which is the difference between an answer and a shrug.

  Behind it is the first Rust in the project: `rust/gpbinary`, a crate with no
  dependencies behind a C ABI. The architecture has said since P0 that the
  hand-rolled binary formats belong in a language with bounds checks, and this
  is that decision becoming a build rather than a paragraph. It is **optional**
  — without cargo the program compiles, runs and reads everything it read
  before, because adding a second toolchain to a build is a real cost to
  whoever packages it, and CI now builds and tests a configuration with no Rust
  at all to prove that is true rather than intended.

  What it does *not* contain is anything that decodes a bar of music, and that
  is deliberate. There is not one `.gpx`, `.gp3`, `.gp4` or `.gp5` file on the
  machine this was written on. A binary format has no physics under it the way
  the harmonics did and no published specification the way MusicXML has, so the
  only way to know a reading is right is to read a file somebody else wrote —
  and a parser checked only against fixtures written by the same hand as the
  parser is a parser that agrees with itself.

Two corrections belong here, because both were claims about absence and both
were wrong in the same way.

An earlier draft of this entry said neither slides nor harmonics appear anywhere
in the test corpus. That was wrong about slides: four of the five files carry 42
between them. Slides were never waiting on evidence — they were waiting on
nobody having looked, which is a worse reason and a correctable one. The same
morning, the roadmap gained a row pricing PDF export at a few days' work, for a
program that has had `--pdf` since P2.

A claim that something is missing is a measurement like any other, and it is the
one most likely to be made by not looking.

Two things are still not said, and both on purpose. A pack's `sl` and `slu`
fields stay written as "not stated" even though slides now work: they are
integers rather than flags, no pack in the library contains a slide, and a
number invented here would be used to mark a learner. And a `semi` harmonic is
reported as a harmonic but not as a pinch, though it is played like one --
that reading is an inference, and somebody else's format is the wrong place to
write one down as a fact.

## 0.2.0 — 2026-09-01

Four directions, in the order they arrived.

**The rig.** The effects panel is laid out around the chain rather than around
the plugins, the chain can be put in the order somebody wants it in, a sound
can be kept under a name and used on another song, and the two side panels can
change places.

**Harmony.** Fretwork works out what key a piece is actually in — from the
notes, since a tablature file almost never says — draws that key on a fretboard
over the score, and writes the chords of any key into it from a circle of
fifths. Underneath all of that is a fretboard solver: given a pitch, which
string and which fret, answered as a preference with a hand attached rather
than as arithmetic.

**Hardware.** A MIDI controller can drive it. The transport, the mixer and the
eight encoders on whatever amplifier chain a part has; and notes typed in from
a keyboard, where a chord is simply the keys held down together.

**Documents.** The score is real pages now, the same ones the printer gets,
with a zoom and a page count. It writes fee[dB]ack practice packs. And the
lines it is drawn with all turn up, which through 0.1.0 they did not.

**Five of the entries below are corrections of things 0.1.0 shipped** rather
than additions to it, and each says so: a capo that was only in half of the
arithmetic, half the bar lines not being drawn, two labels drawn twice over,
notes played a little early, and a seek that let the old ones ring over the
new. Three of the five were found by writing the first tests those parts of
the program had ever had.

- **The effects panel is a board and a bench.** Every plugin's front panel
  drawn at once made the band as tall as whichever plugin was largest and as
  wide as the whole chain, and a guitarix amplifier is nine knobs, two lists
  and two switches — so a chain of three showed one card, half of the next, a
  scrollbar in each direction, and a third plugin somewhere off the side. The
  score, which is what the window is for, was down to a strip. The chain is now
  a row of tiles across the top, always drawn whole, from the instrument
  through each plugin to the pair of ports; the knobs belong to whichever tile
  is selected and are drawn once, beneath, at the width of the window. The same
  amplifier that needed a cassette stacked over two rows of knobs over three
  rows of switches puts all three side by side, because a bench as wide as the
  window has the room a card as wide as the mixer never had. What is folded
  away is only the knobs of the plugins nobody is turning: the chain itself
  never is, since what a part goes through on its way out is the question the
  panel exists to answer. The bench fits the plugin standing on it and stops at
  two rows of knobs, so a cabinet costs the score three knobs' worth and a
  plugin with thirty controls scrolls inside the bench rather than taking the
  music with it.

- **The order of a chain is most of what a chain is, and there was no way to
  say it.** A plugin could be put on the end, taken off the end, or the whole
  thing emptied — so a chain of three whose first plugin was wrong was a chain
  to clear and build again, losing the settings on the two that were right. A
  cabinet in front of an amplifier is a different sound, not the same sound
  written differently. The cable between two plugins is now the handle that
  swaps them, and the panel of the one being edited carries the cross that
  takes it off. A handle rather than a drag, as the parts list already does it:
  a chain is short, the move is one place at a time, and dragging a card whose
  every knob is draggable is two gestures competing for one press. On the cable
  rather than on the plugin because moving a stage one place is exactly
  swapping it with its neighbour, and the neighbour is what the gap between
  them is made of — one handle per seam rather than two controls per card.
- **A stage owns what belongs to it, which is why the above is safe.** The
  settings on a plugin used to live in three hashes beside the list of URIs —
  the knobs, the voicing it came from, and what that voicing could not carry —
  every one of them keyed by the stage's position in the chain. That is fine
  while the only edits are at the end, and it does not survive a reorder:
  moving stage 1 to stage 3 means permuting four containers in step, and the
  failure when one is missed is silent and precise, a voicing's name left on
  whatever plugin now stands at that number. The stage is one object now and
  moves as one thing. `tests/sessiontest.cpp` is new and exists for this: it
  drives a real session and asks whether the knob went where the plugin went.
- **A rig under a name of its own**, which the wishlist has wanted since it was
  written. The rig beside the score is what stops an evening's work being lost
  and it belongs to one transcription; a sound does not. A chain can now be
  kept under a name in the application's data folder and put on any part of any
  score. It is the same document the reader already knew how to read, and the
  conversions each way are shared with the rig beside the score rather than
  written twice. Two refusals come with it: a name is text somebody typed and
  becomes a path, so it is answered rather than trusted, and what comes back
  from saving is the name it was actually kept under; and a rig naming a plugin
  this machine has not got is refused whole, naming what is missing, rather
  than applied with the amplifier quietly left out. A chain missing its
  amplifier is not the rig on the label.
- **The parts and the mixer can change sides**, remembered between runs like
  the rest of the panel state. They are one setting because they are two ends
  of one row. This is a swap and not a rearrangement, and the difference is
  worth writing down: the bands across the bottom cannot be reordered, because
  the three things in a row are laid out in the order they are written and QML
  has no way to write them in another order at run time short of pulling each
  panel out into a component of its own. Sixteen places in the window reach
  across a panel boundary for an id, and an id does not cross a component
  boundary — so that is a refactor with a swap hidden inside it, not a swap,
  and it is not in this release.

- **A capo was only in half of the arithmetic, and 0.1.0 shipped it that way.**
  Putting a capo on moved every note in the part with it, as it should; typing
  a fret number afterwards did not. So the same fret on the same string was two
  different pitches depending on which of them had put it there, and a note
  typed onto a part with a capo at the second fret sounded a tone flat against
  everything already written. `Alt`+arrow had it too, and worse: moving a note
  to another string is the one edit that changes a fret without changing the
  music, and it was changing the music by exactly the capo. Neither was caught
  by a test, because both were tested on parts with no capo on them — and there
  is no capo anywhere in the corpus either, which is why five real
  transcriptions importing and round-tripping cleanly said nothing about it.

  Both sites now go through the fretboard solver, which is the point of having
  one. It was written for the features that come after this release — entering
  notes from a keyboard, inserting chords by name, writing out a file whose
  notes arrived as pitches, none of which can start until something can answer
  "which string" — and finding this was what it did on its first day. Two
  private half-versions of one identity is exactly how the two halves come to
  disagree.

- **A score now knows what key it is written in.** Fretwork knew that a note
  was MIDI 63. It did not know whether that was a D♯ or an E♭ — which is fine
  for playing it, and is the whole problem for writing it down, because the two
  are the same sound and different notes and nothing about the sound decides
  between them. What decides is the key, and there was no key anywhere in the
  model. A key signature is now read from every `.gp`, kept on the master bar
  the way the time signature is, saved and reopened in a `.fw`, and printed by
  `--info` where a score has one — silently where it has not, since no
  accidentals and major is Guitar Pro's default and every transcription in the
  corpus is still sitting on it, so a line reading "C major" would be Fretwork
  printing its own default back at somebody.

  With it comes the rule for writing a note down: in the key, the signature has
  already said how; outside it, the smallest accidental that reaches the note,
  and where a sharp and a flat are equally small, the one the key is already
  written in. Nothing acts on any of this yet and that is deliberate. It is the
  layer standard notation needs before it can draw a single accidental, and a
  note outside the key is a decision somebody made rather than a mistake — so
  this describes and never corrects, and nothing it knows can refuse an edit.

- **And it can work out what key a piece is actually in**, which in a tablature
  program is a different question from what the page says and usually the only
  one of the two with an answer: setting a signature is something a transcriber
  has to go out of their way to do, and not one of the five transcriptions in
  the corpus has. `fretwork FILE.gp --info` now reads the key off the notes —
  how long each of the twelve pitch classes sounds, matched against what each
  key sounds like — and says how many notes fall outside it.

  It says two readings where there are two. The accidentals fall straight out
  of the pitch content and are solid; telling a key from its relative minor is
  a judgement about where the weight sits, and the same seven notes support
  both. So a score that reads as C minor and could be read as E♭ major is
  reported as both rather than as one with false confidence.

  The count of notes outside the key is a count and never a fault. A borrowed
  chord, a chromatic passing note and a blues third are all outside the key and
  all deliberate, and a program that marked them would be arguing with its user
  about music.

- **Half the bar lines were not being drawn, and neither were five of the six
  string lines.** This was in 0.1.0, in a window that gets looked at every day.
  A page is laid out in fractions — justifying six bars across a line puts
  their bar lines at x.00, x.47, x.97, x.49, x.98 and x.50 — and a stroke
  thinner than a pixel drawn at an arbitrary fraction is not a thinner line, it
  is a line that may not appear: half its ink lands in one pixel column and
  half in the next, and against a pale paper each half is nothing. So the page
  always had *some* bar lines, in a pattern that changed with every resize, and
  it read as tablature that was slightly hard to follow rather than as a bug.
  Every horizontal and vertical stroke now sits on a whole number of pixels
  with a whole number of them to fill. The PDF is untouched: it has no pixel
  grid, and snapping to one that does not exist would move lines off the
  positions the layout worked out for them.

- **The score is a document now.** It used to be laid out to the width of the
  window on one sheet as tall as the piece, which meant the music re-broke
  every time the window was dragged — no line of it ever in the same place
  twice, no page one, and the thing on screen and the thing `--pdf` produces
  two different documents that happened to share a painter. It is now the same
  A4 pages the printer gets, broken in the same places, stacked down the window
  on a desk with a gap and a shadow between them, with the title on page 1.

  A fixed page needs a zoom, and it sits at the right of the status bar with
  the page count, where every program that shows somebody a document has put it
  for thirty years. The number is itself the button that fits the page to the
  window. Zooming holds the middle of the view still rather than the top, since
  it is done to look closer at what is already being looked at. The first run
  fits the page and stops at 150%; after that it is whatever it was last left
  at, because refitting on every resize would take the reader's own zoom away
  each time they moved the window.

- **The key, drawn on a neck over the page.** Knowing a piece is in C minor is
  not the same as knowing which frets that is, and for a guitarist the second
  is the useful half. The new **Scale** button lays a fretboard over the score
  with the notes of the key marked on it, roots filled, and the frets under the
  hand lit — the hand taken from the note under the caret. Off by default, like
  the tuner: somebody opening a tab to read it is reading the tab.

  Drawn on a neck and deliberately not on the staff. The horizontal axis of
  tablature is time, so there is nowhere on it that means "the fifth fret" for
  a scale to be marked at — the fifth fret is wherever a 5 was typed, which is
  a fact about the music rather than about the instrument. It sits over the
  page rather than on it because it is a thing about the instrument: it does
  not scroll with the music, and it is not on any page that would be printed.

- **It can write a practice pack.** `fretwork FILE.gp --feedpak pack.feedpak`
  writes a fee[dB]ack pack: a manifest, the notes of every fretted part in
  seconds with the techniques that survive the trip, and one audio stem per
  part plus the mix. The fit is close because a `.fw` is already a ZIP of
  readable JSON, this program already renders a file per track, and its
  timeline already turns notated durations into seconds — which is exactly what
  a pack's note times are. Nothing else on Linux holds both halves: a tab
  editor has the notation and cannot render stems per part, and a DAW has the
  stems and knows nothing about frets.

  Only the techniques it can honestly claim are written, and the ones it cannot
  are written as *absent* rather than left out — a practice program marks a
  learner against what the pack says is there, and a reader filling in a
  missing field with its own default would be deciding something about
  somebody's playing that nobody told it. Slides, vibrato, harmonics and
  tremolo are the gap, and they are the same ones this program already admits
  to not translating.

  The stems are uncompressed, so a pack of a five-minute piece is large.

- **A controller can drive it.** Fretwork now reads MIDI — the first time
  anything in it has, since its MIDI code until now only ever wrote files — and
  speaks Mackie Control, which is the protocol every DAW and most hardware
  already agree on. The transport buttons start and stop it, the faders and the
  mute and solo buttons work the mixer, and **the eight encoders turn the knobs
  of whatever plugin chain the current part has**: the first plugin's controls,
  then the second's, in the order the panel already draws them, so what a hand
  finds under the third encoder is what the eye finds third along. Turning a
  real knob and hearing the amplifier change with no window in the way is the
  point of it.

  Through PipeWire, and the port is chosen from a menu of what is plugged in
  and remembered between runs. A controller is usually several ports — a
  Minilab3 is four — and they are different features: one of them is its keys
  and another is its transport and encoders, so the choice is a port and not a
  device.

  **And notes can be typed in from a keyboard.** A key press writes a note at
  the caret, and the caret moves on when the hand comes off — so a chord is
  simply the keys held down together. There is no quantisation and no timing
  policy to argue with, because none is needed: what is held is a chord and
  what is let go of is finished.

  Which fret a key lands on is the fretboard solver's answer, with the hand
  where the caret's bar says it is. Play E, G and B and they walk up the top
  string; hold a C major triad after them and it comes out as a close-position
  shape at the fifth fret rather than three notes chosen one at a time. A held
  chord is one press of undo, however many keys went into it.

  It does not record, and says so if you press record. The line that keeps this
  from turning into a DAW is that MIDI arrives as an edit or as a control and
  never as a captured performance.

- **Notes were played a little early, and how early depended on the buffer.**
  Each track's synth was handed every note falling anywhere inside the block of
  audio being rendered, and then asked to render the whole block — which put
  every note in that block at the block's own start. So a note's position was
  partly a fact about the caller's buffer size rather than about the music:
  about a hundredth of a second out when exporting stems, and whatever the
  audio graph's buffer happens to be when playing. It is now rendered in pieces
  up to each note, so a note is where it was written whatever size the asks
  are. Found by writing the first tests the per-track synth has ever had, on a
  buffer big enough to show it plainly — asked for a whole second at once, a
  note written half a second in arrived at nought.

- **A seek left the old notes ringing over the new ones.** Moving the playhead
  released what was sounding rather than stopping it, so the chord from where
  you were decayed over the music you landed on. It stops them now. What
  remains is the reverb they were played into, which does not stop because
  somebody moved the playhead.

- **Two controls in the parts panel were drawing their labels twice.** The
  instrument a part is, and the recordings it is played from, were the only
  controls in that panel wearing the desktop's clothes rather than the
  program's — and the desktop style paints a button through the platform,
  its text included, from the background. Giving one a label of its own to
  elide a long programme name with therefore did not replace the text, it added
  a second copy in a second font. Both were centred, so the name had a ghost to
  the left of its first letter, a ghost to the right of its last, and a smear
  in the middle where the two copies crossed. It read as a blurred font and it
  was two of them. Both controls are now the window's own, which fixes the
  blurring and the mismatch at the same time.

- **Chords, from a circle of fifths that is a control rather than a diagram.**
  Turning it picks a key; the seven chords of that key are the row underneath;
  pressing one writes it into the score at the caret as a real beat with real
  notes on real strings. The chords borrowed from the parallel key are offered
  too, named in the key they came from — a flat sixth in C minor is the A flat
  it is and not a G sharp. The circle opens on whatever key the piece sounds
  like, since that is the one somebody writing into this piece almost certainly
  wants.

  Chords arrive where the hand already is: asked for while working at the
  seventh fret, a C major comes as the shape under that hand rather than as the
  open one down at the nut. With nothing under the caret it is the shape
  nearest the nut, which is the one a player already knows. The shapes are
  worked out rather than looked up in a table — find the lowest string that can
  sound the root under the hand, then take the lowest chord tone in reach on
  every string above it — and what comes out is what is in the chord books: C
  as x32010, G as 320003, A minor as x02210, F as the barre 133211.

  The neck drawn over the score follows the circle: turn it to G major and the
  scale on the neck is G major. They are one key kept in one place, because a
  window offering the chords of one key while showing the scale of another
  would be a window arguing with itself. The neck also gained the things a
  player navigates by — a letter on every root, and inlays at the frets an
  instrument marks — and it now takes the hand position from the frets being
  played in the caret's bar rather than from the single note under the caret,
  which is a better answer and needed no new machinery: the score already knew
  where the hand was.

  This is the first thing in Fretwork that writes notes nobody typed, so it is
  careful about it. It writes only when asked. It refuses whole and says which
  chord where an instrument cannot hold one anywhere — a chord with a note
  missing is a different chord. And one undo takes the whole chord back,
  including the beat it made to put it on.

## 0.1.0 — 2026-09-01

The first release: a tablature editor and player that gives every track its own
synthesiser, and so its own stem. Per-track LV2 chains and SFZ sampling are
present and experimental, which is meant as it is written — they are the reason
the program exists and the least settled thing in it.

Built in four phases, kept below as they were written rather than flattened
into one list, because the order the parts arrived in is most of the argument
for why they are shaped the way they are. What the release pass found on the
way out is in [docs/release-0.1.0.md](docs/release-0.1.0.md).

### P3 — the editor

- **Every `.gp` is a file somebody sent you, and two of them could stop the
  program without crashing it.** A score of nothing but nested empty elements
  — 875 bytes on disk at 100,000 deep — took twelve seconds to fail, and
  200,000 deep took a minute and a half, all of it inside `QDomDocument`
  building a tree before anybody had asked it anything. Nothing crashed, which
  is exactly what made it worth fixing: a crash is a thing a person can report,
  and a program that has simply stopped answering over a file that arrived
  looking like a song is a program that appears broken with nothing to say
  about why. Refused now by a depth scan in front of the parser — a pull
  parser, which is linear and keeps no tree, stopping at the first element past
  the limit rather than reading to the end. Real gpif nests about ten deep and
  the limit is a hundred; 200,000 now fails in a tenth of a second and says
  what was wrong with it.
- **A cap on the size of a file is not a cap on the size of what it becomes.**
  `MaximumArchiveSize` refuses a `.gp` over 256 MB on disk, and that is the
  number the sender did not choose. Deflated zeroes run to about a thousand
  times their own length, so 285 KB of perfectly well-formed archive took the
  process to a gigabyte of resident memory in seven tenths of a second, and the
  `.fw` reader shares the same entry path, so both formats were open to it.
  There is now a limit on what an entry *claims to unpack to*, checked before
  anything is allocated to hold it — the same argument as the cap above it, one
  level further down, where compression makes the two numbers different. The
  same file is refused in a tenth of a second and never becomes resident.
- **The window and the command line disagreed about what a voicing was
  called.** `--voicing 0:0=Iron Man` set the amplifier for `--render` and did
  nothing at all when the window opened, both of them reading the same nineteen
  presets off the same disk. The command line resolves a name three ways —
  exactly, then ignoring case, then as a fragment, with an ambiguous fragment
  refused and every candidate named — because "Bass - Come Together" is a lot
  to type accurately. The window compared for equality and, finding nothing,
  returned without a word: no amplifier, no message, and nothing written to the
  rig. The preset is called "Distortion - Iron Man" and the short name is the
  one everybody reaches for. Both ends call the same resolver now, the window
  has the two refusals it never had, and what goes into the rig is the name the
  bank uses rather than the fragment somebody typed — because a rig saying
  "iron" is unambiguous only until another bank is installed. Where two paths
  answer one question, the cost of the second copy is not the duplication; it
  is that only one of them was ever taught to say no.
- **`--track banana` drew the first track, wrote the file and reported
  success.** `QString::toInt()` without its flag answers 0 for anything it
  cannot read, and 0 is also what `--track` means when nobody has said
  anything, so the typo and the default were indistinguishable. `--page` had
  the same shape. Three switches in the same file — `--sfz`, `--lv2`,
  `--knob` — already take the flag and refuse by name; these two now do too,
  before any file is opened. A wrong answer given confidently is the failure
  this program can least afford, and it had one.
- All four were found by a release pass over the tree rather than by anything
  going wrong in use. The two importer limits have tests that were watched
  failing with the limits lifted before they were believed — a document nested
  past the limit, and an entry claiming to unpack to half a gigabyte. The JACK
  transport gained a suite of its own at the same time, which is not a
  regression test for any of this: it is the one piece loaded at runtime rather
  than linked, so it is the most likely thing to behave differently on somebody
  else's machine, and what it asserts is the contract its header promises
  rather than what this machine happens to have installed. The pass itself,
  including what it found and what it left open on purpose, is written down in
  [docs/release-0.1.0.md](docs/release-0.1.0.md).
- **A manual page**, which the program did not have. `man fretwork` documents
  every switch, the exit codes, the files a score keeps beside it, and the two
  things `--help` does not say: that opening with a rig and no batch switch
  gives you a window with the rig already on it, and that `--track` also
  chooses the track `--tune` listens for. Its version comes from
  `PROJECT_VERSION` at configure time, because the release number was already
  written in two places by hand and a third to remember is a third that will
  one day be wrong.
- **The effects deck was never given the height it asked for.** A guitarix
  amplifier drawn on a window a thousand pixels tall came out with its bottom
  row of switches sliced through the middle of the word, and nothing said so.
  The band is capped at over half the window precisely so that this cannot
  happen, and the cap was never the thing that bit: `Layout.preferredHeight` on
  its own is a suggestion, so when the column ran short of room the deck was
  the item that yielded — squeezed to about half of what it had asked for while
  the cap sat unreached above it. It keeps its height now, and the score above
  it yields instead, which is the right way round: the deck is shut by default
  and somebody looking at it has just opened it, whereas a score is legible at
  any height and goes on being a score when it is short. Both at 1120 pixels
  and at 700 the whole amplifier is on the band. The minimum deliberately does
  not read `root.height`, because a minimum that depends on the window's size
  is a window that will not shrink afterwards — it asks only what is on the
  band, and stops at a figure of its own so that a plugin with thirty knobs
  cannot put a floor under the whole window. Past that the deck yields again
  and a scrollbar says what has gone under the fold, which it never did before:
  it scrolled and gave no sign that it scrolled, and a card that is cut with
  nothing to say so is a card that looks wrongly drawn.
- **A row of knobs was not reading as a panel, because it was not one.** Each
  cell was as wide as its own label, so a second row of four sat under a first
  row of five at none of the same places — `BASS` under the middle of
  `MASTERGAIN`, everything after it drifting left. Knobs scattered on a card
  rather than the front of an amplifier, which is the whole reason these are
  knobs and not a list of sliders. Every cell is one width now and the rows are
  columns. The cards themselves are the height of the row as well: a cabinet is
  three knobs and an amplifier is nine, and a box drawn to its own contents put
  a short card beside a tall one and a step in a line that is meant to read as
  a line of equipment. The contents stay at the top of the stretched box, so
  the cabinet's knobs are still level with the amplifier's.
- **Every value was written to two decimals, which hid the one thing a figure
  is there to say.** `20.00` was distortion four fifths of the way up a range
  of one to a hundred, and on the knob beside it `10.00` was a cabinet halfway
  up a range of one to twenty. Both read as the same kind of number. The
  figures follow the span now — a control that runs across a hundred has
  nothing to say in its hundredths, and one that runs from 0.01 to 1 has
  nothing else to say — and the tooltip names the range, which is the thing
  neither the pointer nor the figure ever carried. Where a plugin declares a
  unit it is read from the manifest and shown: guitarix's echo says its delay
  is in milliseconds and the knob now says `100 ms`. Where a plugin declares
  nothing, and most of them do not — twenty-eight of the hundred and eighteen
  bundles on this machine declare a unit anywhere, and guitarix's amplifier
  describes nine controls and a unit for none of them — the panel invents
  none. A `dB` on a port that never claimed one would be this window asserting
  something it was not told.
- **A switch said "On" whichever way it was set.** Lit or unlit, the three
  characters were the same, and on a control called `BYPASS` that leaves a
  reader working out both what the button means and what a bypass that is on
  does to the sound. It says the state it is in rather than the state it goes
  to. Alongside it: `Tonestack Model` was being shown as "Tonestack ...", which
  told nobody anything the first word had not; the end of the chain reads
  `stem out` over the pair of sides when the ports are open rather than the
  bare word `stem`, because a part leaving on ports of its own is the thing
  this program is for and a part going into the mix with the others is not;
  and what a voicing left behind is no longer printed twice. The deck said it
  beside the tape and the status bar said it again at the foot of the window,
  both copies elided, neither readable. The deck now carries it in full and the
  bar keeps the short form while the deck is open — a voicing that reproduced
  half of itself must say so, which is the honesty the feature rests on, but it
  must not say so twice.

- **One LV2 errand at a time, across the whole process, and the eight crashes
  that bought that rule.** A worker thread per plugin instance is what the
  extension is shaped for and what other hosts do, and `process` starts the two
  sides of a mono plugin one line apart — so two of somebody else's errands
  overlapped every time a chain played its first block, and a chain of two on
  four parts measured three at once. What they were overlapping inside was
  FFTW's planner, which is documented as the one part of that library that is
  not reentrant, and which Fretwork does not link: it arrives inside a guitarix
  cabinet by way of zita-convolver, and its heap is this program's heap. Eight
  coredumps in one afternoon, some in the planner, some in a plugin's `run`
  reading a pointer the corruption had already eaten, and one jumping through a
  program counter of `0x3246`.
- **The host is the only place that serialisation could go.** A plugin's errand
  is the plugin's code, and the plugin's code is entitled to call a library
  with global state; the specification promises `work` a thread that is not the
  audio thread and never promises it is the only one. So the lock is around the
  errand rather than around FFTW — which this program cannot see to lock — and
  it is process-wide rather than per chain, because two cabinets on two parts
  are two callers of the same planner. It is paid for entirely off the audio
  thread: an errand exists because it is too slow for the callback, and the
  callback is not waiting for it. The stems rendered after the change are
  byte-for-byte the stems rendered before it.
- The crash itself is a coin toss — eighteen renders and sixteen workers after
  the fact produced none — so it was found by counting overlapping errands
  rather than by reproducing a dump, and the counting is written down in
  [docs/lv2-worker-crash.md](docs/lv2-worker-crash.md) along with the stacks,
  which existed only in a cache directory that gets cleaned.

- **Every track is a row down the left, with a drawing of what it is.** A
  guitar, a bass, a drum kit, a keyboard, or a note for anything else, and the
  one on the page is filled in. Switching between the guitar, the bass and the
  drums is the thing a person reading a tab does most often, and it had been a
  dropdown: a menu makes somebody look for it every time. A list rather than a
  row of tabs because a score has as many parts as it has — twelve of them run
  a row out of window, and Virtual Insanity has twelve. The instrument is
  decided by what the track is rather than by its General MIDI programme,
  because the programme of a drum kit is an acoustic piano.
- **The parts are on the far side of the score from the mixer**, because the
  two panels answer different questions: which part you are looking at, and
  what it sounds like. A track that cannot be heard is dimmed in both, so they
  cannot disagree about it.
- **Every bar of the piece is a box along the bottom.** Click one and the caret
  and the playhead both go there — playing or stopped, because jumping to a bar
  is usually the first half of writing something in it. The bar being played is
  lit in the same magenta the page lights it with, so the strip says where the
  music is as well as where you are, and it follows along on its own: the music
  while it plays, the caret while it does not.
- **The section names are in the strip with the numbers.** "Intro", "Verse",
  "Chorus" — a row of numbers with no words in it is a ruler rather than a map,
  and the reason anybody looks for bar 96 is that it is where the second chorus
  starts.
- **Where a bar starts and which bar is sounding are the same question asked in
  two directions**, so `Timeline` answers both — by pass through the played
  order rather than by bar number, because a bar inside a repeat starts more
  than once and "when does bar 12 begin" has as many answers as there are times
  through it. The test asserts the two directions agree.
- Nothing in the chrome takes the keyboard away from the score any more. A
  toolbar button that took focus when it was clicked would stop the next number
  reaching the page, which is the whole point of the program.

- **The window has a look of its own.** Near-black chrome, the score on paper,
  and one magenta taken from the fret marker on the app icon — a treatment
  drawn up as a design and then built, rather than arrived at by adjusting
  things until they stopped annoying anybody.
- **The colours are the application's own rather than the desktop's.** That is
  a departure from a KDE application's usual manners and it is the one a PDF
  reader and an image editor make too: the thing in the middle is a document,
  and a document that changed colour with the desktop theme would be a
  different document. The chrome is dark so that the paper is the brightest
  thing in the window.
- **The page is drawn in four weights of grey instead of one.** String lines
  sit behind the music, bar lines divide it, stems and beams are read without
  being counted, and fret numbers are the blackest thing on the paper. One
  colour for all of them is what made it look like a spreadsheet.
- **The bar being played is lit by the painting rather than by the window**, so
  the fret numbers inside it are drawn in a colour of their own instead of a
  wash being laid over the top of them. Nothing is lit while the transport is
  stopped at the beginning: there is no playhead yet, and a lit bar would have
  the page claim the program is doing something it is not.
- **The status bar says where the caret is** — "Bar 4 · string 3 · quaver" —
  next to whatever the program last had to say, and what one press of undo
  would take back. Naming a duration out loud is `NoteValue`'s job now, which
  is where the same question was already being answered for the page and the
  editor.
- **The mixer and the status bar can be put away** from the toolbar, and stay
  put away between runs.
- The track chooser is a button and a menu rather than a combo box: the desktop
  style draws that one out of the desktop's own colours and keeps a text field
  underneath its label, so it can be neither recoloured nor taken apart. The
  menu it opens is left to the desktop, because a popup is chrome the desktop
  owns and it looks wrong when it is not.
- The first line of a page had been printing its section name over the title,
  which the new spacing made obvious. The room a system's labels need is
  reserved by the layout now rather than assumed to be inside `titleHeight`.

- **It marks notes.** `x` makes a dead note, `g` a ghost note, `p` palm mutes
  and `l` lets ring — the note under the caret, or every note in the selection.
  Only these four, because they are the ones the program can both draw and
  play: a mark the page cannot show is a key that appears to do nothing, and
  one the synthesiser ignores is a lie about what will come out of the
  speakers. Accents, vibrato and the rest wait until both ends can honour them.
- **A mark goes on unless it is on already.** "Palm mute this" is what a person
  means the first time and "stop" is what they mean the second; flipping each
  note separately would turn a half-marked phrase inside out instead of
  finishing the job. Undo restores what each note had rather than clearing the
  lot, so undoing a palm mute over four bars leaves alone the bar that was
  already muted.
- **The page has a row for the marks that describe a run of notes.** Palm
  muting is not a property of a note the way a fret is; it is something a hand
  keeps doing for a while, so it is drawn the way printed tablature draws it —
  "P.M." with a dashed line saying how far it carries. A run stops at the end of
  a line and the label is printed again on the next one, because a dashed line
  cannot cross a line break. Where the runs start and stop is worked out in the
  layout, so it can be tested by reading numbers rather than by looking at a
  picture.
- A ghost note is drawn in brackets and a dead one is still a cross, which
  needs no room above the staff at all: the note is still there, and it is the
  note that changed.
- **The first line of a page no longer prints its section name over the
  title.** The room a system's labels need is now reserved by the layout rather
  than assumed to be inside `titleHeight`, so a page with no title still leaves
  space for a section name and one with a title leaves space for both. That was
  wrong before the mark row existed and would have been worse after it.
- The fingerprint the reversal tests compare now includes every mark a note
  carries. An edit that palm-muted four bars and an undo that only appeared to
  take it off would have agreed with each other perfectly, because nothing in
  the test could see the difference.

- **It transposes a phrase, not just a note.** `+` and `-` move everything in
  the selection along the strings it is already on, and the selection stays
  where it is so a riff can be walked up a fret at a time. One press of undo
  brings the whole phrase back, because moving it was one act.
- **A transposition that would strand a note is refused entirely**, and the
  status bar says which end of the neck it ran off. Eight notes moved and a
  ninth left behind is not the phrase anybody asked for, and it is worse than a
  refusal because it looks like it worked.
- **`Alt` with an arrow moves a note to the next string and keeps its pitch.**
  This is the one edit that changes a fret without changing the music: which
  string a note is played on is a fingering decision, and the fret it lands on
  is whatever makes it sound the same — fret 5 on the low E becomes the open A
  above it. Refused where the note would fall behind that string's nut, past
  the end of the neck, or on top of a note already sounding, because two notes
  on one string at one moment is not a chord.
- Pressing `+` on an empty string is not a refusal and says nothing: there is a
  difference between "that will not fit" and "there is nothing there", and only
  the first is worth interrupting somebody about.

- **It adds and removes bars.** `Ctrl+B` puts an empty one on the end of the
  score and the caret in it, `Ctrl+Shift+B` makes room at the caret, and
  `Ctrl+Shift+Delete` takes the bar under the caret out. Until now a score
  could only ever be as long as the file it was imported from, which is a
  strange thing for an editor to insist on.
- **A bar is added to every track at once.** A master bar is the score's own
  unit of time, and one made for a guitar and not for the bass beside it would
  put every bar after it out of step for the rest of the piece. There is no way
  to ask for less, because there is no version of it that is right.
- **A new bar is worth what the one it displaced was**, so a bar added to a
  piece in 6/8 is in 6/8. Its section name and its repeat signs are not copied:
  those were written on a particular bar, and a "Chorus" that suddenly starts a
  bar early is a worse mistake than one that has to be typed again.
- **Tempo changes move with the bars they were written in.** A tempo is
  positioned by bar number, so inserting a bar ahead of one and leaving its
  number alone would be moving the change rather than the music. Deleting a bar
  takes the changes written inside it — they were made at a moment that is no
  longer in the piece — with one exception: the tempo the score starts at moves
  to the front of whatever bar takes its place. Losing that would not shorten
  the score, it would silently re-time all of it.
- **The last bar of a score cannot be deleted**, and the status bar says why. A
  score with no bars is not a shorter score; it is one the rest of the program
  treats as empty and refuses to draw or play.
- Deleting a bar goes back exactly: the bars, voices, beats and notes return
  under the ids they had, with the master bar in the place it was and the whole
  tempo map as it stood. The whole-session reversal test adds and deletes bars
  on its way through, and the fingerprint it compares now includes the bar
  lines, what each bar is worth and the tempo map — an edit that moved any of
  them had nowhere to show up before.
- A paste refused for running off the end of the score now has an answer other
  than shortening what was copied: add the bars, and paste again.

- **It selects, and copies and pastes.** Shift with an arrow widens the
  selection from the beat the caret was on, dragging across the score does the
  same with the mouse, and `Ctrl+C`, `Ctrl+X` and `Ctrl+V` do what they do
  everywhere. `Delete` with a selection takes all of it.
- **A clip keeps the bars it was copied from.** Four bars of a riff pasted as
  one long bar would be the same notes and not the same music, so the first
  bar's worth lands at the caret and each further bar's worth at the start of
  the next bar of the score. Re-barring somebody's phrase for them is not a
  decision a paste gets to make.
- **A paste that would run off the end of the score is refused outright**, and
  the status bar says why. Half a paste is worse than none: the half that
  landed has to be found and undone by hand, and by then the person has stopped
  trusting the program.
- **The clipboard holds music, not ids.** The ids in a score belong to that
  score, and a clipboard full of them stops meaning anything the moment the
  beat it names is deleted. What is kept is what each beat *is* — how long it
  lasts, everything else the beat carries, and the notes on the strings by
  value — so pasting makes new beats rather than aliases of old ones. It is
  Fretwork's own clipboard and does not reach the desktop's: copying between
  two windows means choosing a MIME type and a serialisation, which is a format
  decision and belongs with the ones in `fwformat`.
- A selection lives in one voice of one track, and stepping out of either ends
  it rather than being reinterpreted. What a selection spanning two parts of a
  bar is supposed to mean is not obvious enough to guess at.
- An edit that touches one beat clears the selection first, so what is
  highlighted never disagrees with what was changed.
- Cut, paste and a deleted selection all go back exactly: the beats and their
  notes return under the ids they had, in the places they were. The
  whole-session reversal test now cuts and pastes on its way through.

- **It adds and removes beats.** `Insert` puts an empty one at the caret and
  pushes the rest of the bar along; `Ctrl+Delete` takes the one under the caret
  out, notes and all. A new beat lasts as long as the one it displaced, or as
  long as the one before it where there is nothing to displace — a bar of
  quavers wants another quaver, not a crotchet and a warning.
- **Typing a number past the end of a bar writes the beat as well as the note.**
  That is how music gets added to the end of a piece, and it is why the caret
  was allowed to sit one past the end in the first place. A bar with no voice at
  all gets one, so an empty bar can be written into — until now it was the one
  place in the score that could not be.
- **All of that is one undo.** Typing `12` where there was no beat makes a
  voice, a beat and a note, and one press of undo leaves the bar exactly as
  empty as it was. A command that makes three things and reverses one of them
  is worse than no undo at all, because people stop checking.
- Deleting a beat takes its notes out of the note table rather than leaving them
  behind, and putting it back restores them under the ids they had — a chord
  restored under different numbers is a different chord to anything still
  holding one.

- **It edits durations.** `Ctrl` and a digit sets the beat under the caret to a
  note value — 1 a semibreve, 2 a minim, and on down by halves — `.` adds a dot
  or takes it away, and `Ctrl` with an arrow doubles or halves what is there,
  keeping the dots. Undoable like everything else.
- **A bar that no longer adds up says so, and is left alone.** Its number is
  drawn in a colour that means the music itself is wrong rather than the
  program. Taking the difference out of the next note along would be rewriting
  music nobody asked it to touch, and picking which note to shorten is a guess
  the person editing has already made. An empty bar is not marked: an empty bar
  is empty, not wrong.
- **Reading a written symbol out of a duration now lives in one place.** The
  page needed it to draw a dotted crotchet and the editor needs it to know how
  many dots are there before adding one, and two implementations of "what was
  this written as" would eventually disagree — with the wrong one being
  whichever nobody was looking at. `NoteValue` is that one place, and its test
  is that the two directions are exact inverses for every value and every
  number of dots.
- Durations are deduplicated as they are added, the way gpif stores them in the
  first place: a score has hundreds of beats and about twenty distinct
  durations. An id left unreferenced by an undo stays in the table rather than
  being collected — there are only so many durations in music, and the next
  edit is likely to want it back.
- The score's own description in the tests carries durations now, so an edit
  that changes only how long a beat lasts cannot pass a reversal test by being
  invisible to it.

- **The tablature says how long a note lasts.** A row of stems under the
  strings: beams in the groups the time signature makes, flags where there is
  nothing to beam to, an open head for a minim, dots, and a mark in the staff
  where nothing sounds. Without it the page is a fingering chart — the numbers
  say where to put your hands and not one thing about when — and a bar of rests
  looked exactly like a bar somebody had forgotten to transcribe.
- **The written symbol has to be worked back out of the duration**, because the
  document does not keep it. `Score::rhythms` holds durations with their dots
  and tuplets already multiplied in, which is exactly what playback wants and
  what a page cannot draw; there is only one way three quarters of a crotchet
  can have been written down, so asking gets it back. A tuplet cannot be
  recovered that way — two thirds of a crotchet is no dotted anything — and is
  drawn as the value it is written as, which for a triplet quaver is a quaver.
- **Beams follow the beat rather than the bar.** 6/8 is two dotted crotchets,
  and beaming it in twos makes it read as 3/4: the same notes and a different
  piece of music. A rest breaks a beam instead of being drawn under one, and
  the short beam on the semiquaver of a dotted pair points at the note it
  belongs with, which is the only thing that says which of the two it is part
  of.
- **A rest is a bar in the middle of the staff, with its duration on the stem
  below it.** The proper glyph wants a music font, which is not vendored until
  standard notation arrives; a plain bar cannot say how long the silence lasts,
  and the stem already does.
- A line of music now measures the strings **and** the rhythm under them. They
  are different questions — where a string is drawn, and how much of the page a
  line takes — and confusing them puts one system's bar numbers through the
  stems of the one above.
- A time signature is given room for the digits it actually has. `12/8` was
  allowed the same gap as `4/4` and put its `2` through the first fret number.

- **It saves.** `.fw` is a ZIP holding `score.json` and a `manifest.json` saying
  which version of the format wrote it. Readable text on purpose: a bug report
  can arrive with a file attached and be understood by looking at it, and a
  format that changes can be migrated with code somebody can follow. Written by
  KArchive and read by Fretwork's own reader, which the tests already check
  against archives KArchive wrote.
- **Every score in the corpus survives import, save and reopen** describing
  exactly the same music — checked against a description deep enough that any
  field quietly dropped changes it. That test caught one immediately: the
  Guitar Pro version a score was imported from was not being written, so it is
  now carried as provenance, which is the first thing to ask about when an
  imported score turns out wrong.
- Saving the same score twice produces the same bytes, so a version control
  system has something useful to show.
- **Fretwork will not write `.gp`.** Reading a format nobody documented is one
  risk; handing people files to open in somebody else's program is a different
  and worse one. An imported score keeps its original file untouched, and the
  first save asks where to put a file of Fretwork's own.
- Unknown keys are ignored rather than refused, so a file from a later version
  opens in an earlier one with whatever it understands.
- The command line opens `.fw` files too — a score saved from the window must
  not be a file only half the program understands.

- **It edits.** A caret that sits on a string within a beat rather than on a
  note — because there may be nothing there yet, and typing a number is how
  something arrives. Click to place it, arrows to move it, digits to type a
  fret, `Delete` to clear one, `+` and `-` to move a note along its string.
- **Every change is undoable, without exception.** Not for the menu item, for
  the discipline: a change that cannot describe how to reverse itself is a
  change that was not thought through, and an undo that works for most things
  is worse than none, because people stop checking.
- **Typing `1` then `2` is fret 12 and one undo.** A digit typed shortly after
  another extends it; a run ends when the caret moves or after a pause. `37`
  is treated as a slip and starts a new number rather than being clamped to 36.
- Undoing a note typed onto an empty string leaves the string **empty**, not a
  fret 0 — a different note and a different-looking bar, and the easiest thing
  in this component to get wrong. There is a test that does a whole editing
  session and checks the score comes back identical.
- An edit relays out the page immediately and rebuilds the player the next time
  playback is asked for: laying out is microseconds, and rebuilding means
  loading a SoundFont for every track, which is not a thing to do between two
  keystrokes.
- **It cannot save yet**, which is the next thing.

### P2 — the player

- **An application icon**, drawn for the project: an F built from a nut and
  strings with a fret marker on it. Six sizes and a scalable copy installed into
  the icon theme, a symbolic version for places that want one, and the scalable
  one compiled into the binary as well — an installed copy takes its icon from
  the theme, and a copy run out of the build directory has no theme to take one
  from.
- A repeat count no longer reads as part of the next bar's number. `×4` sat
  level with the bar numbers and immediately left of one, so a repeated section
  ending before bar 3 announced itself as "×4 3". It has a row of its own now,
  in the colour used for things done to the music rather than the music itself.
- **There is a window.** `fretwork FILE.gp` opens it: tablature on the left, a
  mixer on the right, a transport across the top, and the bar being played lit
  up behind the music. Asked to produce something — `--info`, `--play`,
  `--render`, `--pdf` — it is still a command line tool; asked for nothing in
  particular, it is an application.
- **The mixer works while it plays**, because every track already had a synth
  of its own. Soloing is one atomic store away from being heard.
- **The view follows the playhead until you scroll**, and then stops chasing
  you. It scrolls only when the bar being played has left the screen, rather
  than every bar.
- The score is painted by the same function that writes the PDF, so the window
  and a printout cannot disagree; it paints only the systems on screen, which
  is what makes a four-hundred-bar score scroll.
- Nothing in the model, the timeline or the audio includes a Qt Quick header.
  The window is a facade over things that were already tested without one.

- **Live playback, with a mixer.** `--play` runs the same engine the renderer
  drives, from an audio callback instead of a loop — which is the entire reason
  `fill(frames, at)` was written that way. `--solo` and `--mute` take effect
  while it plays, because each track already has a synth of its own: no
  re-render, no bounce.
- **What runs in the audio callback and what does not.** It fills buffers and
  reads atomics. No allocation, no locks, no logging, no signals. Pressing play,
  moving a fader and soloing a track are each one atomic store from the user's
  thread and one load from the audio thread, and the position is polled on a
  timer rather than pushed — a thread that emits signals is a thread that
  allocates, and a thread that allocates drops out.
- **FluidSynth's PipeWire driver needs the host to have called `pw_init()`**,
  which it does not do for you and which fails with a message about
  `SPA_PLUGIN_DIR` that leads nowhere. Fretwork calls it, and falls back through
  PulseAudio, ALSA and JACK, reporting which one actually opened.
- A muted track is still filled, and only then dropped from the mix, so
  unmuting does not empty every event it slept through into one block.
- Seeking silences what is ringing, repositions each track's cursor by binary
  search, and re-sends the bend range — which is set once at the start and would
  otherwise be skipped past, leaving every later bend a sixth of its size.
- Fretwork no longer introduces itself to the desktop portal as
  `org.kde.fretwork`, which is `KAboutData`'s default and an application that
  does not exist. The same lesson Signpost learned.

- **Tablature, drawn.** `--pdf` and `--png` lay a track out and draw it: title,
  tuning, section names, bar numbers, time signatures where they change, repeat
  signs, dead notes as crosses, and fret numbers coloured where a bend, slide or
  hammer-on is marked. The string line is cleared behind each number rather than
  drawn through it, which is how tablature has always been set.
- **Layout is a separate step from painting.** All the judgement is in the
  layout — how wide a bar should be, where a line should break, how to spread
  the slack — so it can be tested by reading numbers rather than by looking at
  pictures, and so a window, an image and a PDF cannot disagree about it.
- **Bar width follows the square root of duration**, not duration. A whole note
  lasts four times a quarter and is nothing like four times as wide on paper:
  proportional spacing makes a bar of semiquavers unreadable and a bar of
  semibreves mostly blank. Lines are justified to the page except the last one,
  which is left as it falls.
- The score is drawn **as notated**: a repeat is the sign it is, not the eight
  bars it stands for. Expanding it is playback's business.

### P1 — the headless converter

- **Audio, one synthesiser per track.** `--render out/` writes a WAV per track
  and a mix, in a single pass at around twenty times real time. Each track owns
  a FluidSynth of its own, which is the design's whole point: sixteen channels
  each, so a guitar spends six on its strings and the limit that constrains the
  MIDI writer does not exist here. The mix is the arithmetic sum of the stems,
  which the tests check sample by sample — if it were not, the stems would not
  be what anyone is hearing.
- **The engine knows only how to fill a block of frames at a sample position.**
  No clock, no transport, no idea whether a loop or an audio device is driving
  it. Offline rendering runs that loop as fast as the machine allows; live
  playback in P2 will run the identical loop from a callback, rather than a
  second implementation with its own bugs.
- **A WAV writer**, streamed, with its lengths patched on close — five minutes
  of stereo is fifty megabytes and a stem export writes several at once.
  Clipping rather than wrapping, so a loud mix sounds loud and wrong instead of
  quiet and inside out.
- **One list of messages drives both MIDI and audio.** The bend curve used to
  live inside the MIDI writer; the renderer needed the same thing, and two
  implementations of "when does the bend move" would eventually disagree, with
  the wrong one being whichever nobody listened to. Moving it into the timeline
  left the rendered audio bit-for-bit identical, which is how the refactor was
  checked.
- **Techniques, which the spike never attempted.** Bends become curves in
  cents along the note, drawn as pitch bend on that string's own channel and
  returned to centre afterwards so the next note does not inherit them. Let
  ring holds a note until that string is used again, which is what the
  instruction means on a guitar and why it can only be resolved once the whole
  track is laid out. Hammer-ons, pull-offs and legato slides are quieter rather
  than picked — an approximation until P4 has a sampler that can play them
  properly, and a smaller mistake than sounding them at full strength.
- **MIDI out**, whole or per track: `-m out.mid` and `--stems out/`. The file
  format has sixteen channels and a score with four guitars wants twenty-four,
  so channels are handed out greedily — one guaranteed per track, the rest
  spent widening the biggest instruments to a channel per string — and whatever
  had to be given up is printed rather than quietly done. A stem written on its
  own has all the room it needs; only the mix compromises.
- **The converter exists in C++.** `fretwork FILE.gp` reads Guitar Pro 7 and 8,
  reconstructs the score from the deduplicated tables gpif stores it in,
  flattens repeats into the order the music is heard, and reports what it
  understood. Note counts agree exactly with the P0 spike on every file in the
  corpus, which is the only reason to believe either of them.
- **Fretwork reads its own ZIPs, which was not the plan.** Guitar Pro 8.1.4
  writes archives in streaming mode — bit 3 of the general purpose flags, sizes
  recorded after the data rather than in the local header — and KArchive's KZip
  rejects those files outright. Both 8.1.4 files in the corpus failed and all
  nine 8.1.3 files passed. Reading the central directory, which is where a ZIP's
  real index lives, opens all eleven. The regression test builds such an archive
  byte by byte, so it holds without a corpus.
- **Durations are exact rationals rather than doubles.** Not for drift, which is
  far too small to hear, but because positions are compared for equality: a tie
  joins a note to the one already ringing by asking whether one ends exactly
  where the other starts. In doubles those sums agree nearly always, and the
  once in a while they do not, a held note restrikes mid-bar.
- **A channel per string, from the first version of the timeline.** Pitch bend
  is per channel, so six strings sharing one cannot bend a note while the rest
  of the chord rings. The P0 spike got away with one channel per track only
  because it translated no bends; this does not, and the test asserts that two
  strings never share.
- Rust is deferred to GP3–GP5 and GPX, where the argument for it — untrusted
  hand-rolled binary — actually applies. GP7/8 is a ZIP and an XML document.

### P0 — the spike

- **The spike works end to end.** `spike/gp2midi.py` reads a Guitar Pro 7/8
  file, reconstructs the score from the flat id-referenced arrays gpif stores
  it in, expands repeats into a played timeline, and writes a standard MIDI
  file. `--stems` renders one WAV per track through FluidSynth, plus a mix,
  which is the project's whole premise demonstrated in 350 lines of throwaway
  Python. Confirmed the only way it can be: by listening to it.
- Verified against eleven Guitar Pro 8.1.3 and 8.1.4 scores: 1,451 bars, 9,157
  beats, 3,002 notes. All parse. *Horses* renders to 4:52 against a real track
  length of 4:53, which is the tempo map, the time signatures and the note
  durations all agreeing at once.
- **`docs/architecture.md`** — the stack, defined and argued: C++20 with Qt6 and
  KF6, Rust behind a C ABI for the importers, FluidSynth per track, lilv for
  per-track LV2 chains, PipeWire out. Includes the two engine rules that are
  cheap now and expensive later: one MIDI channel per string, and an engine
  that knows only how to fill N frames at sample position T.
- **`docs/gpif-format.md`** — how a `.gp` file is really put together, measured
  rather than assumed, including the three traps: beats are deduplicated across
  the whole score, the track transpose is for display and must not reach
  playback, and a drum kit is identified by instrument type rather than by its
  programme number, which is 0 and means acoustic piano.
