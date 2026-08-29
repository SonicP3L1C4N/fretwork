<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# The release candidate for 0.1.0

Four passes over the tree on **2026-08-29**, three days before the date in the
metainfo: the suite in every configuration that builds, a stranger following
nothing but the README, and an afternoon spent trying to break the importer on
purpose. This is what they found, what was done about it, and the two things
that were left open on purpose.

It is kept as a document rather than as a set of closed issues for the same
reason [lv2-worker-crash.md](lv2-worker-crash.md) is: the useful part is not
the list of bugs, it is what the bugs were evidence of.

## What was already right

Worth saying first, because the rest of this page is a list of faults and a
list of faults is not a description of a program.

- **23 suites, 382 assertions, no failures** in every configuration that
  builds: full, with `lilv` hidden, with PipeWire hidden, and with the corpus
  present. All five transcriptions import and survive a `.fw` round trip.
- **A hundred round trips do not drift.** A score written, reopened and
  rewritten a hundred times is byte-identical at rounds 1, 25, 50, 75 and 100.
- **The silence that was feared does not happen.** With the SoundFont hidden,
  the window opens and its status bar already reads *"No SoundFont is
  installed, so there is nothing to play the score with. Install
  fluid-soundfont-gm, or name one with --soundfont"* — before Play is pressed,
  without being asked. The empty-window state names the one thing to do next.
  This was the single most likely bad first experience the project had, and it
  is answered.
- **The refusals that were promised are made.** Alternate endings announce
  themselves unprompted; the MIDI channel compromise names the track it
  applies to. Truncated archives, path traversal, entity expansion, dangling
  ids in a hand-edited `.fw`, future format versions, NUL bytes and non-UTF-8
  in titles, and a score of 200 tracks by 250 bars are all handled without a
  crash or a wrong answer.

## What was wrong, and what it was evidence of

### 1. A file could stop the program without crashing it

A `.gp` containing nothing but nested empty elements — **875 bytes on disk at
100,000 deep** — took twelve seconds to fail, and 200,000 deep took a minute
and a half. The cost is `QDomDocument::setContent()`, which builds the whole
tree before anybody asks it anything and does so at a price that grows faster
than the document does.

Nothing crashed. That is what made it worth fixing rather than interesting: a
crash is a thing a user can see and report, and a program that has simply
stopped answering, over a file that arrived by mail looking like a song, is a
program that appears broken with nothing to say about why.

Refused now by a depth scan before the tree is built — a pull parser, which is
linear and keeps no tree, stopping at the first element past the limit rather
than reading to the end. Real gpif nests about ten deep; the limit is a
hundred. **200,000 deep now fails in a tenth of a second**, naming the reason.

### 2. The archive cap bounded the wrong number

`MaximumArchiveSize` refuses a `.gp` larger than 256 MB on disk, and the
comment above it makes the argument well: tablature is small, and reading a
whole disk into memory because somebody renamed something costs nothing to
refuse.

It bounds what is read from disk. It says nothing about what that becomes. A
deflate stream of zeroes runs to about a thousand times its own length, so a
**285 KB `.gp` took the process to 1.0 GB of resident memory in 0.7 seconds**,
and a file small enough to send by mail could take the machine with it. The
`.fw` reader shares the same entry path, so both formats were open to it.

The fix is the same argument one level down: a cap on what an entry says it
unpacks to, checked before anything is allocated to hold it. The same file is
now refused in a tenth of a second at no cost in memory.

The general form, which is the part worth keeping:

> A limit on the size of a thing is not a limit on the size of what it turns
> into. Compression is the whole business of the difference between those two
> numbers, and a reader that checks the first has checked the number the
> sender did not choose.

### 3. Two halves of the program disagreed about a name

`--voicing 0:0=Iron Man` set the amplifier for `--render` and did nothing
whatever when the window opened. Both were reading the same nineteen presets
off the same disk.

The command line resolves a name three ways — exactly, then ignoring case,
then as a fragment, with an ambiguous fragment refused and both candidates
named — because "Bass - Come Together" is a lot to type accurately. The window
compared the name for equality and, finding nothing, **returned without a
word**: no amplifier, no message, and no entry in the rig file. The preset is
called "Distortion - Iron Man", and the short name is the one everybody
reaches for.

This was reported to the release pass as *"the rig does not persist voicings"*,
which was wrong and was worth chasing down rather than acting on: the rig
persists them correctly, and does so today with the full name. What did not
work was the lookup in front of it. The visible symptom of the two faults is
identical — an amplifier that is not there when the score is reopened — and
they are one line and one subsystem apart.

Both ends now call `Gx::named`, which returns what a name resolved to and why
it did not. The window gained the two messages it never had, and the rig now
records the name the bank uses rather than the fragment somebody typed, so a
rig written today still resolves after another bank is installed.

> Where two paths answer the same question, the question belongs in one place.
> The cost of the second copy is not the duplication; it is that only one of
> them was ever taught to say no.

### 4. A typo drew the wrong track and reported success

`--track banana --png out.png` wrote a file, exited 0, and drew track 0 —
because `QString::toInt()` without its flag answers 0 for anything it cannot
read, and 0 is also what `--track` means when nobody has said anything.
`--page` had the same shape.

Three switches in the same file already check: `--sfz`, `--lv2` and `--knob`
all take the flag and refuse by name. These two did not, and a wrong answer
given confidently is the failure this program can least afford. Checked now,
before any file is opened, refusing rather than falling back.

## What was left open, and why

- **Slides, tremolo, harmonics, grace notes and trills are flattened without
  the program saying so.** Eight scores through `--info` and `--render`, some
  of them full of slides, produced no message anywhere — the README documents
  it and nothing else does. By the standard the rest of the program is held
  to, that is a defect: alternate endings and the MIDI channel compromise both
  announce themselves, and these should. It is a feature rather than a fix,
  and it is not in 0.1.0.
- **`portedoutput` and `audioinput` have no tests**, because testing them
  honestly means opening real PipeWire I/O in the test process, and a test that
  needs a sound card is a test that does not run. Closing this needs an
  injectable backend, which is a design change and not a test.
- **Noise on startup.** Four `GLib-GObject-CRITICAL` lines on every window,
  `QThreadStorage` warnings from `--help` alone, and raw FluidSynth driver
  chatter ahead of the program's own errors. None of it blocks anything, and
  all of it makes a stranger wonder whether the build is broken.
- **The worker mutex is not exercised by anything.** The pass did not
  reproduce the concurrency of [lv2-worker-crash.md](lv2-worker-crash.md), and
  the same reason still holds: it needs a plugin with a worker installed, which
  is most machines and no CI.

## How it was checked

Every finding here was reproduced before it was believed and again after it was
fixed. The fixture recipes are in the tests rather than in this document:
`gpiftest::refusesADocumentNestedTooDeeply`,
`zipreadertest::refusesAnEntryThatClaimsToUnpackToMoreThanAnyScore`, and
`jacktransporttest`, which covers the one runtime-loaded piece and asserts the
contract rather than the machine.

The soundfont-absent case was measured by hiding the file with `bwrap` rather
than uninstalling the package, which is the same trick the CI matrix plays with
`.pc` files and needs no privileges.
