# Coding

This guide describes how the SeventhSlide source is organized and the rules
each part of it follows. Many of the rules are enforced by scripts in `tools/`
that CI runs, and each rule below names the check that enforces it. For
building, testing and CI, see `docs/BUILDING.md`.

## Source layout

```text
CMakeLists.txt
cmake/          # dependency helpers
src/
  core/         # domain library, no QML (seventhslide_core)
  render/       # audience windows, painters, output sinks (seventhslide_render)
  remote/       # LAN web remote: HTTP + WebSocket server and web client
  audio/        # the Audio Mixer's engine: FFmpeg decoding, mixing, Listen, sound devices and microphones (seventhslide_audio)
  stream/       # live streams sent over the network: SRT with FFmpeg (seventhslide_stream)
  link/         # sync connections between SeventhSlide computers, over TLS (seventhslide_link)
  server/       # `seventhslide --server`: holds synced libraries for computers that are not on at the same time (seventhslide_server)
  app/          # Qt application, SessionFacade, list models (seventhslide)
  ui/           # QML operator UI
  tools/        # layout_dump, render_smoke, render_song CLIs
  devsupport/   # code shared by the tests and tools, never linked by the app (seventhslide_devsupport)
tests/          # Catch2 (seventhslide_tests_*); tests/remote_web is the phone remote's browser suite
fixtures/       # golden inputs and outputs for the layout, theme, render and schema tests
tools/          # CI checks and helper scripts
docs/           # documentation
packaging/      # desktop launcher; the server's systemd unit, launchd plist and server.json; files the release packages need (macOS bundle, deb, Windows installer)
third_party/    # vendored code: DeckLink API headers, the QR code encoder, miniaudio, the shipped fonts and Bibles
```

## Core (`src/core`)

`seventhslide_core` is the UI-agnostic domain library.

