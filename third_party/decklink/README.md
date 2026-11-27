# Blackmagic DeckLink SDK (vendored)

Build-time only. SeventhSlide links **no** Blackmagic binary.

## What this is

The DeckLink API headers plus `DeckLinkAPIDispatch.cpp`, the SDK's own runtime
loader. The dispatch `dlopen`s `libDeckLinkAPI.so` and returns `NULL` from the
factory when it is absent, which is what lets the Blackmagic destination type
grey itself out on a machine with no Desktop Video installed. The shipped
binary depends on `-ldl` and nothing else — same arrangement as the NDI path
(`src/render/ndi_runtime.cpp`), except that NDI needs a hand-written `dlsym`
loader and DeckLink ships one.

The **driver** (Desktop Video) is proprietary, is not redistributable, and is
not here. The operator installs it themselves from blackmagicdesign.com/support.

## Licence

Every file carries a per-file grant that is the Boost Software Licence 1.0
verbatim — use, reproduce, distribute, and prepare derivative works, provided
the notice is retained in *source* copies. It explicitly does not require
attribution in binaries ("solely in the form of machine-executable object
code"). There is no separate EULA in the SDK; the per-file header is the
licence.

**Do not edit these files.** The notices must stay intact. Upgrading means
replacing them wholesale from a newer SDK.

## Provenance and version

    DeckLink SDK 10.11.2 (BLACKMAGIC_DECKLINK_API_VERSION 0x0a0b0200)

Taken from the copy Vizrt redistributes inside the NDI SDK, at
`/usr/share/doc/ndi-sdk-devel/examples/C++/NDIlib_Send_BMD/BMDSDK/Linux/include`.

### This should be upgraded

10.11.2 dates from 2018 and was vendored because it was on hand and verified
working; the current SDK is behind a registration form at
blackmagicdesign.com/support. It drives a modern driver fine — it was tested
against Desktop Video reporting API 16.4.0, an eight-year forward-compat span —
but a current SDK brings newer cards, 12G/8K modes, and current keyer
attributes.

Two known breaks when upgrading past SDK 11.x. Both are deliberately isolated
to one call site each in `src/render/bmd_runtime.cpp` so the upgrade is small:

  * `IDeckLinkAttributes` was renamed `IDeckLinkProfileAttributes` in 11.x.
    See `query_attributes()`.
  * `DoesSupportVideoMode` changed signature: 10.11.2 takes
    `BMDDisplayModeSupport*`; 11.x+ takes a `bool*` plus extra parameters.
    Used by the sink, not the runtime probe.

## macOS

`mac/` holds the macOS headers and dispatch from DeckLink SDK 10.11.4
(BLACKMAGIC_DECKLINK_API_VERSION 0x0a0b0400), the same API as the Linux
10.11.2 copy: the interfaces and IIDs match, 10.11.4 only adds
`IID_IDeckLinkHDMIInputEDID`. Taken from OBS Studio's vendored copy,
`plugins/decklink/mac/decklink-sdk` at commit 39c9bc6823ed ("decklink: Update
SDK to 10.11.4"), unmodified. Only the files `DeckLinkAPI.h` includes are kept.

The macOS dispatch loads `/Library/Frameworks/DeckLinkAPI.framework` (installed
with Desktop Video) through CFBundle, and returns `NULL` when it is missing, as
the Linux one does for `libDeckLinkAPI.so`.

Upgrade both copies together: the SDK version must match on every platform.

## Windows: not yet

Windows needs its own headers from the platform SDK; until then
`bmd_unavailable.cpp` stands in. The platforms are not drop-in equivalents:

  * String types differ per platform. `GetDisplayName` yields `const char*` on
    Linux, `CFStringRef` on macOS, `BSTR` on Windows. Every one goes through
    `bmd::adopt_string()` (`src/render/bmd_internal.hpp`), whose argument type
    is `bmd::SdkString`.
  * Windows needs no dispatch file at all — it is real COM, reached through
    `CoCreateInstance` with the CLSID/IID from the headers.
