<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Marketing and website

Brainstorming, not a plan. Nothing here is committed to; the point is to have
the arguments written down where they can be disagreed with, the way
[wishlist.md](wishlist.md) does for features. Where something is a real choice
it is written as a choice, with what it costs. Open questions are collected at
the bottom.

The release in `io.github.sonicp3l1c4n.fretwork.metainfo.xml` is dated
**2026-09-01**, which is what everything below is timed against.

## What is actually being sold

Nothing. It is GPL and there is no money in it, so "marketing" here means one
thing: **a guitarist on Linux who wants stems has to be able to find out this
exists.** Every decision below is judged against that and not against reach.

The person is specific. They have a `.gp` file of a song they are learning, or
one they wrote. They want the bass on its own, or the mix without the part they
are playing, or their own amplifier on the guitar track. Today they open
TuxGuitar, export a MIDI file, import it into a DAW, and rebuild the
instruments by hand. That workflow is the thing being replaced, and it is worth
naming in copy because everyone who has done it recognises the description.

## The pitch, at four lengths

Written out at each length that a channel actually asks for, so they stay in
step with each other rather than being reinvented per post.

**Six words** (the AppStream summary, already shipped):
> Play every track of a tab as its own stem.

**A sentence** (repo description, Mastodon bio, directory listings):
> A tablature program for Linux that gives every track its own synthesiser, its
> own amplifier and its own audio file.

