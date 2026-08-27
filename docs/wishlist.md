<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Wishlist

Things Fretwork could do, written down so they are arguable rather than
forgotten. Nothing here is promised. The phase table in
[architecture.md](architecture.md) is what will be built; this is what has been
thought of, and most of it never will be.

Every item says what it would cost as well as what it would give, because a
wish with no price on it is a daydream. Items that are refused on principle are
at the bottom under [Not wished for](#not-wished-for), so the refusals are as
visible as the wishes. When something here is actually committed to, it moves
into the phase table and leaves this file.

## Playing it as written

The technique translation stops where honesty does: what it cannot play, it
says it cannot play. Each of these closes one of those admissions.

- **Slides.** A slide is pitch, not articulation — a bend across the tail of a
  beat rather than a second note. Cheap in audio, where every string has a MIDI
  channel to itself; the work is in drawing it, and in deciding where the slide
  ends when the notation does not say.

- **Tremolo picking.** One notated note becomes some number of notes at a
  subdivision the score never states. The rule for how fast is fast enough
  changes with tempo, which is why it is not a two-line change.

- **Harmonics.** Natural harmonics are a fret-to-pitch table and a thinner
  timbre. Artificial and pinch harmonics are neither, and a General MIDI
  soundfont holds nothing that sounds like one — worth waiting for the sampling
  in P4 rather than approximating now.

- **Grace notes and trills.** Both are notes that do not appear in the bar's
  arithmetic, which is the hard part rather than the sound: the model measures
  what a bar is worth and would call the bar wrong.

- **Vibrato and whammy bar.** A periodic bend, and therefore nearly free once
  slides exist, because it is the same per-string channel machinery.

- **Alternate endings and jumps.** The repeat expander flattens plain repeats
  and refuses everything else, which is why a score using them says so. D.S.,
  D.C., codas and numbered endings are a graph traversal, and that graph is
  where transcriptions are most often wrong — a flattener that guessed would
  play music nobody wrote.

- **Fermatas and free time.** Rare enough to have gone unnoticed, wrong enough
  to be worth an hour when it is.

## Sounding like an instrument

P4 in the plan is three items. These are the rest of that thought.

- **Impulse-response cabinets.** A cab is a convolution and a file. It is the
  cheapest realism available per line of code written, and it composes with
  whatever amplifier simulation sits in front of it.

- **Pick, fret and string noise.** The difference between a sampled guitar and
  a guitar. Needs the SFZ layer first, because there is nothing in a GM bank to
  trigger.

- **Per-string panning and per-string round-robins.** One synth per track
  already means sixteen channels per instrument; spending them on stereo width
  and alternating samples costs nothing extra at render time.

- **A dry stem beside the wet one.** Write the unprocessed track alongside the
  processed one, so a mix can be redone in a DAW without re-rendering from the
  score. Follows directly from one synth per track, and is close to free.

- **Live per-track PipeWire ports.** Expose each track as its own graph node so
  Ardour or Reaper can record Fretwork's stems as they play, rather than
  importing WAVs afterwards. This is the item that would make the project's
  premise true *outside* its own window, and is probably the single highest
  value thing on this page.

- **Transport sync.** JACK transport or MIDI clock, so Fretwork and the DAW
  agree about where bar 40 is. Pointless without the ports above; obvious with
  them.

- **Importing the RSE mixer state.** Open question in the architecture rather
  than a decided wish: the effects a `.gp` names are Arobas's and cannot be
  reproduced, so the most that can be honestly taken is gain, pan and which
  track is which.

## Practising with it

None of this is why the project exists, and all of it is why a person would
open it on a Tuesday evening.

- **Loop a selection.** The selection model is already there from P3; the
  transport is the part that does not know about it yet.

- **A speed trainer.** Play the loop at 70% and climb a few percent each pass.
  Tempo scaling is one multiplier in the scheduler, so the feature is mostly
  interface.

- **A count-in.** The metronome exists; being counted in to the start of a
  loop does not, and it is the half of it somebody practising actually wants.
  The awkward part is not the clicks -- it is that the transport has to be
  somewhere before the beginning while they play, and every position the
  window reports is measured from the beginning.

- **Backing track minus one.** `--render --mute 0` almost does this already;
  what is missing is saying so, and a name for it.

## Playing into it

Everything above assumes notes arrive by being typed. These are the other ways
in, and one piece of plumbing — a live audio input and a MIDI input, both
through PipeWire — serves all of them. Build that once and every item here gets
cheaper.

- **A MIDI keyboard.** Pitches arrive and Fretwork has to choose a string and a
  fret, which is the problem the transposer already solves: prefer the position
  the hand is in, refuse what runs off the neck. The most useful way in for
  everything that is not a guitar — bass lines, keys, and drums played on pads,
  where a pad maps to a kit piece and no inference is needed.

- **A MIDI guitar.** A hex pickup or a guitar-to-MIDI box sends string and fret
  directly, so nothing has to be inferred at all. Rare hardware, almost no
  code, and the only input that maps exactly onto the model.

- **The guitar itself, through the interface.** Audio in, pitch and onset out,
  notes on the staff. Monophonic detection is a solved problem and good enough
  for single-note lines; polyphonic detection is not, and shipping it as though
  it were would produce transcriptions nobody asked for. Whichever one it does,
  it should say which.

- **Step entry before real-time entry.** Play a note, it lands under the caret,
  the caret advances. That works with all three inputs above and needs no
  timing analysis whatsoever, which makes it the version worth building first.
  Real-time entry against the click needs quantisation, and quantisation needs
  a stated policy about a note that lands between two subdivisions — the same
  argument as incomplete bars, and it should get the same answer.

- **Choosing the device, and remembering it.** Which PipeWire source, which
  MIDI port, what latency, and does it survive the interface being unplugged.
  The unglamorous half of everything above, and the half that decides whether
  any of it gets used twice.

## Editing further

P3 is fret entry, rhythm, bars, selection, clipboard and undo. These are the
edits it cannot yet make.

- **Editable section names.** They are drawn on the bar strip and on the page,
  and read only.

- **Bar repair, on request.** Incomplete bars are marked rather than corrected,
  which is right as a default. An explicit "make this add up" that the user
  asks for is not the same as rewriting music behind their back.

- **Multi-track selection and paste.** A clip currently keeps its bars; keeping
  its tracks as well is the same argument one level up.

- **Position shifting.** Move a whole phrase up the neck — same pitches,
  different strings. `Alt`+arrow does this for one note already; the phrase
  version needs a rule for what to do when only part of it fits.

- **Find a phrase.** Search by fret pattern or by interval, which is how you
  find the other three places a riff appears.

## Reading and writing more

- **GP3, GP4, GP5.** The old binary formats, and the ones most tab on the
  internet is still written in. Bounded work, well-charted by other projects,
  and entirely in the Rust importer where untrusted input belongs.

- **GP6 `.gpx`.** The compressed BCFS container. Listed in P5 already.

- **MusicXML, both ways.** The only interchange format with no vendor behind
  it, and therefore the honest answer to "can I have this file in something
  else" — a question Fretwork refuses to answer by writing `.gp`.

- **MIDI import.** Cheap, and immediately useful for getting a drum part in
  from a drum machine.

- **ASCII tab out.** The format forums still use, and about a day's work.

## On the page

- **Standard notation.** P5, and stated in the architecture as possibly never.
  MuseScore's layout engine is many people over many years; tablature is a far
  smaller problem that answers most of the question.

- **Multi-track pages.** The tracks stacked down the page as a score, rather
  than a page per track.

- **Print setup.** Page size, margins, which tracks, which bars.

- **Chord diagrams and lyrics.** Both are read past on import, so this is two
  wishes: keep them in the model, then draw them.

- **Fingering marks.** Left hand under the staff, and the reason a beginner
  can use a transcription at all.

- **One page of the riff.** Export the selection as a PNG sized for a phone,
  because that is what actually gets sent to the other guitarist.

## Getting it to people

- **AppStream metadata.** There is a `.desktop` file already; the metainfo XML
  and a screenshot are what a software centre needs to show it at all.

- **A soundfont story.** `fluid-soundfont-gm` is a build dependency the user
  installs by hand. A first run that finds no soundfont should say so in words
  and point somewhere, not fail quietly.

- **Translations.** KF6 i18n is already linked, so the strings are already
  extractable; what is missing is the plumbing and somewhere to send them.

- **A flatpak — carefully.** LV2 plugins live outside the sandbox, and an
  application whose point is per-track LV2 chains is the worst possible
  candidate for one. Worth testing before wanting.

- **KDE Invent, and eventually kdereview.** The project already follows KDE's
  conventions, licence and frameworks, so this is a question of when, not of
  rewriting anything.

## Not wished for

Stated so that nobody has to ask twice.

- **Writing `.gp`.** Reading an undocumented format is one risk. Handing people
  files to open in someone else's program is a different and worse one.

- **Becoming a DAW.** There is a mixer because stems need one, not because
  Ardour looks beatable.

- **A tab library, or downloading transcriptions.** A program that fetches
  other people's transcriptions is in a different legal position from one that
  reads a file you already have, and the second position is the whole reason
  this project can exist.

- **A plugin or scripting API.** Not never, but not before the model it would
  expose has stopped changing every fortnight.
