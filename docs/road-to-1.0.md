<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# The road to 1.0

Six directions, named **2026-09-03** and written up the same day against `main`
at 0.4.0. Each is measured against what is in the tree rather than against what
it sounds like, and that moved two of them: **one is already finished**, and
four of the remaining five turn out to be the same missing piece wearing four
different names.

This sits below [roadmap.md](roadmap.md) rather than beside it. That document
is three directions committed to in principle; this one is the question of what
the first number before the decimal point actually means, and which of these
six is allowed to happen before it.

## What 1.0 means

Not a feature count. [roadmap.md](roadmap.md) already set the gate, and it is
worth quoting because half of it has now been met and half has not:

> nothing in this document starts until 0.1.0 is out, the LV2 crashes are
> recorded and fixed, and someone other than the author has built the program.

0.4.0 closed the crashes — the planner one, the use-after-free in the plugin
host, and the divide-by-zero a crafted file could reach — each with a test
where a test was possible. **The half that has not moved is the last clause.**
Nobody but the author can install this program. [distribution.md](distribution.md)
decided where it goes on 2026-09-02 and step one of nine is a GitHub release
with an AppImage, priced at two days, and it has not been done.

So: **1.0 is the version a stranger can install and use without being told how
by its author.** None of the six below is worth a week before that is true, and
the cheapest item on this whole page is the one that gates every other item on
it.

## The six, measured

| | Direction | Where it actually is | What it needs first |
|---|---|---|---|
| 1 | **Writing tools for transcribing** | The program, minus the source recording | A reference track: load audio, loop it, slow it down |
| 2 | **Collab — sharing scores, stems, riffs** | The bundle is nearly built; the hosting is a service | A decision not to run a service |
| 3 | **Practising together, with a band leader** | Nothing, and the obvious design is the wrong one | The control interface, and a playback loop |
| 4 | **Record stems to third-party DAWs** | **Done, since P4** | A page of documentation |
| 5 | **Discord** | Two features wearing one name | The control interface, for the half worth having |
| 6 | **OBS and Stream Deck plugins** | Clients, not features | The control interface |

## 4 is already built, which changes the shape of the list

Stem recording into a DAW exists in both of the forms it can take, and has
since P4.

- **As files.** `--render` and `--stems` write a WAV per track in one pass;
  `--dry` writes the effected parts a second time before their chains, so an
  amplifier chosen on one evening is reversible on another; `--click` writes
  the metronome as a stem of its own rather than into the mix, because a click
  in the mix is one nobody can take out again.
- **As ports.** `--ports` gives every track a stereo pair in the PipeWire graph
  — one node with many ports, so there is one callback and one answer to what
  time it is. `src/audio/portedoutput.h` states the use case in as many words:
  *"Ardour or Reaper arms eight tracks, links them to these, and presses
  record."*
- **As a transport.** `--follow` drives the graph's transport through JACK,
  which exists because the obvious direction turned out not to be available:
  Reaper on Linux can query a transport and cannot set one, so the program that
  presses play has to be this one.

Tested by `rendertest`, `audioportstest` and `jacktransporttest`. The only real
gap left is one already on the wishlist — a dry pair on the *live* ports, not
only in a render. Everything else this direction wants is a page in the manual
telling somebody it is there, and that is documentation, not a roadmap item.

**It comes off the expansion list.** A plan that lists finished work as
future work is wrong about its own size in the direction that hurts most.

## The prerequisite four of them share

Searching `src/` for `QTcpSocket`, `QUdpSocket`, `QNetworkAccessManager`,
`QWebSocket`, `QLocalSocket` and `QDBus` returns nothing at all. Fretwork can
be addressed from the command line and through the audio graph, and nowhere
else.

That is the whole of what stands between the program and items 3, 5 and 6 —
and they are not three features. **They are three clients of one interface.**
Something outside the program needs to ask where the transport is, and tell it
to go somewhere else. A Stream Deck key, an OBS overlay, a Discord presence
line and a band leader's session control all ask for exactly that and nothing
more.

**The model for it already exists.** `Session` is a `QObject` whose state is
already published as notifying properties — `position`, `currentBar`,
`barCount`, `playing`, `title`, `tempoHere`, `click`, `currentTrack`, the track
names — and whose navigation is already invokable: `seek(double)` at
`src/gui/session.h:577`, `goToBar(int)` at `:586`. An adaptor over that is a
second face on an object that already has the right one. This is not a new
architecture; it is a new way to reach the one that is there.

**There is precedent for outside control, too.** `src/audio/mackie.h` already
takes a control surface off the wire and turns it into "one statement about
what somebody did with their hands", deliberately knowing nothing about what a
track or a plugin is, so that whoever asked decides what it means. The control
interface is that same split over a different wire, and it is testable the same
way — without any of the four clients present.

**D-Bus**, for the reasons that matter to this project specifically: it is the
KDE-native answer, it opens no network socket, it needs no accounts, it is
introspectable and drivable from a shell for testing, and all four clients can
speak it — Python for StreamController, a small bridge for an OBS browser
source, anything at all for the rest.

