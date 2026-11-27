<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo-dark.svg">
    <img src="docs/images/logo.svg" alt="SeventhSlide logo" width="140">
  </picture>
</p>

<h1 align="center">SeventhSlide</h1>

<p align="center">
  Free and open-source presentation software for churches, schools, events
  and other organizations.
</p>

SeventhSlide shows songs, Bible passages, slides, images, video and web pages
on any number of outputs at once.

![The SeventhSlide main window, with a service, the song library, the slides
of the current song and three outputs](docs/images/main-window.png)

## Features

- **Songs and Bibles:** Lyrics and Bible passages are laid out by themes. The
  KJV, BSB and ASV are included, and Bibles in Zefania, OpenSong and OSIS
  formats can be imported.
- **Cue control:** Cues split a slide into lines that you step through one at
  a time. Each screen chooses how much to show around the current line: the
  whole slide, a couple of lines at a time, or the current line with the next
  one.
- **Freeform slides:** A slide editor for everything else. Items and whole
  services can be exported as files and imported into another library.
- **Media:** Images, video and audio, with an audio mixer. Webcams and
  capture cards can be shown from the Live tab.
- **Outputs:** Each output can be shown on as many destinations as you like:
  full-screen displays, desktop windows, virtual cameras, NDI, Blackmagic
  DeckLink and SRT streams.
- **Web remote:** Run a presentation from a phone on the same network.
- **Sync:** Keep libraries in sync across computers, either directly or
  through a server (`seventhslide --server`; see
  [docs/SERVER.md](docs/SERVER.md)).

## Download

Download the latest version from this repository's **Releases** page:

| File | For |
| --- | --- |
| `seventhslide-ubuntu-24.04-amd64.deb` | Ubuntu 24.04, Linux Mint 22 |
| `seventhslide-ubuntu-26.04-amd64.deb` | Ubuntu 26.04 |
| `seventhslide-fedora-x86_64.rpm` | Fedora |
| `SeventhSlide-macOS-arm64.dmg` | macOS 26 or later, Apple silicon |
| `SeventhSlide-Windows-x64-Setup.exe` | Windows 10 (1903) or later |

The `SHA2-256SUMS` file published with them lists each file's checksum. To
update, install the newer package over the old one.

The packages are not code-signed yet:

- **macOS:** The first time you open the app, Control-click it, choose
  **Open**, then choose **Open** again.
- **Windows:** The installer is shown as coming from an unknown publisher.
  Web pages do not display on Windows yet.

### NDI and Blackmagic

SeventhSlide does not include NDI or Blackmagic software. To use those
outputs, install the software yourself:

- **NDI:** NDI Tools or the NDI SDK, from ndi.video.
- **Blackmagic:** Desktop Video, from blackmagicdesign.com (Linux and macOS).

SeventhSlide detects them once they are installed, without a restart.

## Building from source

You need CMake 3.21 or later, a C++20 compiler, Qt 6.8 or later, libmpv and
FFmpeg. With those installed, run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/seventhslide
```

[docs/BUILDING.md](docs/BUILDING.md) lists the packages to install on each
system and explains how to run the tests and how releases are made.

## License

SeventhSlide is free software, licensed under the GNU General Public License,
version 3 or later ([LICENSE](LICENSE)), with an additional permission for
use with the NDI and Blackmagic DeckLink libraries. [NOTICE.md](NOTICE.md)
contains that permission and the licenses of the other software SeventhSlide
uses.

NDI® is a registered trademark of Vizrt NDI AB. Blackmagic Design, DeckLink
and Desktop Video are trademarks of Blackmagic Design Pty. Ltd.
