<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# The `.gp` container and the GPIF document

Notes on Guitar Pro 7/8 files, measured against a corpus of 11 scores written by
Guitar Pro 8.1.3 and 8.1.4 — 1,451 bars, 9,157 beats and 3,002 notes. Nothing
here is from a vendor specification, because there is none. Where a claim is a
guess, it says so.

## The container

A `.gp` file is an ordinary ZIP archive:

```
Content/score.gpif          the document — XML, everything below
Content/BinaryStylesheet    engraving preferences, opaque
Content/LayoutConfiguration
Content/PartConfiguration
Content/Preferences.json
Content/ScoreViews/*.gpsv
Content/Stylesheets/*.gpss
VERSION
meta.json
```

Only `score.gpif` matters for playback. The rest is presentation, and most of it
is undocumented binary that Fretwork carries through unchanged rather than
interprets.

This is the friendly member of the family. GP6's `.gpx` wraps the same XML in a
custom bit-level compression (BCFS/BCFZ) that must be decoded first, and GP3–GP5
are a different format entirely.

## The document is flat and reference-linked

`score.gpif` does not nest. It holds parallel arrays, and the structure is
expressed by integer ids pointing between them:

```
GPIF
├── Score            title, artist, album, words, music
├── MasterTrack      track ids, and Automations (tempo)
├── Tracks           Track*    — name, instrument, tuning, MIDI programme
├── MasterBars       MasterBar* — time signature, key, repeats, sections
├── Bars             Bar*      — clef, and four Voice ids
├── Voices           Voice*    — a list of Beat ids
├── Beats            Beat*     — a Rhythm ref, a list of Note ids, dynamics
├── Notes            Note*     — string, fret, MIDI number, techniques
└── Rhythms          Rhythm*   — note value, dots, tuplet
```

Everything below `MasterBar` is **deduplicated across the whole score**: one
`Beat` element is referenced by every voice that plays it. In the corpus a
176-bar score with four tracks has 704 bars but only 235 distinct beats and 154
distinct notes. An importer that treats an id as a unique occurrence will read a
quarter of the music.

Reading order that works:

1. `MasterBars[i]` gives bar *i* for the whole score, and its `<Bars>` text is
   one `Bar` id **per track, in track order** — `0 176 352 528`.
2. `Bar` gives `<Voices>` as four ids, `-1` where the voice is empty.
3. `Voice` gives `<Beats>`, in time order.
4. `Beat` gives `<Rhythm ref="…"/>` for duration and `<Notes>` for pitches.
   A beat with no `<Notes>` is a rest.
5. `Note` gives the pitch three ways; see below.

## Pitch: use the MIDI number

A `Note` carries `ConcertPitch`, `TransposedPitch`, `Fret`, `String` and
`Midi`. `Midi` is the sounding note and is what playback should use.

Beware the other two. GP's octave numbering runs one higher than the MIDI
convention — a `Midi` of 55 is labelled `G4` where MIDI calls it G3 — and
`TransposedPitch` is the notated pitch, which for guitar is written an octave
above where it sounds. The `Track` carries `<Transpose><Octave>-1</Octave>` to
say so. **That transpose is for display. Applying it to playback drops the whole
guitar an octave.**

Strings are indexed from the *lowest* pitch: `String` 0 is the bottom string.
The tuning lives on the staff as MIDI pitches, low to high:

```xml
<Property name="Tuning">
  <Pitches>36 41 46 51 55 60</Pitches>
</Property>
```

`36 41 46 51 55 60` is drop C. Standard tuning is `40 45 50 55 59 64`. The
identity `Tuning[String] + Fret == Midi` holds throughout the corpus, and is
worth asserting in the importer as a cheap check that the file was understood.

## Rhythm

```xml
<Rhythm id="2">
  <NoteValue>Quarter</NoteValue>
  <AugmentationDot count="1"/>
  <PrimaryTuplet num="3" den="2"/>
</Rhythm>
```

