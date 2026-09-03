<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Distribution: where Fretwork is to be had

Decided 2026-09-02. The end goal is that a guitarist on any current Linux
desktop can install Fretwork without compiling anything, and that the person
looking for a TuxGuitar alternative finds it where that search lands. Every
channel below is measured against what the program actually needs -- current
Qt 6, KF6, PipeWire and a set of LV2 plugins -- rather than against what is
fashionable.

## The channels, in the order they are worth doing

| | Channel | What it buys | Cost | When |
|---|---|---|---|---|
| 1 | **GitHub Releases**, with a tarball and an AppImage | The file the website and everything else points at; runs on any distro | Two days for the AppImage, the same bundling Steam would need | First |
| 2 | **AUR** | The users most likely to file a useful bug, on the one distro whose repos already carry everything this needs | A day | Same week |
| 3 | **Flathub** | Software centres, which is how normal people install things | A day for the manifest, a day for the plugin story, one to two weeks of review | Same week |
| 4 | **openSUSE Build Service** | openSUSE, Fedora and Debian-family packages from one spec, with a repository URL | Half a day once the AUR package exists | Following week |
| 5 | **KDE Invent, then kdereview** | KDE's release service, translators and distro packagers, and every KDE distro after that | Slow, and a conversation rather than a build | Start in parallel; it is the slowest |
| 6 | **Fedora COPR** | Fedora specifically, where OBS is not enough | Half a day | On request |
| 7 | **nixpkgs** | A small, engaged audience | Usually done by a contributor once a release tarball exists | When somebody asks |
| 8 | **Debian and Ubuntu proper** | The largest audience, eventually | A sponsor and months; Ubuntu LTS ships Qt and KF6 older than this needs | After KDE acceptance, by KDE's packagers |
| 9 | **Steam**, Linux only and free | Rocksmith players, who are guitarists already there | $100 unrecoverable, tax and identity paperwork, a bundled build against a Debian 11 runtime, a rebuild per release | After the AppImage, if at all |
| — | **Snap** | Nothing Flathub does not | Native PipeWire is awkward in its sandbox, and the KDE runtime has bitten before | Not planned |

## The flatpak, and why it is no longer "hostile"

The wishlist said LV2 plugins live outside the sandbox and that a program
whose point is per-track chains is the worst candidate for one. The
sandbox now has a standard answer: the `org.freedesktop.LinuxAudio.Plugins`
extension point, which Ardour and Carla use. The app declares the mount,
sets `LV2_PATH` to it, and whatever plugin extension the user installs
appears in the Add effect menu. Checked against Flathub on 2026-09-02: the
x42 plugins are there as an extension; guitarix is there only as its VST3,
so the guitarix LV2 plugins -- most of the menu -- are built into the app or
submitted as an extension of their own. Both are GPL, so either is allowed.

What the manifest needs beyond that:

- `org.kde.Platform` 6.11 for Qt 6 and KF6, and the `rust-stable` SDK
  extension for the binary Guitar Pro reader.
- FluidSynth, lilv and its serd/sord/sratom stack as modules.
- A General MIDI soundfont bundled: FluidR3 is MIT and 142 MB, which
  Flathub accepts. The guitarix factory presets that feed the voicings menu
  live with the guitarix *application*, not its plugins, so they are bundled
  too or that menu is empty.
- The PipeWire socket, which the per-track ports, MIDI input and graph
  transport all need. Ordinary for an audio app.
- Screenshots and release entries in the metainfo, which already exists and
  already carries the GitHub-style app id Flathub wants.

## Steam, for the record

Allowed: the GPL is fine on Steam and a free Linux-only title is fine. What
it costs is the Steam Direct fee, which is only refunded after a thousand
dollars of revenue; identity, tax and bank paperwork even for a free title;
a source offer for every bundled third-party library; and a build against
the Steam Linux Runtime, a Debian 11 container with no Qt 6 or KF6 in it, so
everything is bundled -- an AppImage in all but name. Whether MIDI over
PipeWire reaches into that container is to be tested, not assumed. The
store's audience is gamers, and a free notation program is hard to find
there; the one real hook is Rocksmith players.

