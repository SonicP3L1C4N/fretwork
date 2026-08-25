#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

"""
P0 spike: a Guitar Pro 7/8 file in, MIDI and per-track stems out.

Throwaway on purpose. This exists to answer four questions before a line of C++
is written, and to be deleted once P1 answers them properly:

  1. Does score.gpif carry everything playback needs?          (yes)
  2. Does the flat, id-referenced model reconstruct in order?  (yes)
  3. Does repeat expansion belong in a separate pass?          (yes)
  4. Is one synth instance per track enough for stems?         (this)

Standard library only, so it runs on any machine with python3 and, for audio,
fluidsynth on the path.

Known and deliberate limitations, all of which are P1's job:
  - Alternate endings are ignored; simple start/end repeats are expanded.
  - Bends, slides, hammer-ons, harmonics and tremolo are not translated.
    Notes sound; techniques do not. That is the whole point of P1.
  - One MIDI channel per track, not per string, so a bend would be wrong --
    which is exactly why this spike does not attempt one.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import zipfile
import xml.etree.ElementTree as ET
from fractions import Fraction

TICKS_PER_QUARTER = 960
DRUM_CHANNEL = 9
DEFAULT_SOUNDFONT = "/usr/share/sounds/sf2/FluidR3_GM.sf2"

# The value of a note, in quarters. GP names them; the format goes to 256th,
# though nothing in the corpus does.
NOTE_VALUES = {
    "Whole": Fraction(4), "Half": Fraction(2), "Quarter": Fraction(1),
    "Eighth": Fraction(1, 2), "16th": Fraction(1, 4), "32nd": Fraction(1, 8),
    "64th": Fraction(1, 16), "128th": Fraction(1, 32), "256th": Fraction(1, 64),
}

# Dynamics are on every beat, and are the only velocity information there is.
DYNAMICS = {"PPP": 15, "PP": 31, "P": 47, "MP": 63,
            "MF": 79, "F": 95, "FF": 111, "FFF": 127}

# The second number of a tempo automation names the beat the BPM counts.
TEMPO_BEAT = {1: Fraction(1, 2), 2: Fraction(1), 3: Fraction(3, 2),
              4: Fraction(2), 5: Fraction(3)}


def text(element, path, default=""):
    """An element's text, stripped -- gpif indents its values onto own lines."""
    if element is None:
        return default
    found = element.find(path)
    if found is None or found.text is None:
        return default
    return found.text.strip()


def ints(value):
    return [int(part) for part in value.split()] if value.strip() else []


# ---------------------------------------------------------------- reading ----

