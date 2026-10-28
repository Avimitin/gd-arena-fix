# Native 1080p rendering

`NativeRendering=1` allocates a 1920x1080 main render target while preserving
3840x2160 logical coordinates. `0` keeps the original 4K target and downsample.
Textures, auxiliary outputs and intermediate targets retain their sizes.

## Coordinate handling

Viewport, scissor and clear rectangles are scaled only when the main canvas
is bound. Viewport/scissor getters convert the actual device state back to
logical coordinates, including after state-block restoration. Odd boundaries
round outward to physical pixels. Main-target XYZRHW `DrawPrimitiveUP`
vertices are copied into reusable scratch storage and their XY coordinates
are scaled with the D3D half-pixel convention; caller data is unchanged.
Projected vertices retain their original matrices. The final output copy is
same-size in native mode. No new game-address patches or timing changes are
needed.

## Binary evidence

IDA evidence from the supported `gdxg.dll`:

- `0x1800A5A20` calls `GetViewport` through the D3D device vtable, copies the
  returned width and height into AFP globals, and stores them in
  `qword_182F73478`. The 4K viewport set during canvas creation therefore
  causes AFP to initialize with 3840x2160 logical dimensions.
- `0x1800A5A20` also assembles a vertex shader whose source applies a matrix to
  `POSITION` (`m4x4 oPos, v0, c0`). `0x18009C500` constructs the AFP
  orthographic matrix. Some geometry therefore uses the viewport and matrix.
- AFP has CPU-built pretransformed geometry too. `0x1800A6840`, called by the
  AFP render task `0x18009E6B0`, sets FVF `0x144` (XYZRHW, diffuse, one texture
  coordinate), copies six 28-byte vertices per quad to a scratch list, and
  calls `0x1800A65C0`. The latter calls `DrawPrimitiveUP` with triangle-list
  topology, two triangles per quad, and 28-byte stride. These XY coordinates
  are already in screen space, so viewport scaling alone does not scale them.
- Additional AFP UP-draw paths include `0x18009EE20` and `0x18009EF00`, which
  use FVF `0x44` (XYZRHW plus diffuse), 20-byte vertices, and respectively
  line-list and triangle-list topology. `0x1800A21D0` uses FVF `0x142` with
  24-byte vertices, a non-XYZRHW layout. AFP rendering therefore mixes
  screen-space and transformed vertex data. The `0x144` path restores AFP's
  vertex and pixel shaders after its batch.
- `0x18009E1B0` sets the viewport from AFP's stored dimensions and initializes
  a scissor rectangle. At 3840x2160 it explicitly uses 7680x4320 scissor
  bounds. `0x18009C490` doubles supplied scissor coordinates when AFP
  dimensions are 3840x2160, while `0x1801796E0` builds a separate lane-space
  scissor. Changing AFP dimensions to 1920x1080 would disable these 4K-specific
  coordinate conventions and alter the orthographic setup.

## Limits and validation

Native-mode demo captures appear to omit song-title/artist and player-name
text that is visible in the fallback path. Keep this mode opt-in until the
dynamic-text rendering path is corrected and verified.

The graphics smoke test covers projected drawing, screen-space sprites and
lines, clipping, state restoration, auxiliary-target isolation, output copies
and ResetEx. It does not establish visual correctness throughout the game.
Buffered XYZRHW and indexed user-pointer draws are not converted; the confirmed
AFP screen-space paths use non-indexed user-pointer draws. Movies, Live2D,
other renderers and fullscreen transitions still need live testing.

The main target has 75% fewer pixels, but CPU work and auxiliary rendering
remain. Existing timing logs measure CPU submission rather than GPU execution;
a stable 120 FPS or a specific speedup on weaker hardware is not guaranteed.