- Core must not import QML or Qt Quick.
- Core uses Qt Core and Qt Gui only at the documented boundaries listed under
  [Qt and OpenSSL in core](#qt-and-openssl-in-core).
- Core contains no HTTP or remote-serving code.

### Module tiers

Core's modules are arranged in tiers, listed here lowest first. A module may
include its own headers and the headers of modules in lower tiers. Modules in
the same tier are independent of each other. `tools/core_layers.py` enforces
this in CI, so if you change the list, change it there and here together.

1. `util`: strings, files, errors and other helpers that every module may
   use. It includes no other module.
2. `json`, `geom`, `platform`, `media`, `crypto`: helpers with no project
   dependencies. `platform/paths` uses only `std::filesystem`. `crypto` is
   the only place that calls OpenSSL. It covers signing keys, SHA-256 and the
   passphrase-locked box.
3. `fonts`, `library` and `update`. `fonts` reads font files and works out
   which file a family name refers to. `library` is the registry of
   libraries on disk, along with each library's output destinations and its
   audio mixer levels on this computer. `update` reads the app's releases on
   GitHub and decides which one to offer.
4. `model`: the data types and their JSON codecs (nlohmann/json, no Qt).
5. `parse`: parsing of lyrics, Bibles and song files (pugixml, no Qt). Every
   kind of song file comes in through `parse::read_song_file`
   (`song_import.hpp`), and Bibles come in through `parse::parse_bible_file`
   (`bible.hpp`).
6. `canvas`: the single canvas object type, `canvas::Object`, which holds an
   object's geometry, type, parts, stored form and the files it references.
   The module also provides the list operations on a stack of objects, hit
   testing and selection geometry, edit history, and `ObjectEditor`, which is
   the base class of both canvas editors.
7. `layout`: text measurement, wrapping, chords, styled text boxes and title
   overlays, slide grouping, background fills, and the editor's box HTML.
   `text_measurer` is the plain interface. `qt_text_measurer` and
   `qt_font_faces` are its Qt Gui adapters, which tests run on the offscreen
   QPA plugin.
8. `freeform`: Freeform documents (a canvas object plus its cue fields,
   slides, decks and cues), their editor built on `canvas::ObjectEditor`, and
   the compile step that turns them into what the scene paints.
9. `theme` and `db`. `theme` holds the theme cascade, the element model, the
   theme library and the theme designer's editor. `db` holds the Qt Sql
   (QSQLITE) repositories and the style-profile service built on them.
10. `build`, `exchange` and `sync`.
    - `build` turns resolved themes and content into laid-out slides for
      every output: song, Bible and title-slide layout, and `build_slides`.
    - `exchange` reads and writes Freeform files (`.ssff`, `.ssfb`) and
      service packages as `docs/FREEFORM_FILES.md` describes them
      (`bundle_io.hpp`). It also handles what a document becomes when it
      leaves one library and lands in another (`adapt.hpp`).
    - `sync` is the sync engine. It covers change files, snapshots
      (`snapshot.hpp`), the interface to where they are stored (`SyncStore`),
      and who may write. That last part includes the signed envelope that
      every stored file is, the member list, the admin key, and the gate a
      file must pass before it is stored.
11. `scene`: the `SceneFrame` paint contract and its builders, including the
    theme designer's preview frame and the Freeform snapshot of a build.
12. `session`: the live command API (stacks, the player, and frames per
    output).

Nothing in `src/core` may depend on `src/devsupport`; `tools/core_layers.py`
checks this as well.

### Qt and OpenSSL in core

Only these parts of core use Qt:

- `db`
- `sync`, which is built on `db` and Qt Core
- the two `layout/qt_*` adapters
- `platform/computer_name`, which uses Qt Core's `QSysInfo`

Only `crypto` includes OpenSSL.

### Database statements and schema

- Run a statement as a single call:
  `exec(db, sql, {binds...}, "what it does")` (see `db/query.hpp`). A
  statement that runs in a loop is `prepare`d once and `exec`uted once per
  row.
- `schema.cpp` builds the whole schema in one step, at `k_schema_version`.
  The schema is checked in as `fixtures/schema/library.txt`, so any change to
  the schema also changes that file. To update it, run the schema tests with
  `SEVENTHSLIDE_UPDATE_SCHEMA=1` and review the diff.
- There are no migrations, because no released build has created a library
  yet. When the schema changes, a one-off script converts the libraries
  created so far (`tools/convert_library_schema_<version>.py`, tested by
  `tests/test_convert_schema_<version>.cpp`). A library at any other version
  is refused.

### Retired names and unused fixtures

- A reader keeps no fallback for a name that no build writes any more, such
  as a retired theme key, transition or table. `tools/retired_keys.py`, which
  CI runs, fails when src/, tests/ or fixtures/ write such a name.
- `tools/legacy_markers.py` keeps `k_legacy_` names out of src/, along with
  comments that justify code by what a previous version of the program did.
- Do not keep a fixture that nothing reads (`tools/fixture_refs.py`, which CI
  runs).

### Sync tables

- Add every new table, and every new reference from one row to another (a
  foreign key, or an id stored in JSON), to `db/sync_tables.hpp`, together
  with how it syncs. Tests fail if a table or foreign key is missing there.
- A table that syncs by record gets its `uid` from `index_uid`
  (`schema.cpp`).
- Every table that syncs marks the outbox through `mark_changes`
  (`schema.cpp`, `sync_outbox.hpp`).
- A write that replaces a row with an identical one is harmless. The outbox
  only records which records to compare with the copy that was last shared.

### Folders and media files

Every folder table (image, video, audio, Freeform and web page folders) is a
`FolderTree`, which its repository returns from `folders()`.

Each media kind has a file registry (`image_files`, `video_files`,
`audio_files`) that stores each file under an opaque name that never changes.
Renaming a file changes only its title.

The differences between media kinds are defined in `db/media_kind.hpp`. Code
asks it (for example, `hidden_when_in_use(kind)`) rather than testing for a
particular kind.

A Freeform document's pictures and videos are its own, not the library's
(`freeform/own_media.hpp`). Each Freeform item has a folder,
`freeform_media/<item uid>/`, and its document names each file by URL,
`/static/freeform_media/<uid>/<name>`. No registry lists them, so the Images
and Videos tabs never show them. A file brought in from the editor goes
straight into the item's folder (`SlideEditSession::importMedia`), and so does
a background theme's picture or video. When an item is saved, created or
duplicated, any file its document names that is not already in its folder
(a library file, a theme's background, another item's file) is linked into
it (`take_own_media`). A file stays as long as any document names it: the item
itself, or a service's copy of it. Sync owns these files in the same way, by
the URLs in `freeform_items.document` and `service_items.data`
(`db::k_sync_files`). Files no document names are removed when the editor
closes the item, when the item is deleted, and when the library is opened
(`FreeformLibraryController::prune_own_media`).

### Fonts

A library's fonts are one more registry, `font_files` (`db::FontRepository`),
which has no folders. It has one row per face, named by the face's content, so
the same font added on two computers becomes a single record.

- `fonts::read_font_file` checks a font file. Nothing in `db` or `sync` reads
  font files.
- `app::FontStore` adds fonts to the library. That includes the copy of the
  fonts the program ships (`third_party/fonts`), which each library takes
  once.
- Slides use the library's fonts and no others. `fonts::FontRegistry` maps a
  family to its file, and `app::FontController` (`session.fonts`) fills the
  registry.
- A view that sets a font by name must go through `session.fonts.painted`.
  Otherwise Qt would find the system's font of that name.
- A change of font changes how text wraps, so nothing that lays text out
  again happens while something is live.

### Shipped Bibles

A library takes a copy of the shipped Bibles (`third_party/bibles`) in the
same way, through `LibraryController::take_shipped_bibles`. The Bibles are in
the program's own resources, not in applib's, so a library that a test
creates takes none.

### Stacks: Program and Preview

A stack in the session is a `session::Stack`. It has a Program, which is what
the outputs show, and a Preview beside it when Preview is on for that stack.
Each side is a `StackRuntime`, and a runtime does not know which side it is
on. A `Slot` names one side of one stack, and `Session::runtime` returns the
runtime for a slot.

- Code that decides what the outputs show, play and sound walks the Programs
  only.
- Code that follows content wherever it is (a rebuild, an edit of what a stack
  shows, a clear) walks every side with `Session::for_every_side`.
- Code that reaches into a stack names the side it means, for example
  `focused_stack().program`. That way a new loop cannot treat a Preview as
  live by accident.

Which stacks have a Preview is a choice made per computer. It is stored by
`library::ConsoleStore` and never synced.

Content sent to a stack lands in one of two ways:

- `Land::Cue` lands on the stack's Preview, if it has one.
  `Session::arrive_cued` is the only way into a Preview.
- `Land::Live` lands on the stack's Program.

`Session::transition` performs the take: the two runtimes swap with
`std::swap`, so each keeps its place. A blank travels with its side. A freeze
holds the outputs, not a side.

### Timed media

Timed media (video and audio) plays through `media::IMediaPlayer`. The session
hands out the players, and the app owns them.

- Each video or audio item that is loaded is a separate instance
  (`StackRuntime::media_instance`), and the session asks for a player by that
  instance (`Session::PlayerFor`).
- The app plays each instance on its own deck
  (`PlaybackController::MediaDeck`). On each sync, the app works out from the
  session what every deck should do.
- The session never stops a player. A runtime releases its item's player when
  the item goes away, and the app notices at its next sync.
- `Session::attach_players` runs after each live change. A player is released
  only after it has been taken off its stack.
- All sound is heard through the Audio Mixer (see
  [Audio](#audio-srcaudio)). mpv draws only a video's picture.
- Audio is not a theme context, because it paints nothing.

A video's pictures reach the outputs the same way as every other picture drawn
outside the scene: as a source keyed by its item (`SceneVideoMedia::source`,
`scene::content_video_key`) in the `"content"` provider's `SourceFrames`. No
painter holds a player.

## Audio (`src/audio`)

`seventhslide_audio` is the Audio Mixer's engine. It is plain C++20 with no
Qt and depends only on core, so it can be tested headless. FFmpeg decodes
(`AudioDecoder`, which is private), miniaudio plays (`third_party/miniaudio`),
and PipeWire provides microphones in builds where CMake found it. `render`,
`stream` and `app` link the audio library, and it never includes them.

- All audio is 48 kHz, stereo, float, in blocks of 480 frames
  (`audio_format.hpp`).
- `AudioFilePlayer` is the `IMediaPlayer` for an audio item and for a video's
  sound. Its position is the number of frames the mixer has taken from it.
- `Mixer` sums each stack's sources and each output's stacks, with a fader, a
  meter, Mute and Listen on every strip, and pushes each output's mix to its
  `OutputAudioSink`s. The mixer and the player ring buffers are protected by
  short mutexes rather than lock-free structures, and the audio thread never
  waits for a plan to be built. `mixer.hpp` documents what runs under each
  lock.
- `AudioEngine` is the clock. It runs on the Listen device's callback, with a
  timer standing in when there is no device. Tests run it on miniaudio's null
  device or on the timer, and make no sound.
- Every sink runs on its own clock, so it takes the mix through a
  `DriftBuffer` and keeps its place in the mixer with a `MixerLink`. A sink
  must leave the mixer before it releases the memory that `push` writes to.
- A video's picture follows its sound (`app/video_with_sound.hpp`).

In the app, `MixerController` (`session.mixer`) owns the engine. After every
change it passes the engine the current stacks and outputs (`sync`), and it
stores each library's levels on this computer (`library::AudioMixStore`).

## Stream (`src/stream`)

`seventhslide_stream` sends live streams over the network. For an SRT
destination, it encodes the pictures and sound and sends them as MPEG-TS over
SRT. It is plain C++20 on FFmpeg, with no Qt, and depends on core and audio, so
it can be tested headless. `render` links it.

- `stream_settings.hpp` holds the rules that turn an operator's
  `StreamSettings` into SRT latency, keyframe interval and encoder settings,
  so the editor and the streamer agree.
- `usable_encoders` tries each candidate encoder once and remembers which
  ones opened. That a build includes an encoder says little about whether this
  computer can run it.
- `SrtStreamer` does all of its waiting on its own thread and reports its
  state through a listener. `render::SrtSink` turns those reports into
  `OffscreenSink::statusChanged`, which reaches the editor as the
  destinations' `statusChanged`.

## App (`src/app`)

### SessionFacade and the controllers

`SessionFacade` is the composition root and the QML entry point. The area
controllers (`session.library`, `session.themes`, ...) do not know about the
facade. They know about each other only where `tools/controller_deps.py`
(which CI runs) allows it.

- Controllers borrow shared state by reference: `session::Session`,
  `OpenLibrary` (the open library's database, settings, media folder and the
  registry) and `PreviewResources` (fonts, measurer, image cache and theme
  revision).
- Controllers announce changes with signals (`liveChanged`, `outputsEdited`,
  ...). `SessionFacade::wire_controllers` is the one place that decides what
  happens in response.
- The library tabs add to the open service through `IRundown`
  (`rundown.hpp`), which `LibraryController` implements. They know nothing
  else about the service.
- Where a controller needs an answer from the application, an interface
  stands in: `ILibrarySwitcher` (open another library) and
  `IFreeformDocumentStore` (the Freeform editor's documents, faked in tests).
- A few controllers hold another controller because of what they are. The
  list of which ones, and why, is `ALLOWED` in `tools/controller_deps.py`.
  Add any new one there, with the reason.

### Live output

`LiveController` (`session.live`) owns what goes live: the play commands, the
service selection, and the outputs' blank and freeze.

What one well of the Controller shows of one side of the focused stack is a
`SideView`: `session.program`, and `session.preview` for a stack with Preview
on. The facade wires both in the same way (`wire_side`), and the items that
draw a side take the view they draw.

The take belongs to `StackController` (`session.stacks`).

### Library tabs and search

Every library tab (images, videos, audio, Freeform, web pages) is a
`FolderLibrary`. It gives QML the same folder calls (`setFolder`,
`createFolder`, `moveItems`, ...), written once over a `FolderBrowser`, which
tracks the folder shown and the search. Each library still decides what a
folder holds, how its rows are keyed and what deleting a folder does to them.

Every library search, whether in the app, in QML (`SearchText`) or in the web
remote, matches with `db::SearchQuery` (`db/search_text.hpp`). Lists of names
sort with `db::name_collator()`.

### External tools

The app runs some programs alongside itself (ffmpeg, ffprobe, yt-dlp).
`platform/tools` finds them, and `ToolJobs` (`tool_jobs.hpp`) runs them.
`ToolsController` (`session.tools`) owns both and looks for nothing until it is
asked, so creating a session starts no process. A conversion or a download is
written under a staging name and then taken into the library through
`MediaController::adopt_staged`.

### Updates

`AppUpdater` (`session.updates`) checks GitHub for a newer release of
SeventhSlide. It reads the repository's list of releases, and
`update/releases.hpp` in core decides which release to offer: the newest one
on the chosen channel (stable or beta) that is newer than the running version.
It checks a minute after the app starts and then once a day, but only after
`main` calls `start`, so a session made in a test never goes online. A check
that fails changes only its status line. Nothing opens by itself during a
service.

Only one check, download or install runs at a time. While one runs, the
channel cannot change, and the release being downloaded is recorded when the
download starts, so a later check cannot change it.

A packaged build knows which file of each release is its own
(`package_file()`, from `SEVENTHSLIDE_PACKAGE_FILE`). The updater downloads
that file and keeps it only if its SHA-256 matches the digest GitHub reports,
then checks it again just before installing it. On Windows and macOS it asks
first, because installing closes the app: it opens the installer or the disk
image and asks the window to close. On Linux it installs the deb or rpm with
`/usr/bin/pkexec` and `/usr/bin/apt-get` or `/usr/bin/dnf`, named by full path
so that nothing in the user's `PATH` runs as root, and then offers to restart.
Every link it follows must point into the repository's releases on
github.com, and release notes are shown as plain text. A build configured
with `SEVENTHSLIDE_UPDATE_CHECK` off never checks.

The digest comes from GitHub, along with the file. It catches a damaged or
altered download, but not a release published by someone who has taken over
the repository; code signing the packages would.

### Export and import

Everything that leaves the library goes through `ExportController`
(`session.exporter`). Freeform files and packages come in through
`FreeformImportController` (`session.freeformImport`). Both read the library
on the GUI thread and do the slow work on a worker.

An export paints a slide the way it plays (`session::still_frames`,
`render::paint_still`) and writes archives with `util::ZipWriter`.
`parse::write_song_xml` writes what `parse_song_xml` reads back.

### Windowing system access

The object through which the app asks the windowing system for things
(`PointerConfinement`, `pointer_confinement.hpp`) is created by `main()` for
the platform the app runs on and handed to the session, in the same way as Qt
WebEngine's renderer factory. Tests hand in a fake, so no test asks the
desktop for anything.

### Strings and ids

- Convert a `std::string_view` (or `std::string`) to a `QString` with
  `QString::fromUtf8(view)`, which takes it as a `QByteArrayView`. Do not
  spell out `.data()` and `.size()`, and never pass `.data()` alone: that
  reads up to a NUL, which a view does not guarantee. `tools/qstring_views.py`,
  which CI runs, checks this.
- Convert a list of ids from QML to a `std::vector<std::string>` with
  `ids_from_list` (`qml_ids.hpp`).

### Exposing the session to QML

QML receives the session and the web remote as required properties of
`Main.qml`. `main()` sets them (and so do the UI tests) before the window
loads. Sync arrives as the `sync` property, which is null when sync is off and
in most UI tests. Do not add context properties.

A C++ item that draws from the session derives from `SessionBoundItem`, or
from `SessionBoundPaintedItem` if it paints with QPainter
(`session_bound_item.hpp`). Its `session` property is a typed
`SessionFacade`, so qmllint checks what is bound to it. The item connects to
whatever it follows in the session in `connect_session`, and the base class
drops those connections when the session changes.

### Sync in the app

`SyncController` is the window's sync. `main()` creates it, rather than the
session, so a test that builds a session opens no port. It owns this
computer's `link::SyncNode`, which holds one `link::LibrarySync` per synced
library, each on its own thread with its own database connection.

- What a sync applied to the open library comes back through
  `SessionFacade::reload_after_sync`, by record (`sync::Changed`). Only the
  lists that show those records are read again.
- A viewer's library is read-only (`SessionFacade::set_read_only`). The
  controller answers `SessionFacade::set_read_only_check`, which is asked when
  a library opens, before anything is written to it.
- When the controller refuses a write, `report_entry_error` words the
  refusal.

## UI (`src/ui`)

### Module structure

The UI is a single QML module, `SeventhSlide`, organized in folders by area:
`controls/` (reusable widgets and helpers), `dialogs/`, `operator/` (the main
window's panels), `editors/{song,slide,output,shared}`, `settings/` and `js/`.
`Main.qml` and the four QML singletons (`Style`, `Modals`, `ContentTypes`,
`TransitionKinds`) sit at the top level.

- A file's type name is its file name, wherever the file sits.
- Delete any QML file, JS file or C++ QML type that nothing but CMake refers
  to (`tools/qml_unused.py`, which CI runs).

### Colors, glyphs and displayed values

- Colors and type sizes come from `Style.qml`: the type scale (`fontMicro`
  ... `fontHeading`) and the role tokens (`textStrong`, `menuHover`,
  `dialogChrome`, ...). Ink roles that are meant to look alike refer to one
  shared step (`inkGrey54`, ...). The surface greys deliberately differ by one
  or two levels.
- A view writes no color of its own (`tools/colour_literals.py`, which CI
  runs). The only exceptions are the color picker's spectrum and the white it
  starts on.
- Every glyph is one row of `src/app/glyphs.cpp` and is drawn in QML with
  `Icons.url(name, ink, width)`. A stack's or an output's icon is a glyph name
  from its list (`model::k_stack_icons`, `model::k_output_icons`).
- A kind of content (what a stack receives, a Library tab, a rundown row) is
  named and drawn from `ContentTypes.qml`, which has one row per entry in
  `model::k_stack_type_keys`.
- The values a view shows come from C++, as they will be painted. The
  editors' object maps carry `freeform::painted_type` (with a kind's defaults
  filled in). A new object's values when nothing is selected come from
  `slideEdit.objects.selection`, and a theme's settings come through
  `slideEdit.theme.settings`. QML adds no `|| default` of its own
  (`tools/qml_defaults.py`, which CI runs).

### Editor state

- An editor's state is a `*State.qml` view-model: `SongEditState`,
  `SlideEditState`, `OutputEditState` with its `OutputDestinationsState`, and
  the `Settings*State`s. The state owns the data and every edit.
- The dialog is the sheet around the state. It keeps what only a view has:
  pickers, confirmations, file choosers, focus and keys. The state asks for
  those through signals named `...Asked` or `...Requested`. The dialog binds
  the view facts the state needs (a stage's size, a drag in progress) into it.
- Panels take their state as `required property SomethingState dialog`.
  `tools/panel_refs.py`, which CI runs, checks that every `dialog.X` they use
  exists.

### Content rules in C++

- Rules about content live in C++, and QML calls them. For example, `SongText`
  (over `parse::`) decides how lyrics split into verses and which code a
  verse label plays as. There is no JavaScript copy of those rules.
- A file chooser offers picture, video or sound files with
  `session.media.mediaFilePatterns(kind)`, not with a list of its own
  (`tools/file_filters.py`, which CI runs).

### Commands and error messages

- A command that a view calls (`Q_INVOKABLE QString`) returns the reason it
  refused, or an empty string, which QML receives as `""`. The view shows that
  string unchanged and tests it with `err.length`. A command that returns a
  map (`renameMediaFile`) leaves out its `error` key on success.
- Everything the operator, or a phone on the remote, is shown for a failure is
  written for people. The wording of a database, of JSON or of the file system
  is not. `report_entry_error` (`message_for_operator`, or
  `util::message_for_people` outside the app) logs `what()` and shows a plain
  sentence instead.
- An exception whose message comes from a library's own wording derives from
  `util::InternalError`, which also carries a sentence for people, as
  `db::DatabaseError` does.
- A refusal is a `tr()` sentence at the place that refuses, or a function in
  `src/app/refusals.hpp` when many commands give the same refusal. A refusal
  that core words itself (an `EditResult`'s error) is shown exactly as core
  words it.
- How an entry point hands back a failure depends on what it returns
  (`src/app/entry_error.hpp`).
- `tools/entry_errors.py`, which CI runs, fails on `what()` used outside a log
  and on a refusal written as a literal.

### Models

- A view reads a model's rows by the model's role enum
  (`ServiceItemModel.ItemIdRole`), never by `Qt.UserRole + n`, so a role added
  to the enum cannot shift what a view reads.
- A model that QML reads this way is `QML_ELEMENT` + `QML_UNCREATABLE` with
  `Q_ENUM(Roles)`.

### Shared interaction

- `ListSelection` (click, Ctrl, Shift and marquee over keyed rows) handles
  selection in every multi-select list.
- `js/Reorder.js` decides where a dragged row lands.
- `ReorderDrag` and `ReorderMouseArea` implement drag-to-reorder lists.
- `MultiSelect.autoScrollView` handles edge scrolling.

### Shared controls

Build new pages from the shared controls in `src/ui/controls` (`ScrollPage`,
`GroupList` / `GroupRow`, `FormRow`, `ListWell`, `AppMenu`, `DropdownField`,
...).

- A menu keeps track of whether it is open. `AppMenu` closes on Escape and
  when the item it hangs from is hidden. A state keeps no menu's open flag,
  and does not keep what a menu is open for or hangs from either
  (`tools/menu_state.py`, which CI runs).
- A text field is a `DialogField`, or a kit control built on one.
  `tools/text_fields.py`, which CI runs, lists the few fields that a view
  defines itself, and why.
- `AppPromptDialog` is the only name prompt.
- Every window is a `WindowShell`: a title bar, a rail of pages, the page area
  and a Cancel / Save bar. The window itself keeps its scrim, keys and
  confirmations.
- Fields, and the buttons beside them, are `Style.controlH` high.
- A window, page or dialog is created the first time it is asked for, behind a
  `LazyLoader`, so the operator window starts with only what it shows.

### Main window

- The main window is a `DockArea` around the Controller. Its panels are
  `DockPanel`s, placed where `DockLayout` says. `DockLayout` is C++ and is
  stored per computer in QSettings. `DockState::layout` turns the layout into
  rectangles, so the view only places items.
- Windows that the bar or several panels open (Settings, Collaboration, the
  Output window) belong to `Main.qml`. A panel asks for one with a signal.
- Something dragged across panels draws its ghost on the window, not in the
  panel it left, because a panel clips its contents.
- A dialog with anything inside it that claims `focus: true` must be a
  `FocusScope`, so that a closed dialog cannot hold the window's keys.
- The library panel's tabs are `LibraryTab`s (`SongsTab`, `BiblesTab`,
  `LiveSourcesTab`, and `MediaBrowser` for the media tabs). Each tab owns its
  toolbar actions, search, selection, and create and export choices.
  `LibraryPanel` works only through that interface.

### qmllint and delegates

- Every name must resolve statically. qmllint reports UnqualifiedAccess as a
  warning, and CI fails on warnings (`tools/check_qmllint.py`).
- Files with delegates use `pragma ComponentBehavior: Bound`. Delegates
  declare the model data they read (`required property var modelData`,
  `required property int index`, roles), and their children reach them by id.
  Under Bound, an undeclared `modelData` is a runtime ReferenceError.
- Inside a `transform` (which is not an item), qualify `width` and `parent`.
  A bare name there is looked up in the component's scope, not in the
  transformed item's.

### Screenshot baselines

Before a UI refactor, take a shots baseline (`[.shots]`,
`tools/ui_shots_compare.py`). The shots taken afterwards should be identical.

## Slide editor

One window edits both a Freeform item's slides and a theme's canvas
(`editors/slide/`, `SlideEditorWindow` over `SlideEditState`).

### Layers

The editor is built in layers. From the document up:

- `canvas::Object` (`canvas/object.hpp`) is the single kind of placed thing.
  It has a frame, a kind and parts (`canvas/parts.hpp`). A part is left out of
  the stored form while it has its default value. `freeform::Object` adds the
  cue fields, and `theme::Element` wraps an object with a binding.
- `canvas::ObjectEditor` implements the editing that both share: selection,
  gestures and undo. `freeform::Editor` and the theme's element editor derive
  from it.
  - An edit returns an `EditResult` and either commits in full or changes
    nothing.
  - An edit of the current slide alone (or of a theme's context) goes through
    `commit_here`, so its undo step covers that slide, not the whole
    document. Anything that writes elsewhere uses `commit`.
- Each property is one row of one table (`freeform/object_properties.cpp`,
  which the theme's table builds on). A row has a name, a writer that refuses
  values it cannot take, and a value read back as it paints. For a number
  limited to a range, the row also has that range (`canvas::Range`). The
  writer clamps to it, and the editors' fields take their limits from it.
  - One test writes back every value it reads, by name, and expects no
    change. Any value that can be read must therefore be writable.
  - Another test writes past each range's ends and expects the ends back.
- `SlideEditSession` (`session.slideEdit`) is the API that QML calls, split
  into parts: `objects`, `slides`, `cues`, `fields`, `animations`, `theme`.
- The window is a `RibbonBar` of `Ribbon*` tabs, a `SlideSidebar`, the
  `SlideStage` and the `SlideSorter`. A sidebar control (`SidebarNumber`,
  `SidebarToggle`, ...) is bound to a property by name. It reads the value,
  takes its limits and writes it back, so a row states only its label, the
  property name and how it is shown. Which sections are shown follows
  `theme::object_controls` (`slideEdit.objects.supports`).

### From document to screen

`freeform::compile` turns a deck into what the scene paints, once for each
output. `scene::build_scene_frame` picks a slide at a build step, and
`render::paint_scene_objects` paints its objects back to front.

The stage does not have a second painter. `FreeformShapeItem`,
`FreeformPictureItem` and `OverlayTextItem` paint through the same functions
(`shape_painter`, `picture_painter`, `text_painter`). A text box that the rich
text item cannot show (`drawsAsOutput`) is drawn the way the output draws it.
Tables are the exception: the stage draws them in QML, and the output draws
them with `table_painter`.

A paragraph's own alignment, spacing, line height and indent are not a part.
They are stored as a `<p=...>` at the start of the paragraph's line of markup
(`model::LineFormat`), so they follow the line through every edit.

### Adding a part

A new part of an object touches these places, in this order:

1. `parts.hpp` and the part's stored form.
2. `object.cpp`'s `map_object`, and `map_object_json` when the part has
   lengths (a canvas change maps them).
3. The property table.
4. The painter.
5. The stage's item.
6. A sidebar section.

A part that names a file is also one row of `canvas/media_refs.hpp`, with the
walk in `freeform/media_refs.hpp` beside it.

### Animations and video

A slide's animations become build steps (`freeform::plan_steps`). A video
object's player is found from the session (`Session::object_videos`), not
from the frames, so a blank does not stop it.

## Remote (`src/remote`)

`docs/REMOTE_PROTOCOL.md` defines what a client may send and receive, and what
the bundled client does. A change to a command, a section or a route must
update that document too.

### Server side

- The remote never links `src/app`. `IRemoteBridge` is all it may touch, and
  `SessionRemoteBridge` implements it.
- Every command is a `remote::Command`. The protocol table gives each command
  its wire name, its arguments and the sections it invalidates. The bridge's
  `invoke` switches over the enum, so the compiler requires a handler for
  every command.
- Sections built from the operator's list models name their wire fields
  (`WireField`) rather than forwarding QML role names.
- Every connected device has its own `remote::View` (the stack tab it shows).
  The server keeps the view with the client and passes it to the bridge with
  each section read and each command. The bridge keeps no per-device state.

### Web client

The bundled client is plain ES modules in `src/remote/web/js` and stylesheets
in `src/remote/web/css`, with no build step.

- `main.js` starts the client and redraws the page from what the server
  sends.
- `connection.js` talks to the server and knows nothing about views.
- Each part of the page is a module that wires its own controls in a
  `wire...()` function, which `main.js` calls.
- `dom.js`'s `el()` and `glyph()` build rows. Never build markup with data in
  it.
- Add each new module or stylesheet to the resources in
  `src/remote/CMakeLists.txt`. The server serves each one from there at
  `/js/<name>` or `/css/<name>`.

The client looks like the app. `css/tokens.css` copies the Style.qml tokens it
uses, under the same names in kebab case, and no other stylesheet writes a
color. `tools/remote_tokens.py`, which CI runs, checks the copy against
Style.qml.

The client's glyphs are the ones in `glyphs.cpp`. The server puts the table's
sprite into index.html (`IRemoteBridge::glyph_sprite`), and the client draws
`#ic-<name>`. A stack's icon comes in its section as a `data:` URL
(`glyphs::data_url`), one for each ink.

A change to the client must keep `tests/remote_web` passing (the browser
tests and their lint; see its README). A new client feature gets a spec there.

## Link (`src/link`)

`seventhslide_link` carries sync's connections between SeventhSlide
computers, on top of core's sync engine, member list and crypto. It uses Qt
Network. It never uses Qt Quick and never links `src/app`.

- `Link` (`link.hpp`) listens (`Listener`) and connects out, and keeps one
  `Peer` per connection, one per computer.
- TLS 1.3 is mutual. Each side's certificate is made from its computer key
  (`tls.hpp`), and the member list decides whose key is accepted.
- Messages are length-framed JSON, with media sent in bytes frames between
  them (`frames.hpp`). Every file that arrives goes through the `Gate` before
  it is stored.
- `Fetches` and `routes.hpp` decide which connection fetches what.
- No file sent between computers is larger than `sync::k_largest_store_file`
  (16 MiB). The largest record (`db::k_largest_record`) fits within that,
  along with what is sent with it.
- The link runs on the window's thread, so nothing in the link reads or writes
  a file on that thread. File work happens on the link's own threads
  (`PeerShared`, `run_off_thread`).
- The link moves files between stores. The app runs the engine when a store
  changes (`Link::store_changed`) and tells the link when the engine wrote
  something (`Link::library_changed`).
- `Discovery` announces this computer on the church network and listens for
  the others. `Reach` keeps a connection up to every member it can find.
- `SyncNode` combines all of these the way every computer runs them. The app
  and the server each own one, with the same settings (`SyncSettings`,
  `sync_settings.json`), and each adds only what is its own. Every sync
  setting or action exists in both, or the code says why it does not.
- Whether syncing may happen now is decided in one place,
  `SyncNode::follow_pause`.
- Each library's shared log (`sync/shared_log.hpp`) holds the Collaboration
  window's messages and a note of each change file sent
  (`sync/change_notes.hpp`). The log changes nothing in the library, so it
  keeps moving while syncing is paused.

### Logging

`QT_LOGGING_RULES="seventhslide.link.debug=true"` traces every message. These
logging categories are available:

| Category                  | Traces                         |
| ------------------------- | ------------------------------ |
| `seventhslide.link`       | every message                  |
| `seventhslide.discovery`  | finding other computers        |
| `seventhslide.reach`      | finding other computers        |
| `seventhslide.sync`       | each library's sync            |

## Server (`src/server`)

`seventhslide --server` (`docs/SERVER.md`) runs SeventhSlide as a server.
`main` hands control to `server::run` before any of the interface exists.

- The server is built on core and the link, with Qt Network and Qt Sql. It
  never uses Qt Quick, never links `src/app`, and never creates a
  `QGuiApplication`. Qt Gui comes in only through core, and
  `tests/test_server.cpp` runs the program as a server where no window system
  can be loaded.
- `Server` owns a `link::SyncNode`, which holds one `link::LibrarySync` per
  library. Each library's database and engine live on that library's own
  thread and are touched only there. The link reads the store through a store
  object of its own.
- The commands (`join`, `status`, `sync`, `settings`, `admin`, `address`,
  `leave`) are clients of the running server over a local socket
  (`control.hpp`), so only one process writes a server's data.
- Admin work is `admin_work`, a plain function over a store, which is tested
  without threads.

### Configuration interface

`seventhslide --server config` is one more client of that socket.

- `ConfigModel` knows what can be done and the current state. It contains no
  screen code and is tested over the real socket.
- `ConfigUi` (FTXUI, which nothing else uses) draws the forms. Tests drive it
  by key.
- Settings go through `ask_settings` (`settings_edit.hpp`), and
  `data_folder.hpp` decides which folder is the server's.

## Dev support (`src/devsupport`)

`seventhslide_devsupport` holds what the tests and the developer tools share.
The app never links it. It contains the flat style keys that the fixtures use
to describe looks (`flat_style`), and the layout dump that is compared against
`fixtures/oracle` (`scene_dump`). It is built only when tests or tools are
built. Nothing in `src/core` may depend on it.

## Platforms

Only the release workflow (`release.yml`) builds the Windows code, and it runs
no tests, so the Windows code may not build between releases. The Windows code
is:

- `src/app/pointer_confinement_windows.cpp`
- `cmake/SeventhSlideWindows.cmake`
- `packaging/windows`
- the `windows-msvc`, `windows-mingw` and `windows-exe` presets
- the `WIN32`, `MSVC` and `MINGW` branches of the CMake files
- the `_WIN32` and `Q_OS_WIN` branches of the sources
  (`grep -rln '_WIN32\|Q_OS_WIN' src`)

Build a change to any of this on Windows, either by hand or by running the
release workflow on its branch. If it was not built there, its commit message
must say so.

## clang-tidy

CI runs clang-tidy (`docs/BUILDING.md`, "CI") with the two checks that
`.clang-tidy` names, and fails on either of them.

### `misc-use-internal-linkage`

- A function or variable used only in its own file goes in that file's
  anonymous namespace, alongside the file's other helpers.
- A function or variable that another file uses is declared in a header that
  both files include. For a module's own files, that header is an
  `_internal.hpp`.
- Write a `thread_local` inside a function as `static thread_local`. Without
  `static`, clang-tidy 20 treats it as a variable with linkage.

### `bugprone-unchecked-optional-access`

- Read an optional only where the function itself shows that it holds a
  value: `if (const auto x = f())`, an early return, `value_or`, or a
  comparison with the optional itself (`key != list.admin`).
- The check does not follow a test made in another function. Where the value
  is known from such a test (for example, a list already filtered to entries
  with an id), say so on the line:
  `// NOLINTNEXTLINE(bugprone-unchecked-optional-access): <why it holds one>`.
- The check does not run in `tests/` (`tests/.clang-tidy`). A test reads an
  optional after `REQUIRE(x)`, and the check cannot see that `REQUIRE` ends
  the test.

## Names

- Library slides are called Freeform: the tables `freeform_folders` and
  `freeform_items`, the type `FreeformRepository`, and the remote commands
  `playFreeform` and `addFreeformToService`.
- C++ functions are `snake_case` everywhere (core, render, app, remote).
- `camelCase` is only for names that QML or Qt sees: `Q_INVOKABLE`s, slots,
  signals, `Q_PROPERTY` accessors, and overrides of Qt's own functions
  (`rowCount`, `requestImage`).
- Names on the remote's wire (`currentTitle`, `playBibleRef`) belong to the
  protocol, not to C++, and keep their spelling.
- A `Q_INVOKABLE` or `Q_PROPERTY` exists because QML uses it. One that only
  C++ calls (the remote's bridge, another controller, a test) is a plain
  `snake_case` function, as `ThemeController`'s functions for the slide editor
  are. One that nothing calls is deleted. `tools/qml_api_unused.py`, which CI
  runs, enforces this. Its allow-list is for a test that must reach a name
  through the meta-object.