What it publishes and accepts is small, and worth fixing early so it does not
grow by accident:

| Direction | Members |
|---|---|
| Read | title, artist, bar, bar count, section, tempo, playing, track names |
| Write | play, stop, `goToBar`, loop a bar range, tempo scale, click on or off |

**Build it once.** Four features that each grow their own version of this is
four incompatible halves of the same object, and the fourth one written is the
one nobody will go back and reconcile.

## 3. Practising together, which is the one worth getting right

### The design that does not work

Carrying the players' audio between them. It fails on physics rather than on
effort. Ensemble playing falls apart somewhere around 20 to 30 milliseconds of
delay — roughly what standing eight metres apart already costs — and a
round trip across a country is most of that budget before anything is buffered
at either end. Two audio buffers and a codec spend the rest.

This is not an unsolved problem so much as a *whole* problem: JackTrip and
Jamulus exist, are good at it, and it is the entire reason each of them exists.
Fretwork attempting it would be a worse version of a program somebody can
already install, at the cost of every other item on this page.

### The design that does

**Sync the score, not the sound.** The leader's transport position, tempo, loop
range and current bar go to everyone; each client renders locally, from a score
everyone already has. Nobody is waiting on anybody's packets, because nobody is
carrying anybody's sound.

Everything that makes the audio design impossible stops applying:

- **What crosses the network is tens of bytes**, not a stream. "Go to bar 33
  and loop it at eighty per cent" is a sentence, not a signal.
- **Being forty milliseconds late does not matter**, because the message is an
  instruction about where the music is going, not a sample that had to be in
  a buffer before it was played.
- **Nobody chases anybody's clock.** Each client runs its own, and the leader's
  position is a correction applied at a bar line, not a master clock to lock
  to. A follower that drifts is nudged at the next bar and never audibly.
- **It is what a rehearsal actually is.** Everyone on the same bar, the same
  click and the same tempo, and the sound in the room is the room's. A band in
  one room does not need the network to carry what the air is already carrying.

### The band leader

One client holds the session. **Control is a role that is handed over, not a
vote**: the leader owns the transport, the tempo, the loop and which section
everyone is looking at, and passing it to somebody else is one message. A
follower is either locked to the leader's bar or free to browse their own part
while the session runs — that choice is theirs, not the leader's, and it is the
difference between a rehearsal tool and a surveillance one.

### LAN first, and possibly LAN only

Discovery by mDNS, no server, no accounts, no NAT traversal, no relay, no
moderation. **The common case for a rehearsal is one room**, and the version
that serves it needs none of the machinery that the internet version cannot do
without. Going further than the LAN means running a relay, and a relay is a
service, which is refused below.

### What it needs first

- **The control interface.** Session sync is the same verbs over a different
  transport; if the interface is right, this is a second speaker of it.
- **A playback loop, which does not exist.** Verified rather than assumed:
  every occurrence of "loop" in `src/` is a sampler loop point, a PipeWire
  thread loop, or a Qt event loop. `seek` and `goToBar` are there; looping a
  bar range is not.
- **A tempo scale**, for the same reason — "at eighty per cent" is half of what
  a leader ever says.

The pleasing part is where those last two already are: [wishlist.md](wishlist.md)
lists **"Loop a selection"** and **"A speed trainer"** under *Practising with
it*, both small, both wanted for their own sake. They are now prerequisites for
the largest new thing on this page as well, and they are useful on the evening
they land rather than on the evening the session protocol is finished.

## 2 and 5, which are services wearing the clothes of features

Sharing scores and stems, and a Discord community, look like features and are
operational commitments. A service must stay up, hold accounts, and be
moderated, by a project whose bus factor is one.

### Sharing: the bundle exists, the hosting must not

The format half is nearly done. `.feedpak` already bundles a score, its
notation and its stems into one ZIP — it is simply addressed to somebody else's
program. **A `.fwpak` that is score, stems and rig under one name, pointed at
Fretwork itself, is days of work and no servers**, and it is the entire useful
core of "share a riff with the guitarist in your band".

What should not be built is the other half: hosting, identity, moderation,
takedowns. There is a legal edge here as well as an operational one.
[architecture.md](architecture.md) is careful that the test corpus is
transcriptions the author owns, not redistributed and gitignored by extension —
that care exists for a reason, and a service that hosts other people's
transcriptions of published songs walks straight into it. **Ship the bundle and
let people use the sharing they already have.**

### Discord: presence yes, a bot no

- **Rich Presence** is a write to a local IPC socket: "Fretwork — Sharp Dressed
  Man, bar 33". It is small, it is a client of the control interface like the
  others, and it is fine as an optional module that is off by default and never
  a build dependency.
- **A bot, or a community platform integration**, is a service on top of a
  proprietary platform. For a project heading for Flathub and KDE Invent, a
  hard dependency on a closed platform is a packaging problem and a values
  problem at once. A community lives wherever its users already are and does
  not need any code in this repository.

## 1. Transcribing, the one genuinely missing musical feature