class Score:
    """score.gpif, read into the arrays it already is."""

    def __init__(self, path):
        with zipfile.ZipFile(path) as archive:
            root = ET.fromstring(archive.read("Content/score.gpif"))

        self.path = path
        self.version = text(root, "GPVersion")
        self.title = text(root.find("Score"), "Title")
        self.artist = text(root.find("Score"), "Artist")

        self.tracks = [self._track(t) for t in root.find("Tracks")]
        self.masterbars = [self._masterbar(m) for m in root.find("MasterBars")]
        self.bars = {b.get("id"): ints(text(b, "Voices")) for b in root.find("Bars")}
        self.voices = {v.get("id"): ints(text(v, "Beats")) for v in root.find("Voices")}
        self.beats = {b.get("id"): self._beat(b) for b in root.find("Beats")}
        self.notes = {n.get("id"): self._note(n) for n in root.find("Notes")}
        self.rhythms = {r.get("id"): self._rhythm(r) for r in root.find("Rhythms")}
        self.tempos = self._tempos(root.find("MasterTrack"))

    @staticmethod
    def _property(element, name):
        """gpif hangs most of its data off <Property name="..."> children."""
        holder = element.find("Properties")
        if holder is None:
            return None
        for prop in holder:
            if prop.get("name") == name:
                return prop
        return None

    def _track(self, element):
        sound = element.find("Sounds/Sound/MIDI")
        staff = element.find("Staves/Staff")
        tuning = self._property(staff, "Tuning") if staff is not None else None
        return {
            "name": text(element, "Name") or "Track",
            # The instrument family, not the programme, is what identifies a
            # drum kit: its programme is 0, which is an acoustic piano.
            "type": text(element, "InstrumentSet/Type"),
            "program": int(text(sound, "Program", "0") or 0) if sound is not None else 0,
            "tuning": ints(text(tuning, "Pitches")) if tuning is not None else [],
            "capo": int(text(self._property(staff, "CapoFret"), "Fret", "0") or 0)
                    if staff is not None and self._property(staff, "CapoFret") is not None else 0,
        }

    @staticmethod
    def _masterbar(element):
        repeat = element.find("Repeat")
        signature = text(element, "Time", "4/4").split("/")
        return {
            "bars": ints(text(element, "Bars")),
            "numerator": int(signature[0]),
            "denominator": int(signature[1]),
            "section": text(element, "Section/Text"),
            "repeat_start": repeat is not None and repeat.get("start") == "true",
            "repeat_end": repeat is not None and repeat.get("end") == "true",
            "repeat_count": int(repeat.get("count", "0")) if repeat is not None else 0,
            "alternate": element.find("AlternateEndings") is not None,
        }

    def _beat(self, element):
        rhythm = element.find("Rhythm")
        return {
            "rhythm": rhythm.get("ref") if rhythm is not None else None,
            "notes": ints(text(element, "Notes")),
            "dynamic": text(element, "Dynamic", "MF"),
        }

    def _note(self, element):
        midi = self._property(element, "Midi")
        string = self._property(element, "String")
        fret = self._property(element, "Fret")
        tie = element.find("Tie")
        return {
            "midi": int(text(midi, "Number", "-1")) if midi is not None else -1,
            "string": int(text(string, "String", "-1")) if string is not None else -1,
            "fret": int(text(fret, "Fret", "0")) if fret is not None else 0,
            "tie_destination": tie is not None and tie.get("destination") == "true",
            "muted": self._property(element, "Muted") is not None,
            "palm_muted": self._property(element, "PalmMuted") is not None,
            "accent": element.find("Accent") is not None,
            "ghost": element.find("AntiAccent") is not None,
        }

    @staticmethod
    def _rhythm(element):
        value = NOTE_VALUES.get(text(element, "NoteValue", "Quarter"), Fraction(1))
        dot = element.find("AugmentationDot")
        if dot is not None:
            count = int(dot.get("count", "1"))
            value *= Fraction(2) - Fraction(1, 2 ** count)
        tuplet = element.find("PrimaryTuplet")
        if tuplet is not None:
            value *= Fraction(int(tuplet.get("den")), int(tuplet.get("num")))
        return value

    @staticmethod
    def _tempos(master):
        """Tempo is not on the bar; it is an automation on the master track."""
        found = []
        if master is None:
            return found
        for automation in master.findall("Automations/Automation"):
            if text(automation, "Type") != "Tempo":
                continue
            parts = text(automation, "Value").split()
            beats = TEMPO_BEAT.get(int(parts[1]) if len(parts) > 1 else 2, Fraction(1))
            found.append({
                "bar": int(text(automation, "Bar", "0")),
                "position": float(text(automation, "Position", "0") or 0),
                "quarter_bpm": float(parts[0]) * float(beats),
            })
        return sorted(found, key=lambda t: (t["bar"], t["position"]))


# --------------------------------------------------------------- flattening ---

def played_order(score, expand_repeats=True):
    """
    The notated score is not the played score.

    Repeats are flattened into a list of master bar indices here, once, so that
    nothing downstream ever has to reason about a jump. Alternate endings are
    not handled -- P1's job, and the reason this pass exists as its own step.
    """
    if not expand_repeats:
        return list(range(len(score.masterbars)))

    order, index, section_start, passes = [], 0, 0, {}
    while index < len(score.masterbars) and len(order) < 10000:
        bar = score.masterbars[index]
        if bar["repeat_start"]:
            section_start = index
        order.append(index)
        if bar["repeat_end"]:
            passes[index] = passes.get(index, 0) + 1
            if passes[index] < max(bar["repeat_count"], 1):
                index = section_start
                continue
        index += 1
    return order


