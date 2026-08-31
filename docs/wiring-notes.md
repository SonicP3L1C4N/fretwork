<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# Wiring these in

> **Written 28 August 2026 and kept as written.** Everything in it has been
> done except one item, so read it as a record rather than as a list of things
> to do. The README's build instructions were fixed and are now checked on
> every push by the `readme` job it proposes; the metainfo is installed and
> validated by `ctest`; the CI notes describe the pipeline that exists.
>
> Two corrections it is worth having beside it. The Kirigami development
> package on this distribution is `libkirigami-dev`, not the
> `libkf6kirigami-dev` suggested below. And `type="development"` is still on
> the 0.1.0 release in the metainfo, which is the one recommendation here that
> has not been carried out.

Three small changes, and one bug the CI would have found on its first run.

---

## 1. The README's build instructions do not work

This is not a nitpick. `CMakeLists.txt` requires:

- `Qt6 ... COMPONENTS Core Gui Widgets Xml Qml Quick QuickControls2 Test`
- `KF6 ... COMPONENTS Archive CoreAddons I18n Kirigami`
- `PkgConfig`

The README's apt line installs `qt6-base-dev` and three KF6 libraries. It does
**not** install Qt Declarative or Kirigami, and does not install pkg-config.
A stranger following the README on a clean machine gets a CMake error at
configure time, before anything is built. By the `release-triage` severity
scale that is S1 — a first run that fails with no explanation a stranger can
act on.

Replacement for the block under `## Building`:

```
sudo apt install build-essential cmake ninja-build pkg-config \
    extra-cmake-modules qt6-base-dev qt6-declarative-dev \
    libkf6archive-dev libkf6coreaddons-dev libkf6i18n-dev \
    libkf6kirigami-dev \
    zlib1g-dev libfluidsynth-dev fluid-soundfont-gm

# Optional. Without lilv there are no per-track LV2 chains; without
# PipeWire's headers there is no transport.
sudo apt install liblilv-dev libpipewire-0.3-dev
```

The `readme` job in `ci.yml` reads that block out of `README.md` and runs it,
so it cannot drift again without the build going red.

---

## 2. Install the metainfo

In `CMakeLists.txt`, beside the icon rules:

```cmake
install(FILES src/io.github.sonicp3l1c4n.fretwork.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
```

If the `.desktop` file is currently installed from `src/CMakeLists.txt`, put
the metainfo next to it instead and drop the `src/` prefix. Either is fine —
what matters is that it lands in `${KDE_INSTALL_METAINFODIR}`, because a
software centre will not list the program without it.

Optional but worth it, so a broken metainfo file fails `ctest` rather than
waiting for CI:

```cmake
find_program(APPSTREAMCLI appstreamcli)
if(APPSTREAMCLI AND BUILD_TESTING)
    add_test(NAME metainfo
             COMMAND ${APPSTREAMCLI} validate --pedantic --no-net
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/io.github.sonicp3l1c4n.fretwork.metainfo.xml)
endif()
```

`--no-net` matters: the screenshots are remote URLs and you do not want the
test suite reaching out to GitHub.

---

## 3. Things to change in the metainfo before you tag

- **The release date.** It says `2026-09-01`. Make it the day you tag.
- **The version.** It says `0.1.0`, matching `project(fretwork VERSION 0.1.0)`.
  Keep those two and the git tag in step — nothing enforces it automatically.
- **`type="development"`.** Drop that attribute when it stops being a
  candidate.
- **The screenshot URLs** point at `main`. Once you have tagged, consider
  pointing them at the tag so a stale screenshot cannot appear against a
  released version.

`docs/page-direction.png` is a page-layout diagram rather than a window shot.
If it reads as documentation rather than as the program, cut that screenshot —
three good ones beat four mixed ones in a software centre carousel.

---

## 4. Notes on the CI

**Why Arch for the main build.** The project needs Qt 6.6 and KF 6.0
minimum. Ubuntu LTS does not carry Qt 6.6, so a plain `ubuntu-latest` runner
cannot configure the project at all. Arch always has current Qt6 and KF6 and
installs the whole dependency set in one `pacman` line.

**Why KDE Neon for the README job.** It is Ubuntu-based, so the apt package
names in the README are the right names, and it carries a current Qt6 and KF6
so the build fails for real reasons rather than for being too old. Confirm the
image tag resolves on the first run — if the Neon registry path has moved,
that job will fail to start and the others will still be useful.

**The dependency matrix.** `no-lilv` and `no-pipewire` hide the `.pc` file
rather than uninstalling the package, because FluidSynth pulls some of it in
transitively and the question being asked is what `pkg_check_modules` does
when it cannot find the module. If a variant fails, that is the finding — the
CMake fallback is not there.

**Audio in CI.** `QT_QPA_PLATFORM=offscreen` is set. If any suite tries to
open an audio device and hangs on a runner, that is a test that needs to
assert on a buffer rather than on hardware — and it is exactly the gap the
`regression` agent is meant to close for `renderer` and `wav`.

**The corpus.** `corpus.yml` will not run until you register a self-hosted
runner labelled `corpus` with `FRETWORK_CORPUS` in its `.env`. Until then it
sits idle, which is honest. It also fails the build if a `.gp` ever appears in
the working tree.

One thing to decide: Actions logs on a public repository are public, and a
corpus test failure names files. If your transcription filenames are song
titles you would rather not publish, run `corpus.yml` on
`workflow_dispatch` only and drop the `push` and `schedule` triggers.