The editor is done, and so is everything around it: the fretboard solver, key
and chord analysis, voicings, and as of 0.4.0 note entry from a keyboard
against the transport. What a transcriber does not have is **the source**.

**A reference track: load a recording beside the score, loop a bar against it,
and slow it down without dropping the pitch.** That is the feature that changes
what the program is used for on a Tuesday evening.

- **The loop** is shared with everything above, which is now three uses for it.
- **Slowing down without dropping the pitch** is time-stretching, and it is the
  only genuinely hard part. Rubber Band is GPL and packaged on every target
  distribution, and this project's licence makes borrowing it free.
- **The expensive part is not the stretching, it is the alignment.** A
  reference track needs an anchor — the score's bar one against a position in
  the file — and, if the recording is not click-locked, a tempo map. Most
  records are not click-locked. Naming that now is the difference between a
  feature priced at days and one that quietly becomes a fortnight.
- **This is not automatic transcription.** `pitchdetector` exists and is
  tested, but it hears one note; polyphonic transcription is a research problem
  and is not on this page. The feature is "play along with the record", not
  "the computer writes it out for you".

## The line not to cross

Three refusals, written down now so that they are decisions rather than
arguments later. This follows the pattern the roadmap already set with the MIDI
work, where saying what it would not grow into was cheaper before it existed
than after.

1. **Not a DAW.** Unchanged, and for the unchanged reason: there is no
   recording of audio, no arrangement of it, and no timeline that is not the
   score.
2. **No real-time audio between players.** The reasoning is above. Somebody who
   wants that wants JackTrip, and should be told so in the manual rather than
   sold a worse one.
3. **No service that Fretwork has to run.** No accounts, no hosting, no relay,
   no moderation queue. Everything on this page works without a machine the
   author pays for and has to keep up. The day one of these needs a server is
   the day it needs a second maintainer, and there is not one.

The common thread is worth naming, because it is what all three tests for: each
is a point where a feature stops being code and becomes an obligation. Code can
be left alone for a month. An obligation cannot.

## Order, and why

| | What | Why here | Rough cost |
|---|---|---|---|
| **0** | **Ship it.** GitHub release, AppImage, then AUR | It gates everything else on this page, and it is the cheapest thing on it | Two days, already priced in distribution.md |
| **1** | The control interface, on D-Bus over `Session` | Unblocks three of the six at once; the state and the verbs already exist | Two to three days |
| **2** | Loop a bar range, and a tempo scale | Prerequisite for items 3 and 5 below, two existing wishlist items, and useful alone on the day they land | Days |
| **3** | Reference track, with time-stretching | The transcription half, and the only one that needs no network at all | A week, plus the alignment question |
| **4** | Stream Deck keys and an OBS browser-source overlay | Cheap once 1 exists, and both are testable on your own desk with hardware you already run | Days each |
| **5** | LAN session sync, band-leader model, transport only | The largest new thing, and it is small once 1 and 2 are there | One to two weeks |
| **6** | `.fwpak` bundle, and Discord presence if wanted | Independent of everything else, and neither is urgent | Days each |

Not on the list: stem recording to a DAW, which is finished and needs
documenting instead.

## What this costs, honestly

[roadmap.md](roadmap.md) already carries three unfinished directions — harmony,
MIDI and fee[dB]ack — and this page adds six more to a tree of about 28,500
lines of C++ with one maintainer. Two of the six are large. The order above is
the argument that they can be taken cheaply *if* the shared piece is built
first and the two service-shaped ones are declined, and that argument is only
as good as the discipline to actually decline them.

There is one cost that is specific rather than general, and it deserves its own
sentence. **The security review before 0.4.0 found three real problems in a
program with no network code at all** — a crafted file that crashed it, a
use-after-free in the plugin host, and a way for a fork's pull request to reach
the machine running the corpus. Adding a listening socket to that program is
the riskiest single thing proposed on this page. It is also the strongest
argument for the two design choices above that were made for other reasons:
**D-Bus rather than a socket** means nothing listens on the network, and
**LAN-only** means nothing arrives from a stranger.

And the honest summary, which is the same one the last three planning documents
reached: none of this is the difference between a program with no users and a
program with users. Installability is.

## Open questions

- **Does the leader push edits, or only position?** Editing together is a
  different and much harder problem — conflict resolution, and a data structure
  chosen for it. Pushing a position is not. The first version should be
  position only, and say so.
- **Is a follower locked to the leader's bar, or free to wander?** The
  suggestion above is that it is the follower's choice. It is worth deciding
  before the protocol, because it changes what the protocol carries.
- **Where does a reference track live** — in the `.fw` file, beside it, or in a
  `.fwpak`? The copyright argument says beside it, and that a score file must
  not become a way to pass a record around.
- **Is the control interface public?** A documented interface other programs
  may rely on is a compatibility promise for ever; Fretwork's own back door is
  not. Cheap to decide now, expensive to decide after the second client exists.
- **Does mDNS survive a rehearsal room's Wi-Fi?** Guest networks routinely
  isolate clients from one another. Measure it on a real one before promising
  discovery works.
