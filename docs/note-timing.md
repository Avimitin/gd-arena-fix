# Note motion

`NoteSmoothing=1` smooths GuitarFreaks and DrumMania note rendering above
60 FPS. The song clock remains authoritative; the estimator only supplies
visual positions between its updates. It does not change audio buffers,
input handling, chart traversal for judgment, or judgment offsets.

## Why raw positions can step

The supported game's cursor is a software voice-source position, not a direct
measurement of the sample currently reaching the speakers. The source mixer
advances it as it processes audio chunks. Changing backend, buffer size, or
processing schedule can therefore change how often rendering sees a fresh
position. A small audio buffer is not a requirement for smooth rendering.

The relevant native path is:

- `166F30` assigns a source ID to the channel; `1633C0` creates and registers
  its source interface through engine slot `+0x20`.
- `164FE0` finds that interface and calls slot `+0x80`: `044940` calls
  `158770`, which converts the accumulated sample position at source `+0x1D0`
  into a byte cursor at `+0x238`.
- Source processing in `158E30` advances `+0x1D0` by the samples processed,
  adjusted for the source's rate. `1B60F0` converts the byte cursor to the
  game's 300 time units per second.

These are offsets from the supported image's preferred base. Neither a cursor
update interval nor the smoothing correction measures audible latency.

## Estimator

`src/note_clock.h` uses the existing main-frame QPC elapsed time, not target
FPS. It predicts at 300 units per second, then corrects toward the current
song-derived visual time with a 40 ms exponential time constant. This is a
correction response, not an added fixed 40 ms delay. The estimate is bounded
to within 25 ms of the reported position; no assumed audio latency is added.

Each render frame is charged once. Duplicate passes share their estimate.
Startup, skipped frames, backward seeks, large forward jumps and render stalls
of at least 100 ms reanchor immediately. A cursor held for 100 ms also
reanchors and stops prediction; before that, the 25 ms bound still applies.
Without a playback-state signal, a brief hold cannot reliably distinguish
buffered reporting from a pause. The estimator does not promise motion or
audio output during process suspension.

## Rendering boundaries

GuitarFreaks retains the native time argument to batch `3C06E0`. Only the six
profile-listed chart calls into projection `3C0350` receive a correction to
their relative head and tail times. Both endpoints receive the same shift.
Other callers pass through. Players have independent estimates, reset on
rendering mode or skin changes. Native hit state and the shared song clock
are not overwritten.

DrumMania replaces five local `VMOVSS` reads of visual time `1AFFC9C58` in
render passes `3BACC0`, `3BB200`, `3BB5D0`, `3BC710` and `3BCED0`. All use one
cached DM estimate. The replacement changes only the destination vector
register, reproducing the original load's zeroing of its upper lanes.
Projection, culling and fades in those passes consume the same visual time.
The shared global and the drum sound/input worker are untouched. The passes
contain several visual record types; this is not a separate patch for each
note, marker or long-note endpoint.

All sites, signatures, destination registers and globals belong to
`src/game_profile.h`. They are checked before patch installation. The
estimator introduces no audio-thread hooks, waits, allocations or logging.

## Configuration and verification

The option defaults on; `NoteSmoothing=0` restores direct song-position drawing.
At 60 FPS it is inactive. `Diagnostics=1` enables raw/visual cadence and
correction reports; a distinct position on every frame does not establish
even monitor scanout or correct physical audio/display offset.

Clock tests exercise continuous and chunked sources, changing frame intervals,
seeks and holds. Rendering fixtures check hook scope, shared-clock preservation,
register behavior and repeat-pass consistency. These checks do not replace
in-game verification of motion, visibility and perceived hit alignment.
