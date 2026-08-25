<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Changelog

## Unreleased — P2

- **An application icon**, drawn for the project: an F built from a nut and
  strings with a fret marker on it. Six sizes and a scalable copy installed into
  the icon theme, a symbolic version for places that want one, and the scalable
  one compiled into the binary as well — an installed copy takes its icon from
  the theme, and a copy run out of the build directory has no theme to take one
  from.
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
