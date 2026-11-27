# miniaudio (vendored)

David Reid's miniaudio: `miniaudio.h`, unmodified, compiled once by
`miniaudio.cpp`. SeventhSlide uses its device I/O only (decoding, encoding,
the node graph and its engine are compiled out): the Listen device the
Audio Mixer plays on (`src/audio/audio_engine.cpp`), and later the sound
devices window outputs play on. Backends such as PulseAudio, ALSA and JACK
are loaded at run time; none is linked.

## Licence

A choice of public domain (Unlicense) or MIT No Attribution (`LICENSE`, and
the end of `miniaudio.h`). Neither asks anything of copies or binaries.

## Provenance and version

    https://github.com/mackron/miniaudio, tag 0.11.25 (2026-03-04)
    miniaudio.h sha256 ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89

**Do not edit `miniaudio.h`.** Upgrading means replacing it (and `LICENSE`)
from a newer tag and updating the tag and hash above.
