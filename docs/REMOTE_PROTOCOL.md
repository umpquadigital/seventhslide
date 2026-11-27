# Web remote protocol

This document describes the protocol that SeventhSlide's web remote speaks:
what a client may send and what it receives in return. It applies to the
bundled browser client (`src/remote/web`) and to any third-party client, such
as a Stream Deck or Companion module or a script. Every command is listed
here, including those the bundled client never sends. Nothing on the console
can be reached through the remote except what this document describes.

## Overview

The current protocol version is **6**. The server reports it in the
`protocol` field of the WebSocket `hello` message and of `GET /api/hello`.

The `caps` array in the `hello` message names the capabilities the server
offers: `stacks`, `freeform`, `video`, `autoAdvance`, `audio`, `web`, `live`
and `previewProgram`. The last one covers stacks with a Preview: the
`preview`, `previewLines` and `previewImages` sections, the `transition` and
`clearPreview` commands, and the `side` argument of the transport commands
and of `toggleStackBlank`.

A single port serves three things:

- the bundled web app,
- a small HTTP API under `/api/`,
- a WebSocket at `/ws`.

The HTTP API handles sign-in, paged library reads and thumbnails. Everything
stateful travels over the WebSocket: the server pushes the current state as
named sections, and the client sends commands.

## Connecting

### Port

The port is the `remote_port` setting in this computer's settings file,
`machine.json` in the app data directory. It defaults to 1844.

The server speaks plain HTTP, not HTTPS. The PIN controls who has access, but
traffic is not encrypted.

### Network restrictions

These checks apply to every route, including the web app's static files:

- The server answers only clients with loopback, link-local or private
  addresses (RFC 1918, CGNAT, and IPv6 unique-local). Any other address gets
  `403`.
- If the request has a `Host` header, its host must be an IP address,
  `localhost`, a `.local` name, or this computer's own host name. Any other
  host gets `403`.
- A WebSocket upgrade request that carries an `Origin` header is refused
  unless the origin's host and port match those in the `Host` header.

### Authentication

When no PIN is set, everything is open and no token is needed.
`POST /api/auth` then answers `{"token": "", "open": true}`.

When a PIN is set, the client signs in by sending the PIN to `POST /api/auth`:

```json
{"pin": "1234"}
```

The server answers with a token and also sets it in the `ss_remote` cookie:

```json
{"token": "...", "open": false}
```

The client can present the token in any of these ways:

- an `Authorization: Bearer <token>` header,
- the `ss_remote` cookie,
- a `token` query parameter (`?token=<token>`). This exists for the
  WebSocket, because the browser's `WebSocket` constructor cannot set
  headers.

A token is valid for 12 hours. Changing the PIN invalidates every token
immediately.

A wrong PIN gets `401`. Failed attempts are rate-limited per client address.
The first five failures are free. After that, the address is locked out for a
period that starts at 1 second and doubles with each further failure, up to 15
minutes. While locked out, `POST /api/auth` answers `429` with a `retryAfter`
field giving the number of seconds to wait. A request body larger than 64 KiB
is refused with `413` without being parsed.

An open WebSocket is closed when the token it was opened with ends, either by
expiring or because the PIN changed. If the client then reconnects and is
refused, it should call `GET /api/hello` and ask the user for the PIN again.
Removing the PIN altogether leaves open sockets open.

### HTTP errors

When the server refuses an HTTP request, the response body is
`{"error": "<reason>"}`. The status codes are:

