<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Review: strengths, weaknesses, opportunities

An assessment of the project as it stands on **2026-08-28**, measured against
the code rather than against the documentation describing it. Where a claim
here has a number in it, the number came from running something: `ctest`, `git
log`, `wc`, or the crash reports in `~/.cache/drkonqi`.

Threats are included at the end. A survey that lists what could go right and
omits what could go wrong is half an argument.

## The fact that frames everything else

The first commit is dated **2026-08-25**. The most recent is **2026-08-28**.
In four days, 53 commits have produced 22,654 lines of C++ and QML across nine
subsystems, 8,025 lines of tests, a CI matrix, a design system, a file format,
and documentation good enough to be the project's best marketing asset.

Almost every strength below is a consequence of that pace, and almost every
weakness is a risk it created. Read the two lists as the same fact seen from
two sides.

## Strengths

**1. The premise is unoccupied, and provably so.** One synth per track, live
solo and mute, real stems out. Nothing else on Linux does it — not TuxGuitar,
not MuseScore, not any tab editor. This is not a better version of an existing
thing competing on quality; it is the only version of a thing, competing on
existing. That is the rarest position a project can be in and the hardest one
for anyone to argue with.

**2. Honesty is enforced in code, not promised in a README.** Transposition
that would run a note off the neck is refused outright rather than clamped. A
tempo of 1100 is refused rather than quietly becoming 400. A bar that no longer
adds up is marked rather than corrected. A paste that would overrun the score
is refused rather than half-applied. A voicing that cannot be fully carried
reports what it left behind. A score with alternate endings says it cannot
flatten them instead of playing something adjacent.

This is a coherent, unusual and *defensible* design position, and it is the
thing most likely to earn trust from the exact audience the project needs —
people who have been burned by software that silently rewrote their work.

**3. The reasoning is written down while it is still arguable.** architecture.md
records why each choice beat its alternative; gpif-format.md documents a format
with no vendor specification, measured against a real corpus; wishlist.md puts
a price next to every wish and a refusal list at the bottom so the noes are as
visible as the yeses. Most projects record decisions. This one records
arguments, which is what makes them revisable later by someone who was not
there.

**4. The CI is better than most funded projects'.** Specifically, and these
are not generic points:

- A build matrix that removes each optional dependency by *hiding its
  `.pc` file* rather than uninstalling it — because fluidsynth pulls some in
  transitively, and the question is what CMake does when `pkg_check_modules`
  fails. That is the only honest way to test the fallbacks and almost nobody
  does it.
- A job that extracts the apt line out of the README with awk and builds on a
  KDE-current base using exactly that and nothing else. Documentation rot is
  usually discovered by a stranger; here it fails the build.
- REUSE compliance, `appstreamcli validate --pedantic`, and
  `desktop-file-validate`, all gating.
- `--no-tests=error`, so a test suite that silently stopped building is a
  failure rather than a pass.

**5. The tests are fast, numerous and corpus-free.** 22 suites, 8,025 lines,
**all passing in 4.0 seconds** with no Guitar Pro file present — every
structural case is built in code. A four-second suite is a suite that gets run
on every save; a slow one is a suite that gets run before releases and
therefore does not prevent anything.

**6. The legal hygiene is designed in, not bolted on.** Transcriptions are
never committed, the corpus assertion lives on a separate self-hosted runner,
the trademark position is stated, and the interoperability basis is cited. For
a project whose existence depends on staying on the right side of exactly this
line, having it structurally impossible to commit a transcription is worth more
than a policy.

**7. The hard calls were answered by measurement, and the answers are in the
code.** An in-process lilv host with a real worker thread and two lock-free
rings, rather than the errand run on the audio callback — which would have
worked and would have been a lie about what the callback does. One PipeWire
node with many ports rather than a node per part, because a node per part is a
clock per part. YIN rather than a Fourier transform, because a plucked low
string's second harmonic is routinely louder than its fundamental. Bar widths
by the square root of duration. And the finding that Reaper on Linux can follow
a transport and cannot set one, which inverted the design of the whole feature.

Each of these is a place where the obvious choice was tried and rejected on
evidence. That is the signature of engineering rather than assembly.

**8. It is useful with no window.** The P1 discipline held: `--info`, `--play`,
`--render`, `--stems`, `--pdf`, `--tune` and the rig flags are a complete
product for someone who lives in a terminal, and they are also the reason the
GUI could be written quickly on top of something already known to work.

**9. It has a visual identity, and the identity is documented.** `Ink.qml` and
the design handoff pin every colour and its reason. Four days in, most projects
have the default theme.