**A paragraph** (forum posts, the site's hero):
> Fretwork reads Guitar Pro files and treats the score as a multitrack session.
> Every track gets a synthesiser of its own, so solo and mute take effect while
> it plays — no re-render, no bounce. Every track can go through its own LV2
> amplifier chain, and comes out as an independent WAV, wet or dry. It edits,
> it prints, it tunes your guitar to the score's own tuning, and it exposes
> every part as a pair of PipeWire ports so a DAW can record them live.

**The long version** is the README, and it should stay the long version. It is
unusually good at what it does and rewriting it shorter for a website would
lose the reasoning, which is the part people quote.

### Taglines to argue about

| | Why it might be right | Why it might not |
|---|---|---|
| **Play every track as its own stem** | Already the AppStream summary; concrete; the actual differentiator | Says nothing about tab, so it reads as a DAW to anyone who doesn't already know |
| **Tablature, as a multitrack session** | Both halves of the idea in four words | "Multitrack session" is jargon to a hobbyist guitarist |
| **One synth per track** | The technical claim everything else follows from | Only means something to someone who knows why that is hard |
| **The tab is the session** | Shortest; a slogan rather than a description | Slogans age badly and this one is close to meaningless |
| **Solo the bass. Reamp the guitar. Print the page.** | Three verbs, three audiences, one line | Long for a header; better as a subtitle under the name |

Recommendation: keep the AppStream summary as the single tagline everywhere, and
use the three-verb line as the hero subtitle. One name, one line, repeated —
consistency is worth more than the best possible phrasing.

## Who it is for, in the order they should be reached

1. **Guitarists on Linux who already keep a DAW open.** The smallest audience
   and the one the program is literally built for. They will understand
   "stems", "reamp" and "LV2" without a gloss. Reachable: `r/linuxaudio`,
   `linuxmusicians.com`, the Ardour and Reaper Linux communities, guitarix's
   own users.
2. **Linux desktop users who play guitar.** Much larger, much vaguer. They want
   "opens my tabs and sounds better than TuxGuitar", and the stem story is a
   bonus they discover later. Reachable: `r/linux`, `r/kde`, KDE's own
   channels, Phoronix, software centres.
3. **Guitarists who don't run Linux.** Not an audience. Do not chase them; the
   honest answer is "there is no build for you" and a stream of that reply is a
   drain on a one-person project. The site should say Linux plainly, high up,
   so nobody arrives disappointed.
4. **Developers.** Not customers, but the README and architecture doc are
   genuinely the most shareable things the project has, and a Rust-behind-a-C-ABI
   importer with an in-process LV2 host is the kind of thing Lobsters and
   Hacker News read to the end. This is the channel most likely to
   over-perform, and it converts to contributors rather than users.

## What makes it different, ranked by how convincing it is

Ranked by how hard the claim is to dismiss, which is not the same as how hard
it was to build.

1. **One synth per track, live.** Solo and mute while it plays, no re-render.
   Everything else in the list is downstream of this and it is the one sentence
   that no other tablature program on Linux can say.
2. **Real stems out.** One WAV per track, plus `--dry` for reamping, plus a
   click that is deliberately kept out of the mix. This is the thing people
   currently do by hand.
3. **Per-track LV2 chains with the knobs drawn from the plugin.** Over a
   hundred usable plugins with guitarix installed, and voicings named after the
   records they aim at.
4. **A pair of PipeWire ports per part, and it drives the transport.** The
   Reaper detail — that Reaper on Linux can follow a transport and cannot set
   one, so Fretwork has to be the one that presses play — is the most
   convincing single paragraph in the README for the DAW audience, because it
   proves someone actually tried it.
5. **A tuner that knows the score's tuning.** Small, immediately understandable
   by every guitarist regardless of technical depth, and the easiest thing on
   this list to demonstrate in eight seconds. Undersold; probably the best
   *hook* even though it is fifth on merit.
6. **It says what it cannot do.** Alternate endings, harmonics, tremolo
   picking — marked rather than approximated. Sceptical readers weigh this
   heavily and it costs nothing to state.

### What not to claim

- Not "a Guitar Pro replacement". It does not write `.gp`, does not do standard
  notation, does not read GP3–GP6. Say what it does.
- Not "better than TuxGuitar". The README's own framing — TuxGuitar is not
  going to be beaten at being TuxGuitar — is both truer and more disarming, and
  it pre-empts the first reply in every thread.
- Not "professional" or "studio-quality". The SFZ sampler interpolates in a
  straight line and says so. Keep that habit in the copy.
- Nothing that implies affiliation with Arobas Music. See the legal note below,
  which is not optional.

## The website

### Should there be one at all

A GitHub repo with that README is already a decent landing page, and the
metainfo means a software centre will show it. So a site has to earn itself.
It does, for exactly one reason: **the README cannot play audio**, and the
strongest demonstration this project has is a sound.

### The one thing the site must do

A **stem player in the browser.** One short passage from a real score,
rendered by Fretwork itself, presented as the mixer: four faders, S and M on
each, one play button. Let someone mute the guitar and hear the backing track
appear. That is the entire pitch, delivered in ten seconds, with no install and
no explanation — and it is honest, because those files came out of `--render`.

Then a second pair below it: the same eight bars dry, and through the guitarix
amp with a named voicing. Same passage, two files, one toggle. That is the P4
argument made audibly instead of in prose.

Everything else on the site is optional. If only the hero and the stem player
get built, the site has done its job.

Cost: the audio has to be a score that can be published, which the test corpus
cannot — transcriptions are not ours to redistribute, and neither are the
songs. This needs an **original riff, written for the purpose**, in a `.fw`
that ships with the site. Eight bars, four parts. That is a real piece of work
and it is on the critical path for the site, so it is worth starting before
anything else.

### Structure, if it is a single page

1. **Hero** — name, icon, `Play every track of a tab as its own stem`, the
   three-verb subtitle, the window screenshot, and one button: *Build it* →
   the README's build section. No "Download" button while there is no package;
   a download button that leads to `cmake -B build` is a small lie.
2. **The stem player.** Above the fold on a laptop if it can be.
3. **What it does**, as five or six screenshot-plus-paragraph blocks: the
   window, the repeats warning, the tuner, the page, the effects deck. The
   screenshots already exist in `docs/` and are good.
4. **What it does not do yet**, verbatim in tone from the README. Placing this
   *above* the install instructions rather than in a FAQ at the bottom is the
   whole personality of the project in one layout decision.
5. **Install**, honest about the state: build from source; KDE Neon / Arch /
   Fedora status as it becomes true.
6. **The reasoning** — links to architecture.md, gpif-format.md, wishlist.md.
   For the developer audience this is the payload, not an appendix.
7. Footer: licence, the Arobas trademark note, no affiliation.

### Where it lives

- **GitHub Pages on the repo** — free, no domain, no hosting decision, and the
  URL says who wrote it. `sonicp3l1c4n.github.io/fretwork`. Recommended for the
  0.1.0 launch: it is the option that can exist by 1 September.
- **A domain** — `fretwork.app` or similar, if one is free. Better long term,
  and it is the only option that survives a move off GitHub. Can be pointed at
  the same Pages site later, so this is not a decision that has to be made now.
- Note the SEO problem before buying anything: *fretwork* is an ordinary
  English word for decorative woodwork and there are furniture makers, joiners
  and at least one band using it. Searching the name alone will not find this
  for a long time, if ever. Practical consequence: **always pair the name** —
  "Fretwork tablature", "Fretwork Linux", "Fretwork stems" — in titles, the
  `<title>` tag, and post headlines. The name is good and worth keeping; it
  just cannot carry search on its own.

### How it should look

This is the easy part, because the look already exists and is unusually
distinct. `src/gui/Ink.qml` and `docs/design_handoff_ink_chrome_ui/README.md`
have the whole system: ink `#201e1d`, paper `#f3f2f2`, one magenta `#d6006c`
taken from the fret marker on the icon, with `#aa0b56` for magenta text on
paper and `#ff90b1` for magenta on ink.

The site should be that palette exactly, so a screenshot sits on the page
without a seam — dark chrome at the edges, paper in the middle, magenta used
once per screenful and never twice. Source Serif 4 is already the chrome
typeface in the design handoff and is SIL OFL, so it can be used on the site
without a licensing question.

One deliberate difference from most project sites: **do not theme it to the
visitor's dark mode.** The application makes exactly this choice, and for the
same stated reason — the thing in the middle is a document. A site that matches
the program's own argument about itself is more convincing than one that
follows the convention.

## Assets to make

Roughly in the order they pay off.

- [ ] **The demo score.** Original, eight bars, four parts, `.fw` in the repo.
      Blocks the stem player, the audio demo and any video. Start here.
- [ ] **Rendered stems from it**, as OGG (small) with WAV alongside for anyone
      who wants to check the claim.
- [ ] **A dry/wet pair** of the guitar part through a named guitarix voicing.
- [ ] **A screen recording, 45–60 seconds, no voice.** Open a file, press play
      with the bar grid following, solo the bass mid-playback, open the effects
      deck and turn a knob while it plays, render stems. Captions rather than
      narration: it will be watched muted on a forum and with sound on a site,
      and it must work both ways.
- [ ] **An eight-second tuner clip** — pluck a string, needle moves, string
      lights up. This is the one that gets reposted.
- [ ] **Screenshots** — already done and already good. The existing four cover
      window, repeats, tuner and page. Add the effects deck and the mixer when
      P4 settles.
- [ ] **A social card** — icon on ink, name, the one-line summary. Needed for
      link previews on Mastodon and Reddit, where the card is most of what
      anyone sees.

## Channels, and what to say in each

One-person project, so this is a launch *sequence*, not a campaign — and each
post is a place a conversation happens that has to be answered.

**Before anything:** be ready for the first three replies, which are always
the same. *Does it read GP5?* (No — GP8's `.gp` only, the old binary formats
are in the wishlist.) *Windows/Mac?* (No.) *How is it different from
TuxGuitar?* (One synth per track, stems, LV2.)

- **`linuxmusicians.com`** — first, and days before anything else. The most
  knowledgeable and least hostile audience, and their feedback improves the
  post used everywhere afterwards. Lead with stems and per-track LV2.
- **`r/linuxaudio`** — same content, wider reach.
- **KDE channels** — Matrix rooms, and *KDE's This Week/Month in KDE* if it
  will take a third-party Qt/KF6 app. The project already follows KDE's
  conventions and licence, which makes this an easier ask than usual.
- **Mastodon** — `#LinuxAudio`, `#KDE`, `#Guitar`. Video-first; the tuner clip.
- **`r/kde` and `r/linux`** — screenshot-first. Expect the audience to be
  broader and the questions less technical.
- **Hacker News / Lobsters** — but not on the same story as the others. The
  interesting submission for them is not the app, it is *the file format*:
  gpif-format.md, measured against a real corpus with no vendor specification.
  Submit that, and let the app be what people find at the end of it.
- **Guitar forums** — plausible later, and only where Linux is not exotic.
  Lower priority than everything above.
- **Phoronix / OMG!Ubuntu / LWN** — they find things; they are not pitched.
  A good `r/linux` thread is the actual mechanism.

Sequence: linuxmusicians → fix what they find → r/linuxaudio + Mastodon → KDE →
r/linux + r/kde → the format post to Lobsters/HN a week or two later, once the
first wave of bug reports has been answered.

## Timing

0.1.0 is dated 1 September. The temptation is to launch everything on the tag,
and the argument against it is that P4 is *begun* rather than done — the
per-track LV2 chain and SFZ sampling are the most compelling part of the pitch
and are the least finished part of the program.

Two options:

- **Launch on 0.1.0**, positioned as what it is: a tablature editor and player
  with stem export, with amplifier chains marked experimental. Honest, and the
  metainfo already says exactly this. Gets feedback while the design is still
  moving, which is when feedback is worth most.
- **Wait for P4** and launch on the full story — one synth, one amp, one stem —
  which is the version of the pitch that lands hardest.

Recommendation: launch on 0.1.0, but only to `linuxmusicians` and Mastodon.
Save the wide posts — r/linux, HN, KDE's newsletter — for the release where the
amplifier story is finished. A launch spends attention once; spending it on the
weaker version of the pitch is the expensive mistake, and a quiet first release
to people who will actually build from source costs nothing.

## Names and law, in the copy

Every one of these belongs in the site footer as well as the README, because
the site is the thing that will be linked without context.

- "Guitar Pro" is a trademark of Arobas Music. Not affiliated, not derived,
  ships none of their soundbanks or artwork.
- The interoperability point (EU Software Directive 2009/24/EC, Article 6) is
  worth stating plainly once — it is the reason the project can exist, and
  people ask.
- Never phrase a headline as "Guitar Pro for Linux". It is the most tempting
  and most legally careless line available.
- No transcriptions, ever, anywhere — not on the site, not in a screenshot
  where the whole page is legible, not in the demo audio. The existing
  screenshots show real songs, which is fine as a screenshot of a program in
  use and would not be fine as a downloadable file.

## Measuring it

Deliberately little. GitHub stars are not users and the site should carry no
analytics, tracking or fonts loaded from anyone else's server — an audience
that cares about LV2 plugins and PipeWire routing notices, and it would
undercut the project's own stance about the software it uses.

The signals worth watching are all qualitative: issues filed by people who
built it, `.fw` files attached to bug reports, packaging requests from distro
maintainers, and anyone showing up with a score Fretwork gets wrong. One good
bug report from a guitarist is worth more than a thousand stars, and is the
only evidence that the audience above is real.

## Open questions

- Is there an original riff to use as the demo score, or does one need writing?
  This blocks the best asset the site can have.
- GitHub Pages for 0.1.0, or a domain first?
- Launch quietly on 0.1.0 and loudly on P4, or once, later, at full strength?
- Is a screen recording something worth the time, or is the stem player enough?
- Should the site carry the README's full voice, or a shorter register? The
  README's reasoning is the most distinctive thing the project has written,
  and cutting it for a landing page might be cutting the actual asset.
