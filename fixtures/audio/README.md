# Test audio

A real, compressed file for the audio engine's tests (`tests/test_audio_engine.cpp`).
The tests that check sound sample for sample write their own WAV files instead,
so they know every sample.

| File | Length | For |
|---|---|---|
| `tone_440hz_1s.mp3` | 1 s (the MP3 container reports 1.045 s) | decoding a compressed file, and resampling 44.1 kHz to the engine's 48 kHz: a 440 Hz sine at half scale, both sides |

Made with:

    ffmpeg -f lavfi -i "aevalsrc=0.5*sin(2*PI*440*t)|0.5*sin(2*PI*440*t):s=44100:d=1" \
        -map_metadata -1 -fflags +bitexact -flags:a +bitexact -c:a libmp3lame -b:a 96k \
        tone_440hz_1s.mp3
