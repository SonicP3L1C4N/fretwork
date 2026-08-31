<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Fretwork — where it stands, and what a release candidate needs

> **Written 28 August 2026, against `main` as it stood then, and kept as
> written.** It is published because the reasoning is the useful part, not the
> list — this is the document 0.1.0 was planned from, and nearly all of it was
> acted on. Its suggested order was followed almost exactly: there is CI now,
> the SoundFont message, `renderer` and `wav` tests, rig persistence, an
> AppStream metainfo, a man page, a `0.1.0` changelog heading and a matching
> tag. Three of the test gaps it names are still open — `portedoutput`,
> `tracksynth` and `audioinput` have no suite of their own. And the question it
> declines to answer was answered its way: 0.1.0 shipped at the P3/P4 boundary
> with LV2 and SFZ marked experimental.

Assessed against `main` as fetched 28 August 2026, `docs/wishlist.md`, and the
phase table in `docs/architecture.md`.

---

## Where the code actually is

About 20,600 lines of C++/QML across nine modules, against 6,900 lines of test
in 18 suites. A third of the tree being test is a good ratio for a solo
project and better than most.

P0–P3 are done and the README's account of them matches what is in the tree.
P4 is genuinely begun rather than nominally begun: `lv2chain`, `portedoutput`,
`jacktransport`, `sfz`, `sampler` and `tracksynth` all exist, and four of them
have tests.

**The coverage is not evenly spread, and the gap sits exactly on the
differentiator.** These have no test suite at all:

| Module | What goes untested |
|---|---|
| `audio/portedoutput` | the per-track live ports — the stem story |
| `audio/tracksynth` | the per-track synth |
| `audio/renderer` | stem rendering, `--render --dry`, `--render --mute` |
| `audio/wav` | what a rendered stem actually is on disk |
| `audio/jacktransport` | runtime-loaded, so most likely to fail elsewhere |
| `audio/audioinput` | `pitchdetector` and `tuner` are tested; their source is not |

`renderer` and `wav` are the pair I would close first. Stem export is the
thing people would come to Fretwork for, and a stem that is quietly missing a
note is the failure this project can least afford.

---

## The four gaps that are not features

None of these are on the wishlist because they are not wishes. They are the
difference between a repository and a release.

**1. There is no CI.** No `.github/`, no pipeline of any kind. The
architecture doc's claim that the corpus assertion runs on every commit is not
made true by any mechanism — it is true only while you remember to do it.
Highest-leverage single thing on this list.

**2. There is no AppStream metainfo.** There is
`io.github.sonicp3l1c4n.fretwork.desktop` and nothing else. Without the
metainfo XML and a screenshot, no software centre will list the program at
all. You have four usable screenshots in `docs/` already.

**3. There is no man page**, and `--help` coverage against the README is
unverified.

**4. The soundfont failure path is unverified.** `fluid-soundfont-gm` is
installed by hand. A first run without it should say so in words. Right now
the most likely first experience a stranger has with Fretwork is silence with
no explanation, and that is worth more than any feature on the wishlist.

---

## The one wishlist item I would promote

**Keeping a rig.** The knobs turn, and they are forgotten when the program
closes.

The project's stated reason for existing is per-track amplifier chains and
real stems. A user who builds a rig across five tracks and loses it on quit
has not used the flagship feature — they have previewed it. Everything else on
the wishlist is an addition; this one is a hole in the thing the program is
for.

The wishlist already frames the decision correctly: not in the `.fw`, for the
same reason the sample library is not, because it names plugins and paths that
are facts about one machine. A session file beside the score, or a named
preset of Fretwork's own. Either works. Pick one.

---

## What is not a blocker, and should be said out loud

- **Untranslated techniques** — slides, tremolo, harmonics, grace notes,
  trills, alternate endings. These are *announced*. A stated limitation is not
  a defect, and the program refusing to play what it cannot play honestly is
  one of its better qualities. Ship with them open.
- **The Rust importers** for GP3–GP5 and GPX. P5.
- **Standard notation, MusicXML, GP6.** P5, stated as possibly never.
- **`suil` unused.** Plugin interfaces are a later question than plugin audio.

One caveat on the first of those: the honesty only works if the announcement
reaches the user, not just the README. Worth checking that every limitation
surfaces in the window and in `--help`, not only in `README.md`.

---

## The question I cannot answer for you

**A release candidate for what?**

The phase table says P4 ends when "it sounds like a guitar", and P4 is begun,
not done. Two readings:

- **RC for 0.1 at the P3/P4 boundary** — a tablature editor and player with
  stem export, LV2 and SFZ present but marked experimental. Shippable in a few
  weeks: CI, metainfo, the soundfont message, rig persistence, and the test
  gaps above. `CMakeLists.txt` already says `VERSION 0.1.0`, so the tree
  already believes this is the answer.
- **RC for 1.0 at the end of P4** — the differentiator complete. That pulls in
  pick and fret noise, per-string panning, a dry pair on the live ports, and
  the guitarix cabinet convolver. Months, not weeks.

My read is that the first is the right release and the second is the right
*version number* for it to be waiting on. The program already does something
nothing else on Linux does, and a 0.1 people can install is worth more than a
1.0 nobody has seen.

---

## Suggested order

1. CI running `ctest` on every push, both with and without `FRETWORK_CORPUS`.
2. The soundfont message, and the `--info`/window announcement of limitations.
3. Tests for `renderer` and `wav`, then `portedoutput` and `tracksynth`.
4. Rig persistence — decide session file or named preset, then build it.
5. AppStream metainfo with one of the existing screenshots, plus a man page.
6. `CHANGELOG.md` gets a `0.1.0` heading, tag matches, REUSE passes.
7. Run `firstrun` and `adversarial` against the resulting build. Then the
   go/no-go.
