# Test videos

Real, playable clips for the tests that load video into mpv. Each is a
solid #3366cc frame, 64x36, H.264 High: as small as a file mpv will
decode can be. The first two are 10 fps, with no sound.

| File | Length | For |
|---|---|---|
| `solid_3366cc_64x36.mp4` | 2 s | the player and facade playback tests |
| `solid_3366cc_64x36_20s.mp4` | 20 s | the web remote's browser tests (`tests/remote_web`), which pause, seek and restart it: long enough not to end under a slow machine |
| `tone_3366cc_64x36_8s.mp4` | 8 s | a video with sound: the same picture at 25 fps (a common rate, so the sync tests see real frame steps) with a 440 Hz sine (AAC, 48 kHz), for the tests where a video's sound plays through the Audio Mixer |
| `tone_3366cc_64x36_3s_sound_at_500ms.mp4` | 3 s | the same, its sound starting half a second after the picture (an AAC track whose first packet is at 0.478 s, as files with a late sound track have): the sound must come in at 0.5 s, not at the start |

The 20 s clip was made with:

    ffmpeg -f lavfi -i color=c=0x3366cc:s=64x36:r=10 -t 20 -c:v libx264 -profile:v high \
        -pix_fmt yuv420p -an -map_metadata -1 -fflags +bitexact -flags:v +bitexact \
        solid_3366cc_64x36_20s.mp4

The clip with sound was made with:

    ffmpeg -f lavfi -i color=c=0x3366cc:s=64x36:r=25 -f lavfi -i sine=frequency=440:sample_rate=48000 \
        -t 8 -c:v libx264 -profile:v high -pix_fmt yuv420p -c:a aac -b:a 64k \
        -map_metadata -1 -fflags +bitexact -flags:v +bitexact -flags:a +bitexact \
        tone_3366cc_64x36_8s.mp4

The clip whose sound starts late was made with:

    ffmpeg -f lavfi -i color=c=0x3366cc:s=64x36:r=25 \
        -itsoffset 0.5 -f lavfi -i sine=frequency=440:sample_rate=48000:duration=2.5 \
        -t 3 -c:v libx264 -profile:v high -pix_fmt yuv420p -c:a aac -b:a 64k \
        -map_metadata -1 -fflags +bitexact -flags:v +bitexact -flags:a +bitexact \
        tone_3366cc_64x36_3s_sound_at_500ms.mp4
