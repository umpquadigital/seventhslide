# Building, testing and releasing SeventhSlide

This guide explains how to build SeventhSlide from source, run its tests, and
make a release. For how the code is organized and the rules it follows, see
`docs/CODING.md`.

## Requirements

These are the requirements for building on Linux. For macOS, see
[macOS](#macos). Package names are given as Fedora / Debian and Ubuntu.

| Dependency     | Version              | Notes                                      |
| -------------- | -------------------- | ------------------------------------------ |
| CMake          | 3.21 or later        |                                            |
| C++20 compiler | GCC 12+ or Clang 15+ |                                            |
| Qt             | 6.8 or later         | Development packages for the modules below |
| libmpv         |                      | `mpv-devel` / `libmpv-dev`                 |
| FFmpeg         | 5.1 or later         | Development files for the libraries below  |
| OpenSSL        | 3.0 or later         | `openssl-devel` / `libssl-dev`             |

All of these are required. The Qt modules you need are Core, Gui, Qml, Quick,
QuickControls2, QuickLayouts, QuickDialogs2, Sql, Network, HttpServer,
WebSockets and Svg. Qt Sql is included in `qt6-qtbase-devel` /
`qt6-base-dev`. The FFmpeg libraries you need are libavformat, libavcodec,
libavutil, libswresample, libswscale and libavdevice.

Qt 6.8 is a hard minimum: `src/remote` uses `QHttpHeaders` and
`QHttpServerWebSocketUpgradeResponse`, which were both introduced in Qt 6.8.

The following dependencies are optional:

- PipeWire 0.3.49 or later, development files (`pipewire-devel` /
  `libpipewire-0.3-dev`). Linux only. Without PipeWire, a virtual camera
  sends pictures only, and there is no microphone to carry its sound.
- Qt WebEngine (with Qt WebChannel and Qt Positioning) and Qt's private Gui
  headers (`Qt6::GuiPrivate`). These are large. Without them, web pages are
  still kept and can be added to services, but the outputs show nothing for
  them. To leave Qt WebEngine out of a build even though it is installed, pass
  `-DSEVENTHSLIDE_WEBENGINE=OFF`. To make the configure step fail when it is
  not installed, pass `-DSEVENTHSLIDE_REQUIRE_WEBENGINE=ON`.
- The Wayland client and protocol files (`wayland-devel
  wayland-protocols-devel` / `libwayland-dev wayland-protocols`) and XFixes
  (`libXfixes-devel` / `libxfixes-dev`). Linux only. With them, a web page
  that you click into in the Controller holds the pointer: the Wayland files
  provide this under Wayland, and XFixes under X11. Both need Qt's private
  Gui headers. aqt's Qt includes them; with a distribution's Qt, install
  `qt6-qtbase-private-devel` / `qt6-base-private-dev`.

The first time you configure the build, CMake downloads the remaining
dependencies with `FetchContent`, so you need network access. These are
Catch2 (for the tests), nlohmann/json and pugixml (used by core), and FTXUI
(used by the server's settings screen). Their versions are pinned in
`cmake/SeventhSlideDependencies.cmake`.

## Installing the dependencies

### Fedora

Install the dependencies with `dnf`:

```bash
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtshadertools-devel \
  qt6-qthttpserver-devel qt6-qtwebsockets-devel qt6-qtsvg-devel \
  mpv-devel ffmpeg-free-devel liberation-sans-fonts \
  pipewire-devel openssl-devel qt6-qtwebengine-devel \
  wayland-devel wayland-protocols-devel libXfixes-devel qt6-qtbase-private-devel
```

You can use RPM Fusion's `ffmpeg-devel` instead of `ffmpeg-free-devel`. It
also works, and it decodes more formats.

### Ubuntu 24.04 / Mint 22

These releases ship Qt 6.4.2, which cannot build `src/remote` (see the Qt 6.8
minimum above). Install the other dependencies with apt, and install Qt
itself with `aqtinstall`:

```bash
sudo apt-get install cmake ninja-build g++ pkg-config \
  libmpv-dev libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
  libswscale-dev libavdevice-dev libssl-dev \
  libgl1-mesa-dev libegl1 libxkbcommon-dev libxkbcommon-x11-0 \
  fonts-liberation libpipewire-0.3-dev \
  libwayland-dev wayland-protocols libxfixes-dev

python3 -m venv /tmp/aqtenv && /tmp/aqtenv/bin/pip install aqtinstall
/tmp/aqtenv/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
  -m qthttpserver qtwebsockets qtshadertools \
     qtwebengine qtwebchannel qtpositioning -O "$HOME/Qt"
```

Then configure the build against that Qt:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64"
```

### Ubuntu 26.04

Ubuntu 26.04's own Qt is recent enough. The `qml6-module-*` packages provide
the QML modules that the app imports, which the QML compiler needs:

```bash
sudo apt-get install cmake ninja-build g++ pkg-config \
  qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-httpserver-dev \
  qt6-websockets-dev qt6-svg-dev qt6-shadertools-dev qt6-webengine-dev \
  qt6-webchannel-dev qt6-positioning-dev \
  qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-dialogs \
  qml6-module-qtquick-effects qml6-module-qtquick-layouts qml6-module-qtquick-shapes \
  qml6-module-qtquick-templates qml6-module-qtquick-window \
  qml6-module-qt-labs-folderlistmodel qml6-module-qtwebchannel qml6-module-qtwebengine \
  libmpv-dev libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
  libswscale-dev libavdevice-dev libssl-dev libpipewire-0.3-dev \
  libgl-dev libegl-dev libxkbcommon-dev fonts-liberation \
  libwayland-dev wayland-protocols libxfixes-dev
```

## Building

Configure, build, run the tests, and start the application:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/bin/seventhslide
```

The application needs a display to run. For a quick smoke test without one,
set `QT_QPA_PLATFORM=offscreen`.

### Build targets

A build produces these targets:

| Target                           | Description                                                              |
| -------------------------------- | ------------------------------------------------------------------------ |
| `seventhslide`                   | The application (`SeventhSlide.app` on macOS)                            |
| `seventhslide_server_console`    | Windows only: `seventhslide-server.exe`, the server as a console program |
| `seventhslide_tests_*`           | The Catch2 tests, one program per layer (see [Tests](#tests))            |
| `seventhslide_tests_dev`         | Benchmarks, screenshots and the deck check; built only on request        |
| `seventhslide_bench_sync_memory` | The sync memory and timing benchmarks                                    |
| `macos_app`                      | macOS only: an app bundle and disk image for another Mac (see [macOS](#macos)) |

### CMake options

These are the options you are most likely to need:

| Option                            | Default | Description                                            |
| --------------------------------- | ------- | ------------------------------------------------------ |
| `SEVENTHSLIDE_BUILD_APP`          | `ON`    | Build the Qt application                               |
| `SEVENTHSLIDE_BUILD_TESTS`        | `ON`    | Build the Catch2 test programs                         |
| `SEVENTHSLIDE_BUILD_TOOLS`        | `OFF`   | Build the developer CLIs in `src/tools`: `seventhslide_layout_dump`, `seventhslide_render_smoke` and `seventhslide_render_song` |
| `SEVENTHSLIDE_WARNINGS_AS_ERRORS` | `OFF`   | Treat warnings as errors. The `-ci` presets turn this on |
| `SEVENTHSLIDE_WEBENGINE`          | `ON`    | Use Qt WebEngine where it is installed (see [Requirements](#requirements)) |
| `SEVENTHSLIDE_REQUIRE_WEBENGINE`  | `OFF`   | Fail the configure step if Qt WebEngine is not available |
| `SEVENTHSLIDE_NDI_VERIFY_ABI`     | `OFF`   | Build the NDI ABI check (see [NDI](#ndi))              |

### Platforms and presets

The CMake build supports Linux, Windows and macOS. Day-to-day development
happens on Linux first; for macOS, see [macOS](#macos). Windows is built only
by the release workflow (see [CI](#ci)).

`CMakePresets.json` defines these presets:

- `linux`, `macos`, `windows-msvc` and `windows-mingw`: Debug builds in
  `build`. Each has a configure, a build and a test preset.
- `linux-ci` and `macos-ci`: Release builds with warnings treated as errors,
  as CI builds them. `linux-ci` has build and test presets. `macos-ci` is a
  configure preset only, so build with `cmake --build build` and test with
  `ctest --test-dir build`.
- `linux-asan`: the core, media and sync tests under AddressSanitizer and
  UBSan, built in a separate directory, `build-asan`.
- `ubuntu-24.04-deb`, `ubuntu-26.04-deb`, `fedora-rpm`, `macos-dmg` and
  `windows-exe`: the release packages (see [Releases](#releases)). These are
  Release builds in `build-package`, with the tests turned off.

To record where Qt and the other dependencies are installed on your machine,
create an untracked `CMakeUserPresets.json` with a preset that inherits one
of these:

```json
{"version": 3, "configurePresets": [{"name": "me", "inherits": "linux",
  "cacheVariables": {"CMAKE_PREFIX_PATH": "/home/me/Qt/6.8.3/gcc_64"}}]}
```

Then run `cmake --preset me && cmake --build build --parallel`.

### How dependencies are found

CMake finds libmpv and FFmpeg with pkg-config where it is available (Linux,
Homebrew, MSYS2 and vcpkg). Without pkg-config, as in the usual Windows
builds, which ship no `.pc` files, CMake looks for their headers and
libraries under `CMAKE_PREFIX_PATH`, or under `SEVENTHSLIDE_MPV_ROOT` and
`SEVENTHSLIDE_FFMPEG_ROOT`.

On macOS, Homebrew's `openssl` is keg-only. The `macos` preset therefore
points `OPENSSL_ROOT_DIR` at the unversioned `openssl` through
`HOMEBREW_PREFIX`, because that is the OpenSSL that Homebrew's Qt loads.

### Avoid conda environments

Use the system Qt packages for day-to-day builds. Do not configure or link
the project from an activated conda or micromamba environment. Doing so
bakes an `RPATH` into `seventhslide` that points at the environment's `lib/`
directory. Those libraries then mix with Fedora's `libssl`/`libcrypto`, and
the application fails at startup (`OPENSSL_3.0.1` not found). If you have
already built inside such an environment, delete the build tree and
reconfigure from a clean shell.

## Tests

The tests are split into one program per layer: `seventhslide_tests_core`,
`_media`, `_app`, `_remote`, `_sync` and `_ui`. Each program links only the
libraries its tests use. The core, media and sync programs contain no app
code, so a test of app code belongs in `_app`, `_remote` or `_ui`.

Run all of the tests with `ctest`, or one program's tests with its label, for
example `ctest -L sync`.

`seventhslide_tests_dev` holds tests that no regular test run needs. It is
built only when you ask for it:
`cmake --build build --target seventhslide_tests_dev`.

### Browser tests

The web remote's browser tests and their lint (`tests/remote_web`; see its
README) run as part of `ctest` when Node and the Playwright packages are
installed. To install the packages, run `npm ci` in `tests/remote_web`, then
`npx playwright install --with-deps chromium`. Without them, these tests are
skipped. To leave the browser tests out of a run, use `ctest -LE browser`.

### Hidden tests

Hidden tests run only when you name them, each from its own program:
`bin/<program> "[.tag]"`. The header of each test file says what its tests
need. `tools/hidden_tags.py`, which CI runs, checks that every hidden tag has
a row in this table that lists its programs.

| Tag              | Program                                                           | Description                                 |
| ---------------- | ----------------------------------------------------------------- | ------------------------------------------- |
| `[.bench]`       | `seventhslide_tests_dev`, `_ui`, `seventhslide_bench_sync_memory` | Timings of the hot paths, editor and sync   |
| `[.startup]`     | `seventhslide_tests_dev`                                          | The cost of starting the operator window    |
| `[.shots]`       | `seventhslide_tests_dev`                                          | Screenshots (`tools/ui_shots_compare.py`)   |
| `[.deck-check]`  | `seventhslide_tests_dev`                                          | Renders for `tools/deck_check.py`           |
| `[.sync_memory]` | `seventhslide_bench_sync_memory`                                  | Memory used by joining and catching up      |
| `[.sync_make]`   | `seventhslide_bench_sync_memory`                                  | Creates the library the sync timings use    |
| `[.sync_time]`   | `seventhslide_bench_sync_memory`                                  | Sync timings for a church-sized library     |
| `[.audit]`       | `seventhslide_tests_core`, `_media`                               | Checks every look's fit, slide by slide     |
| `[.remote_web]`  | `seventhslide_tests_remote`                                       | The library the browser tests run against   |
| `[.measure]`     | `seventhslide_tests_sync`                                         | The first sync of a large library           |
| `[.pi]`          | `seventhslide_tests_sync`                                         | A server's workload with many libraries     |
| `[.scanners]`    | `seventhslide_tests_sync`                                         | What a flood of connections costs the link  |
| `[.lan]`         | `seventhslide_tests_sync`                                         | Sync with a server on another computer      |
| `[.syncshots]`   | `seventhslide_tests_ui`                                           | Screenshots of the sync pages               |
| `[webengine]`    | `seventhslide_tests_app`                                          | The Qt WebEngine renderer                   |

The `[webengine]` tests need a build with Qt WebEngine and a display with
OpenGL. Qt's offscreen platform has no OpenGL, so run them on a virtual
display:

```bash
SEVENTHSLIDE_TEST_WEBENGINE=1 QT_QPA_PLATFORM=xcb QTWEBENGINE_DISABLE_SANDBOX=1 \
  xvfb-run -a ./build/bin/seventhslide_tests_app "[webengine]"
```

`QTWEBENGINE_DISABLE_SANDBOX` is for systems that may refuse the user
namespaces that Chromium's sandbox relies on, such as Ubuntu 24.04 with
AppArmor. On a desktop, leave it out.

## Data locations

Libraries, settings and media are stored in the platform's data directory
(`~/.local/share/SeventhSlide` on Linux). To use a different directory, such
as a scratch directory for testing, set `SEVENTHSLIDE_DATA_DIR`.

Settings that belong to the computer rather than to a library, such as the
main window's panel layout and the media browsers' view, are stored in Qt's
settings: `~/.config/SeventhSlide/SeventhSlide.conf` on Linux, the registry
on Windows, and `~/Library/Preferences` on macOS. To store them as INI files
in a different directory instead, set `SEVENTHSLIDE_SETTINGS_DIR`.

## macOS

SeventhSlide requires macOS 26 or later, on Apple Silicon or Intel. Build it
with Homebrew's packages:

```bash
brew install cmake ninja pkgconf qt mpv ffmpeg openssl
cmake --preset macos -DCMAKE_PREFIX_PATH="$(brew --prefix)/opt/qt"
cmake --build build --parallel
open build/bin/SeventhSlide.app
```

This app loads the Homebrew libraries from where they are installed, so it
runs only on the Mac that built it. To build an app that runs on another Mac:

```bash
cmake --build build --target macos_app
```

This creates `build/deploy/SeventhSlide.app`, which contains Qt, OpenSSL, mpv
and FFmpeg, and a disk image of it, `build/deploy/SeventhSlide.dmg`. The app
is signed ad hoc rather than with a Developer ID, so Gatekeeper asks for
confirmation the first time it is opened on another Mac (Control-click the
app, then choose Open).

Four network tests need loopback addresses that macOS does not configure by
default. They are skipped unless you add the addresses:

```bash
sudo ifconfig lo0 alias 127.0.0.2 up
for i in 1 2 3 4 5 6 7 8; do sudo ifconfig lo0 alias "127.0.1.$i" up; done
```

## CI

There are three workflows in `.github/workflows`.

`linux.yml` runs on every push and pull request. It runs on Ubuntu 24.04
with Qt 6.8.3 installed by aqt (`.github/actions/linux-deps`), and has these
jobs:

- `build-and-test` runs the Python checks in `tools/`, then builds with the
  `linux-ci` preset, builds `seventhslide_tests_dev`, and runs
  `ctest --preset linux-ci` (including the browser tests), QML lint and the
  QML reference checks.
- `sanitizers` builds with the `linux-asan` preset and runs its tests.
- `clang-tidy` runs clang-tidy 20 over `src/` and `tests/`
  (`tools/clang_tidy.py`).
- `webengine` is the only Linux job with Qt WebEngine. It builds the app and
  `seventhslide_tests_app` with it, runs clang-tidy over the WebEngine code,
  and runs the `[webengine]` tests as described in [Hidden tests](#hidden-tests).

`macos.yml` runs on every push and pull request, on macOS 26 with Homebrew's
packages. It runs some of the Python checks and builds with the `macos-ci`
preset. This is the only build that compiles the `.mm` files, and it also
compiles the WebEngine code, because Homebrew's `qt` includes Qt WebEngine.
It then runs the tests with the loopback addresses described in
[macOS](#macos), but not the `[webengine]` tests. Finally, it builds the
`macos_app` disk image and keeps it as an artifact of the run.

`release.yml` runs when a tag is pushed, or when you start it by hand. It
builds the five packages (see [Releases](#releases)), each from its packaging
preset in `build-package`, with the tests turned off and
`SEVENTHSLIDE_PACKAGE` set. It also sets `SEVENTHSLIDE_DEB_FOR` and, for
Ubuntu 24.04, `SEVENTHSLIDE_BUNDLE_QT`. These variables are read by
`cmake/SeventhSlidePackaging.cmake` and by the install rules in the
top-level `CMakeLists.txt`. This workflow is the only build of the Windows
code, which is listed in the "Platforms" section of docs/CODING.md.

### Running clang-tidy locally

clang-tidy (`tools/clang_tidy.py`, with the checks named in `.clang-tidy`)
needs three things from a build: `compile_commands.json`, the moc files and,
where the build has Wayland pointer confinement, the header for the Wayland
protocol. A full build produces all three. To produce them without building,
do what CI does:

```bash
cmake --preset linux-ci -DCMAKE_GLOBAL_AUTOGEN_TARGET=ON -DCMAKE_AUTOGEN_ORIGIN_DEPENDS=OFF
cmake --build build --target autogen seventhslide_wayland_protocols
python3 tools/clang_tidy.py                  # every source
python3 tools/clang_tidy.py src/app/x.cpp    # one file
```

If your build has no Wayland confinement, leave `seventhslide_wayland_protocols`
out of the second command, because that target does not exist.

The build directory (`-p`, `build` by default) and the file arguments are
paths relative to the current directory. A single clang-tidy process can use
most of a gigabyte of memory, so the script checks four files at a time
unless you set a different number with `-j`. Only the project's own headers
are checked, not Qt's headers, `third_party/`, or files that the build
generates.

## Releases

Releases are GitHub releases of this repository. They are built by
`.github/workflows/release.yml` from a tag, never on a developer's computer.
Every release has the same files under the same names, so
`https://github.com/<owner>/<repo>/releases/latest/download/<file>` always
points at the newest release's copy:

| File | Built with |
| ---- | ---------- |
| `seventhslide-ubuntu-24.04-amd64.deb` | The `ubuntu-24.04-deb` preset and CPack. It carries aqt's Qt in `/opt/seventhslide` (`packaging/linux`) |
| `seventhslide-ubuntu-26.04-amd64.deb` | The `ubuntu-26.04-deb` preset and CPack, using Ubuntu's Qt and FFmpeg, in `/usr` |
| `seventhslide-fedora-x86_64.rpm` | The `fedora-rpm` preset and CPack, using Fedora's Qt and FFmpeg |
| `SeventhSlide-macOS-arm64.dmg` | The `macos-dmg` preset and the `macos_app` target |
| `SeventhSlide-Windows-x64-Setup.exe` | The `windows-exe` preset (MSYS2 UCRT64), `packaging/windows/deploy.sh` and Inno Setup |
| `SHA2-256SUMS` | The checksum of each file |

Each deb works only on the Ubuntu release it was built on, because it
depends on that release's libraries by name. Each deb also includes an
AppArmor profile that allows Qt WebEngine's sandbox to run there. The
Windows build has no Qt WebEngine, because MSYS2's Qt does not include it.

To make a release:

1. Set the version in the top-level `CMakeLists.txt` (`project(... VERSION
   1.3.0)`) and commit the change.
2. Tag that commit `v1.3.0`, or `v1.3.0-beta.1` for a beta, and push the
   tag. `tools/release_version.py` stops the workflow if the numbers in the
   tag do not match the project version. For a beta, the label becomes part
   of the version that the app shows (`1.3.0-beta.1`), through
   `SEVENTHSLIDE_VERSION_LABEL`.
3. The workflow builds all five packages and checks that the deb, the rpm
   and the Windows installer install and start. The disk image is not
   checked. It then creates a draft release of the tag containing the
   packages, `SHA2-256SUMS`, notes generated from the commits since the last
   release, and an attestation of where each file was built. You can verify
   a file with `gh attestation verify <file> --repo <owner>/<repo>`.
4. Once the Linux and macOS workflows have also passed on that commit, edit
   the notes and publish the draft.

To try the packages before you tag a release, run the workflow by hand on a
branch (Actions > Release > Run workflow). It builds the packages and keeps
them as artifacts of the run, without creating a release.

None of the packages is code-signed. That would require an Apple Developer
ID (with notarization) and a Windows code-signing certificate.

## Optional outputs

### SRT

SRT is sent through FFmpeg's `srt` protocol, so it is available only when
the FFmpeg that SeventhSlide links against was built with libsrt
(`--enable-libsrt`). Ubuntu's FFmpeg is built with it; Homebrew's `ffmpeg` is
not.

### Blackmagic DeckLink

The DeckLink API headers are vendored in `third_party/decklink`. The driver,
Blackmagic's Desktop Video, is loaded at run time. DeckLink output is
available on Linux and macOS only.

### NDI

SeventhSlide does not bundle, link or redistribute the NDI SDK or runtime.
The runtime is loaded with `dlopen` at run time, when the operator has
installed it. Nothing in the build or in CI needs the SDK.

The parts of the NDI C ABI that SeventhSlide calls are declared by hand in
`src/render/include/seventhslide/render/ndi_abi.hpp`, which keeps SDK
material out of this repository. After you change that file, verify that it
still matches the real SDK. Do this on a machine with the NDI SDK's headers
installed. CMake looks for them under `/usr/include/ndi-sdk`,
`/usr/include/ndi` and the SDK's usual macOS and Windows folders, or you can
point it at them with `-DNDI_SDK_DIR=<sdk>`:

```bash
cmake -S . -B build-abi -G Ninja -DSEVENTHSLIDE_NDI_VERIFY_ABI=ON
cmake --build build-abi --target seventhslide_ndi_abi_check
```

Compiling that target is the check itself: it uses static assertions to
compare every struct size, field offset, enum value and function signature
against the real headers. The check is never part of a normal build or of
CI.
