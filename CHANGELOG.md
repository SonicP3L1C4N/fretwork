<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Changelog

## Unreleased — P1

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
