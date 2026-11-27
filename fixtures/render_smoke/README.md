# Render smoke fixtures

1920×1080 PNG grabs rendered by the native painter from the song fixtures in
`fixtures/oracle/*.json` (the same inputs the layout dump tests use):
`<id>_line0.png` is the first line of each oracle song.

Regenerate:

```bash
SEVENTHSLIDE_UPDATE_RENDER_SMOKE=1 ./build/bin/seventhslide_tests_media "[render_smoke]"
```

The render-smoke CLI writes the same files (`seventhslide-render-smoke
fixtures/oracle fixtures/render_smoke`; the CLI tools are opt-in, with
`-DSEVENTHSLIDE_BUILD_TOOLS=ON`).

`tests/test_render_smoke.cpp` re-renders them and compares with an
anti-aliasing tolerance.
