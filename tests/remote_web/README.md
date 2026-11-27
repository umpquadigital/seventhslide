# Web remote browser tests

The web remote (`src/remote/web`) tested the way a phone uses it: the real
app, serving a real library, driven through a real browser (Chromium, by
Playwright). The C++ tests cover the server and the bridge; these cover the
client and the whole path through it.

## Running them

Once, here:

    npm ci
    npx playwright install chromium

Then, after building the project:

    npx playwright test                      # everything
    npx playwright test specs/stacks.spec.mjs
    npx playwright test -g "double-click"    # by name
    npx playwright test --repeat-each 4      # look for flakiness

They test the build in `../../build` (`SEVENTHSLIDE_BUILD_DIR` for another),
using its `bin/seventhslide` and `bin/seventhslide_tests_remote`. The app serves
the client from its resources, so a change to it needs a rebuild before the
tests see it.

`ctest` runs them too, as `remote_web_browser` (label `browser`; `ctest -LE
browser` leaves them out), and skips them where they are not installed. CI
installs them.

`npm run lint` runs ESLint over the client and these tests (`ctest` runs it
as `remote_web_lint`). It finds a name used but never declared or imported,
which the browser reports only when that line runs. Before ESLint, and
without anything installed, it checks what no browser reports at all:

- `commands.mjs`: every protocol command is sent by the client or has a
  bridge test, and `docs/REMOTE_PROTOCOL.md` marks exactly those the
  client does not send;
- `glyphs.mjs`: every glyph the client names is in `src/app/glyphs.cpp`;
- `markup.mjs`: every class a stylesheet styles is used, and every id a
  module looks up exists.

A failed test keeps a trace, a screenshot and the app's log under
`test-results/`; `npx playwright show-trace <trace.zip>` replays it.

## How a test runs

Every test has an app and a library of its own, so tests never see each
other's changes and any number can run at once:

1. The test binary builds a fresh library (`tests/remote_web_fixture.cpp`,
   hidden tag `[.remote_web]`): the sample library the UI shots use
   (`tests/sample_library.hpp`) plus a playable video and audio file, a
   "Media" service, a three-slide Freeform deck, a second stack ("Lower
   thirds", which the reading in "Sunday Service" plays on), and the remote
   on a free port with PIN 2468. It writes `fixture.json`, what the library
   holds (ids, the output, the stack), for the test to read as
   `app.fixture`.
2. The app starts on it offscreen, with its own config, cache and data
   directories, and with no sound (no sound server or ALSA to reach, so
   the Audio Mixer runs without a device, which still keeps time).
3. The test drives the page; the app stops when it ends. A test fails if
   the app exits under it or the page throws.

The app opens no service when it starts: a test opens the one it needs.

## Pictures for review

`shots/` takes pictures of the remote in its main states at a desktop's, a
tablet's and a phone's size, to look at beside the app's own
(`tests/ui_shots.cpp`) when a change alters how the remote looks. They are
not compared by pixel and do not run with the specs:

    SEVENTHSLIDE_REMOTE_SHOTS_DIR=out npx playwright test --project=shots

## Writing a test

`harness/test.mjs` gives each test:

- `app`: the running app (`app.url`, `app.fixture`, `app.restart()`).
  `test.use({ library: { songs: 250 } })` gives it a library with that many
  more songs (a list longer than a page).
- `remote`: the page, signed in (`remotePage` before signing in). It is a
  `RemotePage` (`harness/remote-page.mjs`): what is where on the page and
  what a person does with it. Specs go through it, so a change to the
  client's markup is fixed there once. `remote.commands(name)` lists the
  commands the page sent.
- `observer`: a second client speaking the protocol
  (`docs/REMOTE_PROTOCOL.md`), to check what the server did apart from what
  the page shows (`observer.waitFor(state => ..., 'what')`) and to set a
  test up (`remote.openServiceFrom(observer, ...)`). It is a device of its
  own, with its own stack tab.

Assert on what someone sees, and on the server's state through the
observer. Wait for things with Playwright's `expect` and `observer.waitFor`,
never a fixed sleep; the only sleeps space out what a test does (two
clicks that are not a double-click) or give what must not happen time to
(a reconnect that was cancelled).

## Network changes

Chromium drops whatever is loading when the machine's network changes (a
VPN connecting, Docker starting a container): `net::ERR_NETWORK_CHANGED`.
The page is loaded a second time when that happens while it loads, and a
test where it happened later says so in its annotations. A machine whose
network keeps changing makes the tests fail; one that is quiet does not.