`NoteValue` seen in the corpus: `Whole`, `Half`, `Quarter`, `Eighth`, `16th`,
`32nd`, `64th`, `128th`. (`256th` exists in the format; nothing here uses it.)
`AugmentationDot` counts 1 or 2. `PrimaryTuplet` seen as 3:2 and 6:4.

Duration is `whole × 2^-value × dot factor × den/num`.

## Time, tempo, repeats

`MasterBar` carries `<Time>4/4</Time>` as text, a `<Key>`, an optional
`<Section>` with a name, an optional `<TripletFeel>`, and repeat structure:

```xml
<Repeat start="true"  end="false" count="0"/>
<Repeat start="false" end="true"  count="4"/>
<AlternateEndings>1</AlternateEndings>
```

Tempo is **not** on the bar. It lives on `MasterTrack/Automations` as entries
carrying `Bar`, `Position`, and a `Value` of two numbers — `150 2` meaning 150
beats per minute where the beat is a quarter note (the second number is the note
value: 2 = quarter). A score with no automation has no tempo, and 120 is the
conventional fallback.

Corpus frequencies, which are a fair guide to what an importer meets first:
4/4 × 1091, 3/4 × 172, 6/8 × 86, 5/4 × 64, 12/8 × 20, 2/4 × 18. Repeats appear
36 times, alternate endings 8.

## Techniques

Every technique that appears in the corpus, by frequency. This is the list P1's
translation layer owes an answer to; the count is how much a score notices if
the answer is wrong.

**On the note, as elements:** `Tie` 920, `Vibrato` 155, `Accent` 152, `LetRing`
84, `AntiAccent` 52.

`Tie` carries `origin` and `destination` booleans — a note may be both, in the
middle of a chain. A destination tie must extend the sounding note rather than
restrike it, which is the single most audible correctness bug available in this
component.

**On the note, as properties:** `Slide` 194, `HopoOrigin` 185, `HopoDestination`
161, `Bended` 100 (with `BendOrigin/Middle1/Middle2/DestinationValue` and
matching `Offset`s describing the curve), `Muted` 69, `PalmMuted` 45, `Tapped`
2, `Harmonic`/`HarmonicFret`/`HarmonicType` 1.

Bend values are in hundredths of a semitone: 100 is a semitone, 200 a whole
tone. Offsets are positions along the note, 0–100.

**On the beat:** `Dynamic` (always present — `F` 5341, `MF` 2186, `MP` 996,
`FFF` 293, `FF` 160, `P` 165, `PP` 13, `PPP` 3), `Chord`, `Lyrics`, `FreeText`,
`Hairpin`, `Tremolo`, `Whammy`, `GraceNotes`, and as properties `Slapped` 43,
`Popped` 20, `Brush` 9, `VibratoWTremBar` 6.

`PrimaryPickupVolume` and `PrimaryPickupTone` appear on every single beat and
mean nothing outside Guitar Pro's own effects engine. Carry them; ignore them.

## Instruments

`Track/Sounds/Sound/MIDI/Program` is a General MIDI programme number, and
`InstrumentSet/Type` is the family. Every score in the corpus has a drum track,
whose `Type` is `drumKit` with `Program` 0 — **drums are identified by type, not
by programme, and must be routed to MIDI channel 10.** A `drumKit` track played
on any other channel is an acoustic piano playing nonsense.

Types seen: `electricGuitar`, `electricBass`, `drumKit`, `acousticPiano`,
`electricPiano`, `electricOrgan`, `padSynthesizer`, `leadSynthesizer`,
`saxophone`, `clarinet`, `violin`, `viola`, `ukulele`.

## What Guitar Pro already agrees with

Two details in the format are worth pointing at, because they confirm decisions
[architecture.md](architecture.md) makes independently:

- `Track/UseOneChannelPerString` and `MidiConnection/ForeOneChannelPerString`.
  Guitar Pro allocates a MIDI channel per string for the same reason Fretwork
  will: pitch bend is per-channel, and a shared channel bends notes that are not
  being bent.
- Beats are deduplicated by content. A document model built around shared,
  immutable beats is not a clever optimisation; it is what the format already
  assumes.
