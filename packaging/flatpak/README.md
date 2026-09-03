<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# The flatpak

`io.github.sonicp3l1c4n.fretwork.yml` builds Fretwork against
`org.kde.Platform` 6.11.

**This is the portable one.** The AppImage borrows the host's glibc and so
inherits the floor of whatever distribution built it — 2.43, as things stand,
which rules out every current long-term-support release. A flatpak carries its
runtime, so it runs anywhere flatpak does. That is the opposite of what
[../../docs/distribution.md](../../docs/distribution.md) originally assumed,
and it is the reason this matters more than the AppImage does.

## What is in it, and why

- **FluidSynth and the lilv stack are built here**, because the KDE runtime
  has neither. lilv brings lv2, zix, serd, sord and sratom with it, and their
  versions are pinned against each other rather than taken as latest: lilv
  0.26.4 refuses a sord older than 0.16.20, which is how the first build
  failed.
- **A General MIDI soundfont is carried.** FluidR3 is MIT, which is what makes
  redistributing it allowed. It is installed under a data directory rather
  than named outright, so a better bank in `~/.local/share` is found first —
  which the program's own search was changed to make possible in 0.4.1.
- **LV2 plugins are not carried.** They arrive through
  `org.freedesktop.LinuxAudio.Plugins`, the extension point Ardour and Carla
  use, so whatever the user installs appears in the Add effect menu without
  this manifest naming any of it. Several hundred megabytes of amplifier
  simulation bundled here would be several hundred megabytes that never got
  updated.
- **The PipeWire socket is asked for by name**, not just the pulse one. The
  per-track ports and the graph transport are PipeWire's, and `--socket=pulseaudio`
  alone does not reach them.
- **`--device=all`, for MIDI.** Keyboards and control surfaces are USB
  devices and there is no narrower permission that reaches them.

## Building it here

```
flatpak install --user flathub org.kde.Sdk//6.11
flatpak-builder --user --force-clean --install \
    build packaging/flatpak/io.github.sonicp3l1c4n.fretwork.yml
flatpak run io.github.sonicp3l1c4n.fretwork
```

## What it has not been through

**It has not been submitted to Flathub.** That is a pull request against
`flathub/flathub` from the maintainer's own account, followed by a review, and
neither has happened. Before submitting:

- The app module should point at the release tarball for the version being
  submitted, with that tarball's checksum. It does.
- Flathub wants the manifest named after the application id, at the root of
  the submitted repository.
- `flatpak run --command=fretwork` with a real `.gp` file, and a rendered
  stem, are worth doing inside the sandbox rather than trusting that the
  permissions above are the right set.

## What is not in it yet

**The Rust reader.** `org.freedesktop.Sdk.Extension.rust-stable` would add it,
and the crate has no dependencies at all so `cargo build --offline --locked`
needs no vendoring. Without it a file from an older Guitar Pro is refused
without being named, which is a smaller loss than an untested SDK extension in
a first submission.