| Status | Meaning |
|---|---|
| `401` | No valid token was presented, or the PIN was wrong. |
| `403` | The client's address or `Host` is not one the server answers. |
| `404` | Nothing exists at that path. |
| `413` | The `POST /api/auth` body is over 64 KiB. |
| `429` | Too many wrong PINs; see `retryAfter`. |
| `500` | The request failed on the server. |
| `503` | Too many thumbnails are waiting to be drawn; see [Thumbnails](#thumbnails). |

## HTTP endpoints

Every endpoint except `GET /api/hello` and `POST /api/auth` requires a token
when a PIN is set.

### `GET /api/hello`

Needs no token. It answers:

```
{"protocol": 6, "needsPin": bool, "authed": bool}
```

| Field | Meaning |
|---|---|
| `protocol` | The protocol version. |
| `needsPin` | A PIN is set. |
| `authed` | The request is authorized: either no PIN is set, or it carried a valid token. |

### `POST /api/auth`

Exchanges the PIN for a token. See [Authentication](#authentication).

### `GET /api/library/<kind>`

Reads one page of a library list.

Query parameters: `q`, `folderId`, `chapter`, `offset`, `limit`.

The response is:

```
{"total": n, "offset": n, "rows": [...]}
```

`total` counts every matching row, not just those on the page. `limit` is 1
to 200 and defaults to 50.

`q` is applied to the whole list before the page is cut, so it searches every
row, not just the current page. It may be form-encoded, with `+` for a space,
as a browser's `URLSearchParams` writes it.

The kinds and their rows:

| Kind | Parameters | Row fields |
|---|---|---|
| `songs` | `q` | `id`, `title`, `authors`, and `matchedLine` (see below) |
| `freeform` | `folderId` | Folders: `folderId`, `name`. Items: `id`, `name`. |
| `bibles` | | `id`, `name` |
| `bible-books` | `folderId` is the bible's id | `book` |
| `bible-chapters` | `folderId` is the bible's id; `q` is the book | `chapter` |
| `bible-verses` | `folderId` is the bible's id; `q` is the book; `chapter` | `verse`, `text` |
| `images`, `videos`, `audio` | `folderId` | Folders: `folderId`, `name`. Files: `filename`, `title`. |
| `web` | `folderId` | Folders: `folderId`, `name`. Pages: `id`, `name`, `url`, `thumb`. |
| `live` | | `kind`, `kindLabel`, `name`, `key`, `detail`, `signal`, `live`, `url` (streams only), `thumbRev` |

For the folder-based kinds, `folderId` names the folder whose contents are
listed.

Songs are searched the same way the console's song list searches them: by
title, authors, songbook, topics, key and lyrics. They come back in the
console's order. `matchedLine` is the lyric line that matched, and is present
only when a song was found by its lyrics and by no other field.

For `web` pages, `thumb` identifies the page's current still. Pass it as `r`
when requesting the page's thumbnail.

The `live` kind lists the live sources found right now and the saved streams.
`signal` is `ok`, `lost` or empty. `url` is present for streams. Pass
`thumbRev` as `r` when requesting the sources' thumbnails. The server searches
for live sources only while a client wants them: each read of `live` keeps
the search running for 5 seconds, so a client that displays the list should
read it again within that time.

An unknown kind answers `{"error": "...", "rows": [], "total": 0}`.

### `GET /api/services`

The bundled client does not use this endpoint. It lists service tags and
services for a picker.

Query parameters: `tag`, `q`.

```
{
  "tag": "...",
  "tags": [{"name": ..., "count": ...}],
  "services": [{"id": ..., "name": ..., "serviceAt": ..., "createdAt": ..., "tags": [...], "isOpen": bool}]
}
```

- Without `tag`, the response lists every service and every tag. A `q`
  filters both lists.
- With `tag`, the response lists the services that carry that tag, and no
  tags.
- A `q` searches every service's name and tags, whatever the `tag`.

### Thumbnails

| Endpoint | Returns |
|---|---|
| `GET /api/preview/<outputId>?w=&slide=&stack=&side=&r=` | A picture of an output (PNG). |
| `GET /api/thumb/<images\|videos>/<filename>?w=` | A library media thumbnail (JPEG). |
| `GET /api/freeform-thumb/<id>?w=` | The first slide of a Freeform item (PNG). |
| `GET /api/web-thumb/<id>?w=&r=` | The still of a web page that the library keeps (JPEG). `404` until one exists. |
| `GET /api/live-thumb/<key>?w=&r=` | The still of a live source, by its `key` (JPEG). `404` until one exists. |

For `/api/preview/<outputId>`:

- Without `slide`, the picture shows what the output shows now.
- With `slide=N`, the picture is slide N of the deck on stack `stack`. If no
  `stack` is named, the console's tab is used.
- With `side=preview`, the picture is of that stack's Preview: slide N, or
  its current slide when no `slide` is given.

`w` is the requested width. It defaults to 320 and is rounded up to 160, 320
or 640. Requests wider than 640 get 640.

`r` is a revision. A thumbnail requested with `r` is sent with
`Cache-Control: private, max-age=31536000, immutable`. Put whatever
identifies the picture in `r`, such as a slide's `lines.build` or a moving
picture's `previewRevision`, and the client never needs to fetch the same
picture twice. Include `hello.boot` in `r` as well, because build ids and
revisions start again from 1 when the app restarts.

Output pictures and Freeform thumbnails that are not already cached are drawn
on a budget. The server draws at most 32 at once, then one more every 25 ms.
The rest wait their turn, and the first requested is drawn first. When more
than 512 are waiting, the server answers `503` with `Retry-After: 1`.

## The WebSocket

### Opening the connection

Connect to `/ws`. To return a device to the tab it had before, add
`?tab=<stackId>` (see [Stack tabs](#stack-tabs)). When a PIN is set, also add
`?token=<token>` unless the cookie is sent.

The server then sends two messages, in this order:

1. `hello`:

   ```
   {"type": "hello", "protocol": 6, "boot": "<token>", "caps": [...]}
   ```

   `boot` identifies this run of the server. See [Thumbnails](#thumbnails)
   for its use.

2. `snapshot`, holding every section:

   ```
   {"type": "snapshot", "rev": n, "state": {<every section>}}
   ```

After that, whenever something the device can see changes, the server sends
a `patch` holding only the sections that changed:

```
{"type": "patch", "rev": n, "sections": {<only the sections that changed>}}
```

Each section in a patch replaces the client's copy of that section entirely.
Changes are coalesced over 50 ms, so several changes in quick succession
arrive as one patch.

### Client messages

The client sends two kinds of message.

`cmd` runs a command:

```
{"type": "cmd", "id": "<any string>", "name": "<command>", "args": {...}}
```

The server answers each command with an `ack` carrying the same `id`. On
success:

```
{"type": "ack", "id": "...", "ok": true}
```

Commands that answer a question also include a `data` field. On failure:

```
{"type": "ack", "id": "...", "ok": false, "error": "<reason>"}
```

The arguments are checked against the command tables below before anything
runs. A missing required argument, an argument of the wrong JSON type, or an
unknown argument key is refused. An argument whose value is `null` counts as
not sent, and `args` may be left out for a command that takes no arguments.

`resync` asks the server to send a full snapshot again, for example after the
page was hidden:

```json
{"type": "resync"}
```

### Errors and limits

A malformed message, or one whose `type` is neither `cmd` nor `resync`, gets
an `error` message:

```
{"type": "error", "text": "..."}
```

A message or frame larger than 64 KiB closes the socket with close code 1009.

### Stack tabs

Each connected device has its own stack tab, separate from the console's and
from every other device's. A device starts on the console's tab, or on the
stack named by `?tab=` when it connected.

`setFocusedStackId` changes only this device's tab. Every other command runs
against the device's tab. Sending content live to another stack moves the
device's tab to that stack.

The sections a device receives describe its own tab. If the tab is left with
nothing live or cued (because its stack was cleared), the sections describe
the frontmost stack that still has something, as the console does after a
clear.

### Previews

A stack may have a Preview. This is a setting of this computer, made on the
console. Content sent to a stack with a Preview lands in the Preview, cued,
instead of on the outputs, and waits there to be taken.

Content sent from the remote lands where it would if sent from the console:
on a stack with a Preview, the `play...` commands, `selectServiceItem` and
`selectServiceItemAtImage` cue their content in the Preview.

`transition` takes what is cued live, at the position it was cued at. What was
live goes back to the Preview, at the position it had reached.

Each of the `preview`, `previewLines` and `previewImages` sections mirrors a
Program section (`session`, `lines` and `images`) for the tab's Preview. They
use the same field names wherever a field describes one side, so a client can
draw a Preview with the same code that draws the Program.

## Sections

The state is divided into named sections. A snapshot holds all of them; a
patch holds those that changed.

| Section | Contents |
|---|---|
| `session` | The device's tab and what is live on it. |
| `stacks` | Every stack, and which ones hold something. |
| `service` | The open service's rundown. |
| `lines` | The line map of what is live on the tab. |
| `outputs` | The outputs that are on, and their blank and freeze state. |
| `images` | The image playlist live on the tab. |
| `advance` | Each stack's auto-advance settings. |
| `preview` | The tab's Preview and what is cued in it. |
| `previewLines` | The line map of what is cued in the Preview. |
| `previewImages` | The image playlist cued in the Preview. |

### `session`

Describes the device's tab.

| Field | Meaning |
|---|---|
| `mode` | What is live: `song`, `bible`, `service`, `freeform`, `video`, `audio`, `image`, `web`, `live` or `empty`. |
| `hasBuild` | Slides have been built for what is live on a stack. |
| `lineCursor` | The current line. |
| `totalLines` | The number of lines. |
| `stepCursor` | The live slide's current build step, counted from 0. 0 for a slide without builds. |
| `totalSteps` | The number of build steps on the live slide. 1 for a slide without builds. |
| `globalBlank` | Every output that is not exempt is blanked. |
| `globalFreeze` | Every output that is not exempt is frozen. |
| `serviceItemIndex` | The live rundown row, or `-1` for none. |
| `currentTitle` | The title of what is live. |
| `openServiceId` | The open service's id. |
| `openServiceName` | The open service's name. |
| `primaryOutputId` | The primary output's id. Outputs are named by id in thumbnail URLs and in the output commands. |
| `primaryOutputName` | The primary output's name, for display. |
| `imageMode` | An image playlist is live. |
| `videoMode` | A video is live. |
| `mediaTransport` | Video or audio is live on this tab, playing or paused. |
| `videoPlaying` | The video or audio is playing. |
| `videoPositionSec` | The playback position, in seconds. |
| `videoDurationSec` | The length, in seconds. |
| `currentImageFilename` | The file of the image playlist's current picture. |
| `freeformGallery` | A Freeform item is live that is shown as a gallery of slides. |
| `webMode` | A web page is live on this tab. |
| `webUrl` | The address the page is on now. |
| `webLoading` | The page is still loading. |
| `webProblem` | Why the page is not showing. Empty when it shows. |
| `webCanGoBack` | There is a previous page for `webBack` to go to. |
| `liveSourceMode` | A live source is live. |
| `liveSourceProblem` | Why the live source is not showing. |
| `liveSourceLost` | The live source's picture has stopped arriving. |
| `advancingStackIds` | The stacks whose auto-advance is running. |
| `outputSlides` | The slide index each output is showing. |
| `previewRevision` | Present only while a video, a web page or a live source shows. It changes with the picture, at most four times a second. |

Despite their names, the `video...` transport fields also cover audio. The
playback position (`videoPositionSec`) is sent at most four times a second.

### `stacks`

| Field | Meaning |
|---|---|
| `defs` | Every stack. Each has `id`, `name`, `icon`, `preview` (the stack has a Preview), and the stack's icon as inline SVG in `iconUrl`, `iconUrlIdle` and `iconUrlActive`. |
| `occupied` | The stacks with something live or cued. Each has `id`, `name`, `icon`, the three icon URLs, `blank`, `frozen`, `live`, `cued` and `previewBlank`. |
| `focusedStackId` | This device's tab. |
| `armedSendToId` | The stack armed by `armSendTo`. |
| `showStackTabs` | More than one stack is in `occupied`. |
| `takeFadeMs` | How long this computer's Fade takes, in milliseconds. `setTakeFade` sets it. |

### `service`

`items` is the open rundown. Each item has these fields:

| Field | Meaning |
|---|---|
| `title` | The item's title. |
| `itemType` | The item's type. |
| `itemIndex` | The item's position in the rundown. |
| `itemId` | The item's id. |
| `selected` | The item is live. |
| `cued` | The item is cued in the tab's Preview. |
| `hasOverrides` | The item has overrides. |
| `displayNumber` | The item's number as displayed: counted from 1 among the items that are not dividers, and 0 for a divider. |
| `folderImages` | For an image folder item, its file names. Empty for other items. |
| `imageFile` | For an image item, the file whose thumbnail represents it. Empty for other items. |
| `stackId` | The stack the item plays on. |
| `stackIcon` | That stack's icon. |
| `alsoPlayIds` | The ids of the items that go live with this one (its Also play set). |
| `alsoPlayWith` | The title of the item whose Also play set includes this one. Empty when there is none. |

### `lines`

The tab's line map.

| Field | Meaning |
|---|---|
| `stackId` | The stack whose deck this is. |
| `build` | Which build of the deck this is. |
| `rows` | The lines. Each has `lineIndex`, `text` and `label`. |
| `slideMaps` | For each output, a map from line to slide. |
| `nextChange` | For each line, the line that Next goes to. |
| `prevChange` | For each line, the line that Previous goes to. |

### `outputs`

`list` holds each output that is on, with `id`, `name`, `isBlank` and
`isFrozen`. The section is status only. An output that is switched off in
Settings is left out here, and also from `outputSlides` and `slideMaps`.

### `images`

The image playlist live on the tab. When none is live, `active` is `false`,
`images` is empty, `index` is `-1` and `folderName` is left out.

| Field | Meaning |
|---|---|
| `active` | An image playlist is live. |
| `folderName` | The folder's name. |
| `images` | The file names. |
| `index` | The current picture. |

### `advance`

`byStack` holds the auto-advance settings of each stack, keyed by stack id:
`enabled`, `intervalSec`, `loop`, `through` and `stopAtDividers`.

### `preview`

The tab's Preview.

| Field | Meaning |
|---|---|
| `stackId` | The stack this Preview belongs to. Use it as the `stackId` of `transition` and `clearPreview`, because this section may arrive before `stacks` reports that the tab moved. |
| `available` | The stack has a Preview. |
| `occupied` | Something is cued. |
| `hasBuild` | Slides have been built for what is cued. |
| `blank` | What is cued will go live blanked. |

It also carries the `session` fields that describe one side, for what is
cued: `lineCursor`, `totalLines`, `stepCursor`, `totalSteps`,
`serviceItemIndex` (the cued row), `currentTitle`, `imageMode`, `videoMode`,
`mediaTransport`, `videoPlaying`, `videoPositionSec`, `videoDurationSec`,
`currentImageFilename`, `freeformGallery`, `webMode`, `webUrl`, `webLoading`,
`webProblem`, `webCanGoBack`, `liveSourceMode`, `liveSourceProblem`,
`liveSourceLost` and `outputSlides`. `previewRevision` is present while a
video, a web page or a live source is cued.

### `previewLines`

The Preview's line map, with the same fields as `lines`.

### `previewImages`

The Preview's image playlist, with the same fields as `images`.

## Commands

Each table lists a command's arguments and its sections. The sections are
those a successful command marks as changed; a patch carrying them follows
the `ack`. "all" means every section.

Commands marked *not sent by the bundled client* are kept for other clients.

### Moving through what is live

| Command | Arguments | Sections |
|---|---|---|
| `nextSlide` | optional `side` string | session, lines, images, preview, previewImages |
| `prevSlide` | optional `side` string | session, lines, images, preview, previewImages |
| `jumpToLine` | `lineIndex` int, optional `side` string | session, lines, images, preview, previewImages |
| `nextSlideOnStack` *(not sent by the bundled client)* | `stackId` string | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `jumpToLineOnStack` *(not sent by the bundled client)* | `stackId` string, `lineIndex` int | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `nextServiceSlide` *(not sent by the bundled client)* | | all |

`side` is `"program"` or `"preview"`. Without it, a command works on the
Program. `"preview"` works on the tab's Preview instead, and is refused on a
stack without one. `imageGoto` and the video commands also take `side`.
`webBack` and `webReload` do not, because a page cued in a Preview can only
be operated from the console.

### Blank and freeze

| Command | Arguments | Sections |
|---|---|---|
| `toggleGlobalBlank` | | session, outputs, stacks |
| `toggleGlobalFreeze` | | session, outputs, stacks |
| `toggleOutputBlank` *(not sent by the bundled client)* | `outputId` string | session, outputs, stacks |
| `toggleOutputFreeze` *(not sent by the bundled client)* | `outputId` string | session, outputs, stacks |
| `toggleStackBlank` | `stackId` string, optional `side` string | session, outputs, stacks, preview |
| `toggleStackFreeze` | `stackId` string | session, outputs, stacks |
| `clearBlankFreeze` | | session, outputs, stacks |

Blank and freeze each come in two kinds, with separate toggles:

- An output's blank or freeze applies to every layer on that output.
- A stack's blank or freeze applies to that stack's layer on every output.

Blank takes precedence over freeze. Blank hides only the picture; media keeps
playing.

`toggleGlobalBlank` and `toggleGlobalFreeze` turn the toggle on for every
output that is on and not exempt from it (in Output settings). If every one of
those outputs already has it on, they turn it off for all of them instead.

`toggleStackBlank` and `toggleStackFreeze` are refused for a stack with
nothing live on it, unless they are turning the toggle off.

With `side` set to `"preview"`, `toggleStackBlank` blanks what the stack's
Preview has cued instead. This shows on no output; the cued content goes live
blanked when it is taken, just as a blank on the live content comes back to
the Preview with it. Cueing something new, or clearing the Preview, lifts
this blank. It is refused for a stack without a Preview, or with nothing cued,
unless it is turning the blank off.

`clearBlankFreeze` lifts every blank and freeze, on every output and every
stack.

### Stacks

| Command | Arguments | Sections |
|---|---|---|
| `setFocusedStackId` | `stackId` string | all (this device's tab only) |
| `armSendTo` *(not sent by the bundled client)* | `stackId` string | stacks |
| `clearStack` | `stackId` string | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `clearAllStacks` | | all |
| `transition` | `stackId` string; optional `type` string, `durationMs` int, `direction` string, `easing` string | all |
| `clearPreview` | `stackId` string | session, lines, stacks, service, images, preview, previewLines, previewImages |
| `setTakeFade` | `durationMs` int | stacks |
| `setServiceItemStack` *(not sent by the bundled client)* | `itemId` int, `stackId` string | service, stacks |
| `setServiceItemAlsoPlay` *(not sent by the bundled client)* | `itemId` int, `companionIds` int array | service, stacks |
| `linkServiceItems` *(not sent by the bundled client)* | `itemIds` int array | service, stacks |
| `unlinkServiceItem` *(not sent by the bundled client)* | `itemId` int | service, stacks |
| `setAutoAdvance` | `stackId` string; optional `enabled` bool, `intervalSec` int (1 to 3600), `loop` bool, `through` bool, `stopAtDividers` bool | session, advance |

`transition` is the take. What is cued on stack `stackId` goes live, and what
was live goes back to the stack's Preview. A take with nothing cued is
refused.

- Without `type`, the change uses the transition of its theme.
- With `type`, the change uses the named transition: `cut`, `fade`,
  `fade_through_black`, `push`, `wipe`, `split`, `zoom`, `cover` or `reveal`.
  It lasts `durationMs` milliseconds (500 by default, at most 60000), comes
  from `direction` (`right`, `left`, `up` or `down`; `right` by default) and
  uses `easing` (`ease_in_out`, the default, or `linear`). It applies until
  the stack next moves.

A name that is not in these lists is refused. So is a `durationMs`,
`direction` or `easing` sent without a `type`.

`clearPreview` drops what is cued and leaves what is live. Both `transition`
and `clearPreview` are refused on a stack without a Preview.

`setTakeFade` sets how long this computer's Fade takes. The value is brought
into the range 100 to 10000.

### Media

| Command | Arguments | Sections |
|---|---|---|
| `imageGoto` | `index` int, optional `side` string | session, images, preview, previewImages |
| `videoPlay` | optional `side` string | session, preview |
| `videoPause` | optional `side` string | session, preview |
| `videoSeek` | `positionSec` number, optional `side` string | session, preview |
| `videoRestart` | optional `side` string | session, preview |
| `webBack` | | session |
| `webReload` | | session |

The video commands also control the tab's audio. With `side` set to
`"preview"`, they control what is cued in the tab's Preview, which is heard
only on the console's Listen output until it is taken.

`webBack` shows the page that was showing before a link was followed. It is
refused when there is no such page; `webCanGoBack` says whether there is.
`webReload` loads the page live on the tab again.

### The service rundown

| Command | Arguments | Sections |
|---|---|---|
| `openService` *(not sent by the bundled client)* | `serviceId` int | all |
| `selectServiceItem` | `index` int | all |
| `selectServiceItemAtImage` | `index` int, `imageIndex` int | all |
| `addSongToService` *(not sent by the bundled client)* | `songId` int, optional `atIndex` int | service |
| `addFreeformToService` *(not sent by the bundled client)* | `freeformId` int, optional `atIndex` int | service |
| `addBibleRefToService` *(not sent by the bundled client)* | `bibleId` int, `book` string, `chapter` int, `verseStart` int, `verseEnd` int, optional `atIndex` int | service |
| `addImageToService` *(not sent by the bundled client)* | `filename` string, optional `atIndex` int | service |
| `addVideoToService` *(not sent by the bundled client)* | `filename` string, optional `atIndex` int | service |
| `addImageFolderToService` *(not sent by the bundled client)* | `folderId` int, optional `atIndex` int | service |
| `addVideoFolderToService` *(not sent by the bundled client)* | `folderId` int, optional `atIndex` int | service |
| `addFreeformFolderToService` *(not sent by the bundled client)* | `folderId` int, optional `atIndex` int | service |
| `addDividerToService` *(not sent by the bundled client)* | optional `title` string, optional `atIndex` int | service |
| `removeServiceItem` *(not sent by the bundled client)* | `itemId` int | service, session, lines, images, preview, previewLines, previewImages |
| `moveServiceItemTo` *(not sent by the bundled client)* | `fromIndex` int, `toIndex` int | service |
| `reorderServiceItems` *(not sent by the bundled client)* | `orderedItemIds` int array | service |

### Services: rename and delete only

Creating services and tagging them can only be done on the console.

| Command | Arguments | Sections |
|---|---|---|
| `renameService` *(not sent by the bundled client)* | `serviceId` int, `name` string | session |
| `deleteService` *(not sent by the bundled client)* | `serviceId` int | all |

`deleteService` cannot be undone from the remote, and anyone who has the PIN
can send it.

### The library: play only

Nothing in the library can be changed from the remote.

| Command | Arguments | Sections |
|---|---|---|
| `playSong` | `songId` int | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `playFreeform` | `freeformId` int | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `playImage` | `filename` string | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `playImageFolder` *(not sent by the bundled client)* | `folderId` int, optional `startIndex` int | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `playVideo` | `filename` string, optional `loop` bool | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `playBibleRef` | `bibleId` int, `book` string, `chapter` int, `verseStart` int, `verseEnd` int | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `playAudio` | `filename` string, optional `loop` bool | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `playWebPage` | `pageId` int | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `playLiveSource` | `kind` string, `name` string (for a stream, its address, as the `url` field of a `live` row gives it) | session, lines, stacks, service, images, advance, preview, previewLines, previewImages |
| `resolveBibleRef` | `reference` string, `bibleId` int | none |
| `suggestBibleBooks` | `prefix` string, `bibleId` int | none |

`resolveBibleRef` returns this in `data` when the reference resolves:

```
{"success": true, "bible_id": ..., "book": ..., "chapter": ..., "verse_start": ...,
 "verse_end": ..., "ref": ..., "verses": [{"verse_num": ..., "text": ...}]}
```

When it does not resolve, `data` is `{"success": false, "message": "..."}`.

`suggestBibleBooks` returns `{"books": [...]}` in `data`, with at most 12
books.

The argument check accepts either command without a `bibleId`, but the
command itself then refuses it.