## Where people find it, which is a different question

The LinuxMusicians forum, the KVR Audio listing, the AlternativeTo and
Flathub pages for TuxGuitar, and r/linuxaudio. A guitarist searching for a
TuxGuitar alternative on Linux is the whole audience, and those are the
five places that search lands.

## The order, restated

A tagged release with a tarball first, because every channel above consumes
one. Then AUR, the AppImage and the Flathub manifest in the same week, since
the bundling decisions are shared. Then OBS. And the KDE Invent conversation
started alongside all of it, because it is the slowest and the highest
leverage.

---

## What actually happened, 3 September 2026

Written after doing it, and kept as a record of what each channel cost
against what this page guessed.

### 1. GitHub Releases, with an AppImage — **done**, as 0.4.1

Guessed at two days. It took an afternoon, and almost none of that was the
packaging: the AppImage was assembled in twenty minutes and then refused to
work for three separate reasons, each of which had to be found by running it.

**The tools fail quietly, and that is the whole difficulty.** An AppImage of
a KDE program is not a list of libraries — `ldd` gets those right first time.
It is a list of things loaded by name at runtime, which no scanner can see:

- **`org.kde.desktop`**, the QtQuick Controls style Kirigami picks on a
  desktop. Nothing imports it, so `qmlimportscanner` never finds it, and the
  window does not open at all.
- **`org.kde.sonnet`**, which that style imports for a spelling settings
  object. Found only once the first was fixed.
- **The Wayland client buffer integration.** Without it the program aborts on
  a Wayland session with `Available client buffer integrations: QList()`,
  while the same bundle offscreen or on X11 is perfectly happy. `linuxdeploy`
  accepted `EXTRA_QT_PLUGINS` and ignored it silently, so the directory is
  copied by hand.
- **The SVG icon *engine*.** Breeze is SVG throughout, and the SVG *image
  format* plugin is deployed automatically and is not the same thing. Without
  the engine every icon draws as nothing.

Two of those produced a working program that looked broken rather than a
broken one, which is the failure mode a bundle specialises in. The build
script therefore checks its own work before packaging: ten paths that must
exist, each with a note saying what its absence looks like to a user.

**It found two real bugs in the program**, both of which affect an ordinary
installation and neither of which anybody had noticed:

- Icons were blank on any desktop that is not Plasma, because these names are
  Breeze's and Qt was left holding its default theme of `hicolor`. Reproduced
  with `XDG_CURRENT_DESKTOP=GNOME`.
- The soundfont search looked at four fixed paths under `/usr/share`, which
  inside a bundle belongs to the host and not to us.

That is the argument for packaging early rather than last: **a bundle is the
first honest test of what a program assumes about the machine it is on.**

**What it costs the user: glibc 2.43.** Measured rather than guessed, by
reading the highest symbol version anything in the bundle asks for. It comes
from Ubuntu's Qt and the libraries under it, not from this program, and it
rules out every current long-term-support release. An AppImage built against
a distribution's own Qt inherits that distribution's floor, and the only
escape is building Qt too, which is a different project. **So the AppImage is
for current distributions and the flatpak is the portable one** — which
inverts this page's original assumption that the AppImage was the universal
answer and Flathub the convenience.

### 2. AUR — **written, not published**

`packaging/aur` has the PKGBUILD and a `.SRCINFO`. Neither has been through
`makepkg`, because there is no Arch machine here, and the AUR authenticates
with an SSH key that only the maintainer has. It is ready to push and has not
been pushed; the first `makepkg` on a real Arch box is the test it has not
had.

### 3. Flathub — **manifest written and built locally**

`packaging/flatpak` builds against `org.kde.Platform` 6.11. FluidSynth and the
lilv stack — lv2, zix, serd, sord, sratom, lilv — are not in the runtime and
are built as modules. The soundfont is carried, installed under a data
directory so that a better one in `~/.local/share` still wins. LV2 plugins
arrive through `org.freedesktop.LinuxAudio.Plugins`, so whatever the user
installs appears in the Add effect menu without this manifest naming any of
it.

Submission to Flathub is a pull request against their repository, from the
maintainer's account, and has not been made.