## Weaknesses

**1. It segfaults, and nothing in the repository knows.** Eight crash reports
from **2026-08-27**, between 13:22 and 20:59 — six SIGSEGV, two SIGABRT. Every
one is an LV2 run (`--lv2` with guitarix, or `--render`). Three land in
`gx_amp.so` itself; two are inside FFTW's planner, which is notable because
**Fretwork links no FFTW at all** — it arrives inside a guitarix plugin.

There is no mention of any of this in CHANGELOG.md, no fix in the five commits
since, and no issue. The single most-marketed capability in the project is the
one that crashed eight times in one afternoon, and the record of it exists only
in a cache directory that gets cleaned.

A hypothesis worth an afternoon, not a diagnosis: FFTW's planner is documented
as not thread-safe — only `fftw_execute` is — and `lv2chain.cpp` gives *every
instance its own `Worker` thread*, with a mono plugin instantiated twice, one
per side. A chain of two mono plugins on four tracks is sixteen worker threads,
any two of which may plan concurrently inside the same non-reentrant library.
Instantiation itself is sequential, so if this is the cause it is the workers
and not the setup. Confirming or killing it costs one run under a thread
sanitiser.

**2. The least-tested code is the most-advertised code.** There is no test for
`session.cpp` (1,721 lines), `scoreview`, `portedoutput.cpp` (607 lines),
`jacktransport`, `audioinput`, or `Main.qml` (3,105 lines) — roughly 5,400
lines of the GUI and 900 lines of transport and port plumbing with no
automated coverage at all. `lv2test` exists, but the crashes are happening
where nothing runs.

The suite is excellent at the model, the importer, the layout and the
arithmetic — the parts that were built first and are least likely to break.
It has not yet caught up to the parts built this week.

**3. The README claims Rust that does not exist.** "The stack, briefly" says
*"Rust behind a C ABI for the file importers, because that is the only code
eating untrusted binary input"*. There is no Rust in the tree; `gpif.cpp` and
`zipreader.cpp` are C++.

architecture.md is entirely honest about this — it marks the importers "not yet
written", and argues correctly that GP7/8 is ZIP and XML rather than
hand-rolled binary and so does not need Rust. The README simply states in the
present tense a thing that is not built. In a project whose whole credibility
rests on never doing that, this is the highest-value correction available and
it is one sentence long.

**4. Untrusted input is parsed in C++, and nothing fuzzes it.** `zipreader` and
`gpif` read attacker-supplied files — a `.gp` is a thing people email each
other and download from forums. `cargo fuzz` is named in architecture.md as the
reason Rust wins for importers; the C++ that took its place inherited the
argument's premise without inheriting its mitigation. There is no fuzz target
in the tree.

**5. Two files are quietly becoming the project.** `Main.qml` is 3,105 lines
and `session.{h,cpp}` is 2,341. The facade that exposes everything to QML is on
the usual road to a god object, and a single QML file holding the entire window
is the thing that makes the next contributor's first change hard. Neither is a
problem yet; both are on a trajectory.

**6. Bus factor one, and no external contact whatsoever.** One author, 53
commits, four days. Nobody else has built it, no stranger has run it, no bug
has been filed by anyone who did not write the code. Every claim about how it
behaves is a claim about how it behaves on one machine with one desktop, one
PipeWire, one guitarix and one set of eleven transcriptions.

**7. The release is four days away and the flagship feature is mid-flight.**
The metainfo tags 0.1.0 for 2026-09-01. P4 is *begun*: ports, an SFZ path, a
per-part chain — and the unrecorded segfaults above. The release notes already
say "experimental", which is the honest framing, but the gap between the
README's confidence and the crash log is wider than the word carries.

**8. Nobody can install it.** Build from source, with a soundfont the user
installs by hand and a first run that can be silent. The intended audience is
guitarists; the delivery mechanism is `cmake -B build`. Every packaging option
is still an open question in the wishlist, including the flatpak that the
project's own architecture makes hostile.

**9. The public repository is not what this review describes.** Eight commits
unpushed and `Main.qml` modified in the working tree. Small and routine, but
worth noting in a document about a release in four days.

## Opportunities

Ordered by what they return for what they cost.

**1. Write the crashes down, then fix them.** An issue and a CHANGELOG line
cost an hour and convert the largest weakness on the list into evidence of the
project's stated virtue. A thread sanitiser run against a two-plugin chain on
four tracks either confirms the FFTW hypothesis or eliminates it; if confirmed,
a mutex around plugin instantiation and worker dispatch is a small change.
This is the highest-value hour available anywhere in the project.

