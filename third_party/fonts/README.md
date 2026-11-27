# Shipped fonts (vendored)

The fonts every library can take a copy of (`src/app/font_store.hpp`). They are
built into the program's resources; nothing else reads this folder.

A folder per family, named exactly as the font names itself, each with its
licence. `SOURCES.txt` lists what `tools/fetch_bundled_fonts.py` fetched and each
family's version: static files as Google Fonts serves them, never changed.

## Liberation 2.1.5

Sans, Serif and Mono, the same widths as Arial, Times New Roman and Courier New.
Not on Google Fonts under these names, and not fetched:

    https://github.com/liberationfonts/liberation-fonts, release 2.1.5
    as packaged by Ubuntu 24.04's fonts-liberation 1:2.1.5-3

## Licence

All SIL Open Font License 1.1, `OFL.txt` in each folder, which goes into the
program with the fonts (`src/app/CMakeLists.txt`). A font under it may ship with
other software but not be sold alone, and the licence goes with it. A font's
Reserved Font Names, where its `OFL.txt` declares any, must not be on a changed
copy.

**Do not edit these files.** A library's copy of a font is known by its bytes, so
upgrading means new files, not changed ones, and a library that has a copy keeps
it. To add a family, put it in the fetch tool's list and run the tool.
