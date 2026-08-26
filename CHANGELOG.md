<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Changelog

## Unreleased — P3

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

## Unreleased — P2

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

## Unreleased — P1

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

## Unreleased — P0

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
