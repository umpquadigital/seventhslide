# SeventhSlide: copyright and license

Copyright (C) 2026 the SeventhSlide authors

SeventhSlide is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version, with the additional permission below.

SeventhSlide is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
SeventhSlide, in the file LICENSE. If not, see <https://www.gnu.org/licenses/>.

SPDX-License-Identifier: GPL-3.0-or-later

Contributions to SeventhSlide are accepted under the same terms, including
the additional permission.

## Additional permission under GNU GPL version 3 section 7

If you modify SeventhSlide, or any covered work, by linking or combining it
with either of the following libraries (or a modified version of either
library):

- the NDI runtime library of Vizrt NDI AB (`libndi`, `Processing.NDI.Lib`),
- the DeckLink API library of Blackmagic Design's Desktop Video drivers
  (`libDeckLinkAPI`, `DeckLinkAPI.framework`, or the DeckLink components on
  Windows),

containing parts covered by the terms of those libraries' own licenses, the
licensors of SeventhSlide grant you additional permission to convey the
resulting work. Corresponding Source for a non-source form of such a
combination need not include the source code of those libraries.

### Scope of the permission

This permission is granted only by the copyright holders of SeventhSlide, and
only for SeventhSlide's own code. It does not change the license terms of
other software combined with SeventhSlide, such as Qt or FFmpeg. It grants no
rights to the NDI or DeckLink libraries themselves, which remain under their
own terms.

If you modify SeventhSlide, you may extend this permission to your version,
but you are not obligated to do so. You may also remove it from your copies.

### How SeventhSlide uses these libraries

SeventhSlide does not include either library. It loads them at run time from
wherever the user has installed them (see `src/render/ndi_runtime.cpp` and
`src/render/bmd_runtime.cpp`).

SeventhSlide is also built without any material from the NDI SDK. The parts
of the NDI interface that it calls are declared by hand in
`src/render/include/seventhslide/render/ndi_abi.hpp`.

NDI® is a registered trademark of Vizrt NDI AB. Blackmagic Design, DeckLink
and Desktop Video are trademarks of Blackmagic Design Pty. Ltd.

## Third-party software

### Included in this repository

These components are in `third_party/`. Each has its own license file or
notices.

| Component | License |
| --- | --- |
| Blackmagic DeckLink API headers and dispatch | Boost Software License 1.0 |
| Project Nayuki's QR Code generator | MIT |
| miniaudio | Public domain (Unlicense) or MIT-0 |
| The shipped fonts (`third_party/fonts`) | SIL Open Font License 1.1 |

### Downloaded during the build

| Component | License |
| --- | --- |
| nlohmann/json | MIT |
| pugixml | MIT |
| FTXUI | MIT |

### Libraries

SeventhSlide also uses the following libraries. The macOS disk image, the
Windows installer and the Ubuntu 24.04 package include their own copies of
Qt. The macOS disk image and the Windows installer also include FFmpeg, mpv
and OpenSSL, along with the libraries those depend on. The other packages
use the copies installed on the system.

| Component | License |
| --- | --- |
| Qt 6 | LGPL 3.0, except Qt HTTP Server, which is GPL 3.0. Qt WebEngine includes Chromium, which has its own notices. |
| FFmpeg | LGPL 2.1 or later, or GPL 2.0 or later as most distributions build it |
| mpv (libmpv) | LGPL 2.1 or later, or GPL 2.0 or later, depending on how it is built |
| OpenSSL 3 | Apache License 2.0 |

The source code of each of these components is available from its own
project.