**2. Fuzz the importer.** libFuzzer or AFL++ against `zipreader` and `gpif` is
an afternoon, closes the gap opened by weakness 4, and — because the corpus
cannot be shipped — a fuzzer that generates its own input is also the only
route to importer testing that a stranger can run. It doubles as one of the
better things the project could write about.

**3. gpif-format.md is the asset that travels furthest.** No vendor
specification exists for this format and nobody else has published a careful
one measured against a real corpus. It will be read, cited and linked by people
who never run the program, and it brings the kind of attention that arrives as
contributors rather than as support requests. It is already written; it needs
publishing as a thing in its own right.

**4. GP3, GP4 and GP5 import is the single largest reach multiplier.** Most tab
on the internet is still in the old binary formats. The work is bounded, well
charted by other projects, and lands entirely in the importer — and it is the
one place where the Rust argument becomes real rather than aspirational, which
means feature 4 and weakness 3 can be closed by the same piece of work.

**5. KDE Invent, and eventually kdereview.** The conventions, the licence, the
frameworks and the metadata are already KDE's, so the cost is administrative
rather than technical. What it buys is disproportionate: packaging routes,
translation infrastructure that is already linked but unplumbed, and — most
valuable of all — reviewers, which is the only real answer to bus factor one.

**6. One distribution, early.** An AUR package is roughly a day and reaches
precisely the audience that runs current Qt, current KF6, PipeWire and
guitarix. It converts "build from source" into "install it" for the users most
likely to file a useful bug.

**7. The tuner is a product hiding inside a program.** `fretwork --tune` needs
no score and no window, it does something no chromatic tuner does — tunes to
the piece's own tuning, capo included — and it demonstrates in eight seconds.
It is the cheapest hook the project has and it is currently the fifth thing
mentioned.

**8. A rig under a name of its own.** The per-score rig is done and the named
preset is a short step from it — the same document in a different place. High
perceived value for a small amount of work, and it is the thing that turns an
evening's amp tweaking into something reusable across songs.

**9. Translations are half-built already.** KF6 i18n is linked and the strings
are extractable; what is missing is plumbing and a destination. Point 5 supplies
the destination.

## Threats

**1. Hosting other people's code in your process means their bugs are your
segfaults.** This is not hypothetical — it is weakness 1, and it is the price
of the in-process lilv decision. That decision was right for latency and clock
reasons, and it permanently attaches the program's stability to the quality of
whatever LV2 plugins a user happens to have installed. Worth deciding now what
the program does when a plugin dies, because "the whole application crashes" is
currently the answer.

**2. Software centres are how normal people install things, and the app's core
feature resists the sandbox.** LV2 plugins live outside it. The wishlist
already flags this. The risk is that the flatpak question stays open long
enough that the answer becomes "there is no way for an ordinary user to get
this", which caps the audience at people who compile.

**3. Arobas can change the format.** Mitigated by the fact that no
specification ever existed, so the method — measure a corpus — is already the
right one, and by the corpus runner that would notice. Low severity, non-zero.

**4. The pace is the schedule, and the pace is not sustainable.** 22,654 lines
in four days is a remarkable sprint and a terrible baseline. The 1 September
date, the P4 scope and the marketing plan are all implicitly costed at this
rate. Every one of them should be re-costed at a rate a person can hold for a
year, because the failure mode for a bus-factor-one project is not a bad
release, it is silence after a good one.

**5. Perpetual requests for Windows and macOS.** Cheap to deflect once, tiring
to deflect a hundred times. Answering it plainly and high up on the website
costs nothing now and saves the same sentence being typed for years.

## If there are four days

1. Reproduce and record the LV2 crashes. Issue, CHANGELOG line, sanitiser run.
2. Fix the README's Rust sentence. One sentence, largest credibility return.
3. Push the eight commits.
4. Decide the release scope honestly: either 0.1.0 ships with LV2 chains marked
   experimental *and the known crash documented*, or the chains stay out of the
   release notes' headline and go in at 0.2.0 when they are stable.
5. Do not launch wide. The quiet-first-release argument in
   [marketing.md](marketing.md) is stronger with the crash log in hand than it
   was without it.

The project is in unusually good shape for its age, and the gap between what it
claims and what it does is small — three items, all of them fixable this week.
That gap is the only thing worth spending the next four days on, because
closing it is what makes every other claim in the documentation believable.
