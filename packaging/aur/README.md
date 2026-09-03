<!--
SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
-->

# The Arch package

`PKGBUILD` and `.SRCINFO` for the AUR, kept here in the source tree so that
the version and the dependency list are changed in the same commit as the
thing they describe. The AUR itself is a separate git repository; this is the
copy that gets pushed to it.

## What it has not been through

**It has not been built.** Neither `makepkg` nor an Arch machine exists on the
computer this was written on, so the dependency names below are taken from
Arch's own package names and the build steps from what CMake is actually
invoked with here -- but the first `makepkg` on a real Arch box is the test
this has not had, and a missing `makedepends` is the likeliest thing it will
find.

`.SRCINFO` was written by hand from the `PKGBUILD` rather than by
`makepkg --printsrcinfo`, for the same reason. **Regenerate it properly**
before the first push:

```
makepkg --printsrcinfo > .SRCINFO
```

## Publishing it

The AUR authenticates with an SSH key registered to the maintainer's account,
so this cannot be done from anywhere but the maintainer's own machine.

```
git clone ssh://aur@aur.archlinux.org/fretwork.git aur-fretwork
cp PKGBUILD aur-fretwork/
cd aur-fretwork
makepkg --printsrcinfo > .SRCINFO       # not the hand-written copy
makepkg -si                             # build it, and install it, before pushing
git add PKGBUILD .SRCINFO
git commit -m 'Initial import: fretwork 0.4.1'
git push
```

## On each release

Bump `pkgver`, reset `pkgrel` to 1, and replace `sha256sums` with the checksum
of the new tag's tarball:

```
curl -sL https://github.com/SonicP3L1C4N/fretwork/archive/refs/tags/vX.Y.Z.tar.gz | sha256sum
```