def note_events(score, track_index, order):
    """Every note of one track, as (start, duration, pitch, velocity) in ticks."""
    events, position = [], Fraction(0)

    for bar_index in order:
        master = score.masterbars[bar_index]
        bar_length = Fraction(master["numerator"] * 4, master["denominator"])
        bar_ids = master["bars"]
        if track_index >= len(bar_ids):
            position += bar_length
            continue

        # Voices run in parallel, each from the start of the bar.
        for voice_id in score.bars.get(str(bar_ids[track_index]), []):
            if voice_id < 0:
                continue
            offset = Fraction(0)
            for beat_id in score.voices.get(str(voice_id), []):
                beat = score.beats.get(str(beat_id))
                if beat is None:
                    continue
                duration = score.rhythms.get(beat["rhythm"], Fraction(1))
                velocity = DYNAMICS.get(beat["dynamic"], 79)

                for note_id in beat["notes"]:
                    note = score.notes.get(str(note_id))
                    if note is None or note["midi"] < 0:
                        continue
                    start = position + offset
                    sounding = duration

                    # A tie does not restrike: find what is already ringing at
                    # this pitch and lengthen it instead. The single most
                    # audible thing a naive importer gets wrong.
                    if note["tie_destination"]:
                        for held in reversed(events):
                            if held["pitch"] == note["midi"] and held["end"] == start:
                                held["end"] += sounding
                                break
                        continue

                    loudness = velocity
                    if note["accent"]:
                        loudness = min(127, loudness + 16)
                    if note["ghost"]:
                        loudness = max(1, loudness // 2)
                    if note["muted"]:            # a dead note: a click, not a pitch
                        sounding = min(sounding, Fraction(1, 8))
                        loudness = max(1, loudness - 20)
                    elif note["palm_muted"]:
                        sounding = sounding * Fraction(1, 2)

                    events.append({"start": start, "end": start + sounding,
                                   "pitch": note["midi"], "velocity": loudness})
                offset += duration
        position += bar_length

    ticks = lambda q: int(q * TICKS_PER_QUARTER)
    return sorted(((ticks(e["start"]), ticks(e["end"]), e["pitch"], e["velocity"])
                   for e in events), key=lambda e: (e[0], e[2]))


def tempo_map(score, order):
    """Tempo changes placed on the played timeline rather than the notated one."""
    if not score.tempos:
        return [(0, 120.0)]

    starts, position = {}, Fraction(0)
    for bar_index in order:
        master = score.masterbars[bar_index]
        starts.setdefault(bar_index, position)   # first pass through wins
        position += Fraction(master["numerator"] * 4, master["denominator"])

    changes = []
    for tempo in score.tempos:
        start = starts.get(tempo["bar"])
        if start is None:
            continue
        tick = int((start + Fraction(tempo["position"]).limit_denominator(64))
                   * TICKS_PER_QUARTER)
        changes.append((tick, tempo["quarter_bpm"]))
    return sorted(changes) or [(0, 120.0)]


# -------------------------------------------------------------- MIDI output ---

def varlen(value):
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


def chunk(name, body):
    return name + struct.pack(">I", len(body)) + body


def midi_track(events):
    """events: (tick, bytes) pairs, unsorted. Returns one MTrk chunk."""
    body, previous = bytearray(), 0
    for tick, message in sorted(events, key=lambda e: e[0]):
        body += varlen(tick - previous) + message
        previous = tick
    body += varlen(0) + b"\xFF\x2F\x00"
    return chunk(b"MTrk", bytes(body))


def assign_channels(score):
    """Channel 10 is percussion and nothing else may have it."""
    free = [c for c in range(16) if c != DRUM_CHANNEL]
    channels = []
    for track in score.tracks:
        if track["type"] == "drumKit":
            channels.append(DRUM_CHANNEL)
        else:
            channels.append(free.pop(0) if free else 15)
    return channels


def write_midi(score, order, path, only_track=None):
    channels = assign_channels(score)

    conductor = []
    for tick, bpm in tempo_map(score, order):
        micros = int(60_000_000 / max(bpm, 1))
        conductor.append((tick, b"\xFF\x51\x03" + struct.pack(">I", micros)[1:]))
    position = 0
    for bar_index in order:
        master = score.masterbars[bar_index]
        denominator = master["denominator"].bit_length() - 1
        conductor.append((position, b"\xFF\x58\x04"
                          + bytes([master["numerator"], denominator, 24, 8])))
        position += int(Fraction(master["numerator"] * 4, master["denominator"])
                        * TICKS_PER_QUARTER)

    chunks = [midi_track(conductor)]
    wanted = range(len(score.tracks)) if only_track is None else [only_track]
    for index in wanted:
        track, channel = score.tracks[index], channels[index]
        events = [(0, b"\xFF\x03" + varlen(len(track["name"].encode()))
                   + track["name"].encode()),
                  (0, bytes([0xC0 | channel, track["program"] & 0x7F]))]
        for start, end, pitch, velocity in note_events(score, index, order):
            events.append((start, bytes([0x90 | channel, pitch & 0x7F, velocity])))
            events.append((max(end, start + 1), bytes([0x80 | channel, pitch & 0x7F, 0])))
        chunks.append(midi_track(events))

    header = chunk(b"MThd", struct.pack(">HHH", 1, len(chunks), TICKS_PER_QUARTER))
    with open(path, "wb") as out:
        out.write(header + b"".join(chunks))
    return path


# ------------------------------------------------------------------- audio ---

def soundfont(explicit=None):
    for candidate in filter(None, [explicit, DEFAULT_SOUNDFONT,
                                   "/usr/share/sounds/sf2/default-GM.sf2"]):
        if os.path.exists(candidate):
            return candidate
    sys.exit("no SoundFont found -- pass --soundfont, or install fluid-soundfont-gm")


def fluidsynth(args):
    if not shutil.which("fluidsynth"):
        sys.exit("fluidsynth is not on the path -- sudo apt install fluidsynth")
    return subprocess.run(["fluidsynth"] + args, check=False).returncode


def play(midi_path, sf2):
    return fluidsynth(["-a", "pulseaudio", "-q", "-i", sf2, midi_path])


def render(midi_path, wav_path, sf2):
    return fluidsynth(["-F", wav_path, "-q", "-i", "-r", "48000", sf2, midi_path])


def render_stems(score, order, directory, sf2):
    """One synth pass per track. The whole reason for the project, in miniature."""
    os.makedirs(directory, exist_ok=True)
    written = []
    for index, track in enumerate(score.tracks):
        stem = "".join(c if c.isalnum() or c in "-_" else "_" for c in track["name"])
        base = os.path.join(directory, f"{index:02d}-{stem}")
        write_midi(score, order, base + ".mid", only_track=index)
        render(base + ".mid", base + ".wav", sf2)
        written.append(base + ".wav")
        print(f"  stem {index}: {track['name']}")
    mix = os.path.join(directory, "mix.mid")
    write_midi(score, order, mix)
    render(mix, os.path.join(directory, "mix.wav"), sf2)
    written.append(os.path.join(directory, "mix.wav"))
    return written


# -------------------------------------------------------------------- main ---

def describe(score, order):
    print(f"{score.title or '(untitled)'} — {score.artist or '(no artist)'}")
    print(f"  Guitar Pro {score.version}, {len(score.masterbars)} bars notated, "
          f"{len(order)} played")
    if score.tempos:
        print("  tempo   " + ", ".join(f"{t['quarter_bpm']:g} bpm at bar {t['bar'] + 1}"
                                       for t in score.tempos[:6]))
    alternates = sum(1 for m in score.masterbars if m["alternate"])
    if alternates:
        print(f"  warning: {alternates} alternate endings ignored (P1's job)")
    for index, track in enumerate(score.tracks):
        tuning = " ".join(str(p) for p in track["tuning"]) or "—"
        count = len(note_events(score, index, order))
        print(f"  [{index}] {track['name']:<22} {track['type']:<16} "
              f"prog {track['program']:<3} {count:>5} notes   tuning {tuning}")


def main():
    parser = argparse.ArgumentParser(
        description="P0 spike: Guitar Pro 7/8 in, MIDI and stems out.")
    parser.add_argument("file", help="a .gp file (Guitar Pro 7 or 8)")
    parser.add_argument("-o", "--output", help="write a MIDI file here")
    parser.add_argument("--play", action="store_true", help="play it now")
    parser.add_argument("--stems", metavar="DIR", help="render one WAV per track")
    parser.add_argument("--soundfont", help="SoundFont to synthesise with")
    parser.add_argument("--no-repeats", action="store_true",
                        help="read the score as notated rather than as played")
    args = parser.parse_args()

    score = Score(args.file)
    order = played_order(score, expand_repeats=not args.no_repeats)
    describe(score, order)

    if args.stems:
        print(f"\nrendering stems into {args.stems}")
        for path in render_stems(score, order, args.stems, soundfont(args.soundfont)):
            print(f"  {path}")
        return

    output = args.output or os.path.splitext(os.path.basename(args.file))[0] + ".mid"
    write_midi(score, order, output)
    print(f"\nwrote {output}")
    if args.play:
        play(output, soundfont(args.soundfont))


if __name__ == "__main__":
    main()
