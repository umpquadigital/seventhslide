# Freeform files and service packages

This document specifies the files SeventhSlide writes when Freeform items or
a whole service are exported from a library, and what it accepts when such a
file is imported.

The implementation is in `src/core/exchange`, with its headers in
`src/core/include/seventhslide/exchange`. `bundle.hpp` defines the format,
`adapt.hpp` defines how a document changes on its way out of one library and
into another, and `bundle_io.hpp` writes and opens the files. All three are
built on `util::ZipWriter` and `util::ZipReader`. Any change to the format
must update this document, and must increase `format_version` if an older
reader could no longer read the result.

## Overview

There are two kinds of file:

- A **Freeform file** holds one or more Freeform items, together with the
  pictures, videos and fonts they use. SeventhSlide names an export of a
  single item `.ssff`, and an export of several items, or of a folder,
  `.ssfb`. Both extensions use the same format, and a reader accepts either,
  whatever it holds.
- A **service package** (`.zip`) holds a Freeform file that includes the
  service's rundown, plus the service's own pictures, videos and audio files.
  See [The service package](#the-service-package).

## The container

A Freeform file is a ZIP archive in which every entry is stored, without
compression or encryption. The archive uses classic ZIP structures until an
entry or the archive itself exceeds 4 GiB, and ZIP64 structures from that
point on.

SeventhSlide chooses every entry name itself. No name is ever derived from a
title.

| Entry | Contents |
| --- | --- |
| `manifest.json` | A description of what the file holds (see [manifest.json](#manifestjson)). Written first. |
| `service.json` | A service's rundown, present when `has_rundown` is true (see [service.json](#servicejson)). |
| `items/0001.json` … | One Freeform document per entry, at the schema version given by `freeform_schema_version`. |
| `previews/0001.png` … | The first slide of the matching item, 480 px wide. Optional. |
| `media/<sha256>.<ext>` | Each picture and video the items use, stored once. |
| `fonts/<sha256>.<ext>` | Each font face the items use, stored once. |

### File references in documents

Inside the documents, every file reference names the archive entry that
holds the file, for example `media/ab12….jpg`. This applies to an image
object's `filename`, a video's file and poster, a picture fill and a slide
background. A library's own file names mean nothing in another library, so
they are never written. A reference to a file that the exporting library did
not have is written as empty.

### Decks

A document is written with decks only for outputs the exporting library
still has. A deck for an output the library no longer has is never shown, so
it is left out.

## manifest.json

```json
{
  "format": "seventhslide.freeform-bundle",
  "format_version": 1,
  "freeform_schema_version": 4,
  "made_by": "SeventhSlide 0.1.0",
  "made_at": "2026-11-26T14:03:00Z",
  "source_library": "<library_uid>",
  "outputs": [{ "id": "9f3c…", "name": "Main", "width": 1920, "height": 1080 }],
  "folders": [{ "key": "f12", "name": "Easter", "parent": null }],
  "items": [{ "entry": "items/0001.json", "preview": "previews/0001.png",
              "name": "Welcome", "folder": "f12", "tags": [] }],
  "media": [{ "entry": "media/ab12….jpg", "kind": "picture", "title": "Sunrise",
              "size": 482113, "sha256": "ab12…" }],
  "fonts": [{ "entry": "fonts/cd34….ttf", "family": "Lato", "style": "Bold",
              "bold": true, "italic": false, "variable": false,
              "size": 61012, "sha256": "cd34…" }],
  "not_included": { "media": ["Lost picture"], "fonts": ["Gotham"] },
  "has_rundown": false
}
```

### Top-level fields

| Key | Type | Description |
| --- | --- | --- |
| `format` | string | Always `seventhslide.freeform-bundle`. |
| `format_version` | integer | The container version. The current version is 1. |
| `freeform_schema_version` | integer | The Freeform schema version of every document in `items/`. |
| `made_by` | string | The application and version that wrote the file. |
| `made_at` | string | When the file was written, in UTC, as `yyyy-MM-ddTHH:mm:ssZ`. |
| `source_library` | string | The `library_uid` of the library the file was exported from. |
| `outputs` | array | The outputs the documents have decks for. |
| `folders` | array | The Freeform folders chosen for export. |
| `items` | array | The items in the file. |
| `media` | array | The pictures and videos in the file. |
| `fonts` | array | The font faces in the file. |
| `not_included` | object | What the documents name but the file does not contain. |
| `has_rundown` | boolean | True when the file contains `service.json`. |

### outputs

Each output that the documents have decks for, as the exporting library had
it. An import matches outputs by the shape of this canvas.

| Key | Type | Description |
| --- | --- | --- |
| `id` | string | The output's id in the exporting library. |
| `name` | string | The output's name. |
| `width`, `height` | integer | The output's canvas size in pixels. |

### folders

| Key | Type | Description |
| --- | --- | --- |
| `key` | string | A key that identifies the folder within this file only. |
| `name` | string | The folder's name. |
| `parent` | string or null | The `key` of the parent folder, or null for a top-level folder. |

### items

| Key | Type | Description |
| --- | --- | --- |
| `entry` | string | The item's document entry, such as `items/0001.json`. |
| `preview` | string | The item's preview entry, such as `previews/0001.png`. Omitted when the item has no preview. |
| `name` | string | The item's name. |
| `folder` | string or null | The `key` of the item's folder, or null for the top level. |
| `tags` | array of strings | The item's tags. |

### media

| Key | Type | Description |
| --- | --- | --- |
| `entry` | string | The file's entry, such as `media/ab12….jpg`. |
| `kind` | string | `picture` or `video`. |
| `title` | string | The title the exporting library gave the file. For a file of a Freeform item's own, this is the name it was brought in as. |
| `size` | integer | The file's size in bytes. |
| `sha256` | string | The file's SHA-256 hash, in lowercase hexadecimal. |

### fonts

The file contains every face the exporting library had of each font family
the documents name. A document names a family in a text box's `font_family`,
in a verse number's font, or with `<font=…>` in text.

| Key | Type | Description |
| --- | --- | --- |
| `entry` | string | The face's entry, such as `fonts/cd34….ttf`. |
| `family` | string | The font family. |
| `style` | string | The face's style name, such as `Bold`. |
| `bold`, `italic` | boolean | Whether the face is bold or italic. |
| `variable` | boolean | Whether the face is a variable font. |
| `size` | integer | The file's size in bytes. |
| `sha256` | string | The file's SHA-256 hash, in lowercase hexadecimal. |

### not_included

| Key | Type | Description |
| --- | --- | --- |
| `media` | array of strings | Titles of pictures and videos the documents name that the exporting library had no file for, or had a file for that it could not read. |
| `fonts` | array of strings | Families the documents name that the exporting library had no face of, or none of whose faces it could read. |

## service.json

A Freeform file contains `service.json` when it was written as part of a
service package. It holds the service's rundown.

```json
{
  "name": "Sunday 30 November",
  "tags": ["Communion"],
  "service_at": "2026-11-30T10:00:00",
  "audio_fades": {},
  "rows": [
    { "kind": "song", "title": "Amazing Grace", "item": "items/0001.json",
      "stack": { "id": "lyrics", "name": "Lyrics" }, "also_play": [4] },
    { "kind": "divider", "title": "Sermon" },
    { "kind": "web", "title": "Giving",
      "data": { "name": "Giving", "url": "https://…", "width": 1920, "height": 1080, "zoom": 1 } },
    { "kind": "live", "title": "Stream",
      "data": { "kind": "stream", "name": "Stream", "url": "srt://host:9000?passphrase=…" } },
    { "kind": "video", "title": "Welcome loop", "files": ["Media/05 Welcome loop.mp4"],
      "data": { "autoplay": true, "loop": true } },
    { "kind": "image_folder", "title": "Notices",
      "files": ["Media/06 Notices/01 Bake sale.jpg"], "data": { "folder_name": "Notices" } }
  ]
}
```

### Top-level fields

| Key | Type | Description |
| --- | --- | --- |
| `name` | string | The service's name. |
| `tags` | array of strings | The service's tags. |
| `service_at` | string | When the service is held, as `yyyy-MM-ddTHH:mm:ss`. Empty when it has no date. |
| `audio_fades` | object | How the service fades audio in and out (the General page of Service settings), exactly as the service stores it. `{}` when there are none. |
| `rows` | array | The rundown's rows, in order. |

No theme choices are carried, either for a row or for the service, because
theme ids belong to a single library.

### Rows

| Key | Type | Description |
| --- | --- | --- |
| `kind` | string | `song`, `bible`, `freeform`, `image`, `video`, `audio`, `image_folder`, `web`, `live` or `divider`. |
| `title` | string | The row's title. |
| `item` | string | For song, Bible and Freeform rows, the entry of the item the row plays. |
| `files` | array of strings | For image, video, audio and image folder rows, the row's files in the package. |
| `data` | object | Settings that depend on the row's kind (see below). Omitted when empty. |
| `stack` | object | The `id` and `name` of the stack the row played on. Omitted for the default stack. |
| `also_play` | array of integers | The zero-based positions of the other rows that go live with this one. Omitted when empty. |

The fields that apply depend on the row's kind:

- **Song, Bible and Freeform rows** name their item in `item`, which must be
  one of the manifest's items. Songs and Bible passages are converted to
  Freeform documents laid out and themed as they play from the service, using
  the service's themes and the row's own theme choices.
- **Image, video and audio rows** name at most one file in `files`. A row
  whose file was not on the exporting computer names none, and is imported
  without it.
- **Image folder rows** name each of their pictures in `files`.
- **Web page rows** carry the page's address and size in `data`, not a
  reference to the exporting library's web page.
- **Live source rows** carry the source's kind, name and address in `data`.
  A stream's address is written in full, including its key.

`data` keeps only the keys that the row's kind uses, and only when they have
the expected type. A reader drops anything else.

| Row kind | Keys in `data` |
| --- | --- |
| `video`, `audio` | `autoplay`, `loop` (booleans) |
| `image_folder` | `folder_name` (string) |
| `web` | `name`, `url` (strings); `width`, `height` (integers); `zoom` (number) |
| `live` | `kind`, `name`, `url` (strings) |
| Any other kind | None |

In `also_play`, each other row is listed once. A reader ignores a repeated
position and the row's own position, and refuses a position outside the
rundown.

## The service package

A service package is a ZIP archive of stored entries. At its top level it
holds exactly one Freeform file, named `<Service>.ssfb`, which contains
`service.json`. The service's own files are stored under `Media/`.

The Freeform file is stored whole inside the package, so it can be extracted
and imported on its own. In that case its image, video and audio rows are
imported without their files.

A reader treats an archive that contains `manifest.json` as a Freeform file,
and any other archive as a package. It refuses a package that does not have
exactly one `.ssfb` at its top level.

## Reading a file

A reader treats every file as untrusted. It opens only the entries that the
manifest names, and it extracts each one under a name it chooses itself,
never a name taken from the archive.

### The archive

A reader refuses a file that has any of the following:

- an entry that is deflated or encrypted
- two entries with the same name
- entries that overlap
- a local header that disagrees with the central directory

It also checks each entry's CRC when it reads the entry, and refuses an entry
whose CRC does not match its contents.

### Limits

| Limit | Value | When exceeded |
| --- | --- | --- |
| Size of `manifest.json` or `service.json` | 4 MiB | The file is refused. |
| JSON nesting depth in `manifest.json` or `service.json` | 200 levels | The file is refused. |
| JSON nesting depth in a document | 200 levels | That item is refused; the others are imported. |
| All documents together | 128 MiB | The file is refused. |
| Entries in the archive | 65,534 | The file is refused. |
| Size of the central directory | 64 MiB | The file is refused. |
| Items, media files, fonts, folders or rows | 20,000 | The file is refused. |
| A single document | 12 MiB (the largest library record) | That item is refused; the others are imported. |
| A name or title | 400 bytes | The text is cut to fit. |
| A tag, font name or output name | 200 bytes | The text is cut to fit. |
| A string in a row's `data` | 4096 bytes | The value is dropped. |
| A preview | 4 MiB | The item is treated as having no preview. |

### Content

A reader refuses the whole file when:

- its `format_version` is later than the reader's
- its documents are at any Freeform schema version other than the reader's
  own, whether older or newer
- it holds no items and no rundown
- its folders do not nest correctly, because a `parent` is not a folder in
  the file or the folders form a loop
- a media or font entry is missing, or its size differs from the size in
  the manifest
- `has_rundown` is true but there is no `service.json`
- a rundown row has an unknown kind

A reader refuses a single item, and imports the rest, when that item's
document is missing, is larger than 12 MiB, or is damaged.

### Checks on import

When a file is imported, each font is checked in the same way that
Settings > Fonts checks a font. A picture, video or audio file whose
extension is not one its kind allows, or which is larger than its kind's
size limit, is left out, and the import reports it in a note.

## What an import changes

An import changes nothing that the library already has. Each imported item
is added as a new item.

- **Files.** The pictures and videos an item names become that item's own
  files, in a folder of its own. They are never added to the library's images
  or videos. If the same bytes appear twice in one file, they are imported
  once. A service package's own image, video and audio rows are added to the
  library, and there a file whose bytes the library already holds is reused
  as it is. Files are compared by size first, then by SHA-256.
- **Fonts.** A font face is added only when the library has no face in the
  same slot, meaning the same family, bold and italic.
- **Decks.** A deck goes only to an output with the same aspect ratio as the
  deck's output in the file, and each library output receives at most one
  of an item's decks. The deck is fitted to that output's canvas.
- **Services.** A service whose rows need no output, such as pictures,
  videos and web pages, is imported whether or not any of the file's outputs
  match the library's.
