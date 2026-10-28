# Frame-independent timing, input and judgment line

Related: [coverage audit](frame-dependency-audit.md) and
[central game profiles](development.md#game-build-profiles).

Rendering may target 120, 144, 240 or another supported refresh rate. Musical
time comes from sampled audio playback; countdowns use elapsed time; authored
animation ticks use a separate clock with optional render interpolation. None
of these clocks divides by the configured rendering FPS. The stock 60 FPS
configuration keeps the original timing paths.

The current implementation replaces the earlier fixed-120 filter output and
adds elapsed-time menu/play timers. Native-code fixtures verify their behavior
with controlled state. They do not measure end-to-end latency or certify full
gameplay at each refresh rate. Untraced procedural animations and scene counters
may still contain per-frame assumptions.

## Scope and evidence

Supported `gdxg.dll` SHA-256:
`0ed4aabc75f5bd997c237759d5fcf17143aa69073110b5c9235d1515cf0fd32c`.
Addresses below use the preferred image base `0x180000000`. Critical control
flow was checked in IDA disassembly because the decompiler loses parts of
the AVX calculations and sometimes invents argument types.

The available DrumMania boot log is from 2026-10-26 18:35:44. It reports:

- TOPPING Pro USB Audio Device, 48,000 samples/sec, 32-sample ASIO buffers.
- Driver-reported output latency of 168 samples, or 3.50 ms.
- `-vsyncbuffer 3`, and spicecfg taking precedence over command-line settings.
- The J32D serial input emulator on COM1 and MIDI drum bindings.

A 32-sample block lasts 0.667 ms. Neither that duration nor the reported
3.50 ms output latency measures the full pad-to-sound path. The log predates
the latest animation candidate and must not be treated as its live telemetry.

## Drum hits run in the sound-engine thread

The cabinet-button update in the render loop is not the whole drum-input path.

| Address | Observed role |
| --- | --- |
| `0x180154AA0` | Starts the sound worker with entry `0x1801549B0`. |
| `0x1801549B0` | Sets thread priority 15, requests MMCSS `Pro Audio` / critical priority, and loops on virtual slot +24. |
| `0x1806278F8` | `bmsd_engine` vtable; slot +24 points to `0x180150F80`. |
| `0x180150F80` | Updates drum I/O, computes rising edges, calls the registered judgment callback, then processes sound commands and mixing. Includes backend-dependent waits/buffer work. |
| `0x180168F40` / `0x180169270` | Update serial drum state and process pad thresholds/history/pedals. |
| `0x1801FDF80` | Copies the drum chart and installs `0x1801FE4D0` in callback slot `0x1808DD250`. |
| `0x1801FE4D0` | Reads playback position, chooses nearby notes, checks nine pad edges and dispatches judgments. |
| `0x1801FF370` / `0x18004F8B0` | Map lanes to drum bits and read the sound worker's rising-edge mask. |
| `0x1801FEBD0` | Classifies hit error and writes the chart/result flags. |
| `0x1801FEE80` | Dispatches matched-note or empty-hit sound handling. |

The judgment callback reads `0x180164FE0` directly. Its normal clock is
`playback_position * 3.0 / 1920.0 + song_offset`, consistent with conversion
of 192,000 PCM bytes/sec to the chart's 300 units/sec. Pad and pedal judgment
offsets are separate. The lane loop truncates the signed error to an integer,
takes its absolute value, and passes it to the classifier; it retains the
float error for result reporting. These are chart-time units, not render frames.

The callback can reach `0x180286290` and the keysound path before the sound
worker consumes its command queue. Sound commands are queued, not necessarily
audible at the instant of the input event. Some note types dispatch sound
sequences rather than a single immediate sample.

The worker shares mutexes with main-thread gameplay work. Its actual iteration
rate, backend wait duration, lock contention, MIDI event age and pad-processing
delay have not been measured. Do not equate a 32-sample ASIO buffer with a
verified 1,500 Hz judgment rate. Do not impose a 120 Hz limiter on this worker.
The traced path is DrumMania; GuitarFreaks needs a separate judgment audit.

Spice's GITADORA serial emulator constructs a status packet on read and samples
button states in the J32D handler. That packet does not carry the original MIDI
event timestamp. Its general J32D ACIO IAT hook is a different path and should
not be used to infer GITADORA's input cadence. See the upstream
[serial reader](https://github.com/spice2x/spice2x.github.io/blob/main/src/spice2x/games/gitadora/handle.cpp)
and [drum packet builder](https://github.com/spice2x/spice2x.github.io/blob/main/src/spice2x/games/gitadora/j32d.cpp).

## The visible notes use a different clock

`0x180286EC0` filters the audio-derived integer timestamp. Its first history
contains up to 16 raw values. Its second history feeds filtered output back
into the history. Both compensation stages assume a constant update cadence;
the earlier implementation redirected their divisors to a private `120.0` value.
That approach is no longer used; the divisions retain their original constants
and the replacement writes the resulting clock from audio after the native
function updates its initialization/seek/history metadata.

`0x180287230` returns the filtered clock plus the song offset. In
`0x1802B5760`, the same scene-clock value is sent to `0x18035C070` for main
gameplay work and `0x18028A940` for visual updates. The latter sets the DM
visual clock at `0x1AFFC9C58`, adding separate display offsets from
`0x1AEEEBB70`, `0x1AEEEBB84`, `0x1AEEEBB98`, and an optional further offset.
The drum worker instead receives its pad/pedal offsets through
`0x1808F0BA0 + 0x18/+0x1C`, populated by `0x180275190`.

DM drawing paths including `0x1803BB200` subtract the visual clock from the
chart timestamp, then apply fall speed and the lane projection through
`0x1803BA2C0` / `0x1803B8E30`. Doubling render frequency does not itself
double that timestamp difference. However, separate clocks and offsets mean
the note reaching the drawn line is not automatically proof of zero hit error.

### Earlier fixed-divisor filter error

`tests/song_filter_probe.py` maps only the supported game's file-backed PE
sections and necessary zeroed state into its own process. It does not load
imports, run the game's entry point, or change the game file. It applies the
two earlier private-divisor patches and executes the native filter
with controlled audio-derived timestamps. No zero-source fallback is used.

Each scenario runs 7,200 updates, discarding the first 2,400 for settling.
The synthetic continuous clock starts at 100 seconds and is quantized down
to the native 300-unit/sec input. Negative errors mean the filter lags.

| Actual update cadence | Mean filtered minus continuous audio clock |
| --- | ---: |
| Steady 120 FPS | -0.83 ms |
| Steady 119 FPS | -10.62 ms |
| Steady 110 FPS | -98.13 ms |
| Steady 95 FPS | -281.08 ms |
| Alternating 6 / 10.667 ms frames, mean 120 FPS | -1.33 ms |
| One 25 ms frame per 120 updates, otherwise 8.333 ms | -18.56 ms |

At steady 120 the mean filtered-minus-quantized-input error is zero in this
test. The small remaining error is timestamp quantization. At steady 95 FPS
the filtered-minus-input error is -279.50 ms: the large error is introduced
by the filter, not by audio buffering or timestamp quantization.

These values are not measured monitor latency or a diagnosis of the player's
current offset. They demonstrate why fixed-divisor compensation cannot promise
line timing across rendering slowdowns. The recursive history amplifies a
cadence mismatch; an average FPS counter alone is inadequate validation.

Reproduce on Windows with 64-bit Python and an AVX2-capable processor:

```powershell
py -3 tests\song_filter_probe.py 'C:\path\to\gdxg.dll'
```

## Current audio clock implementation

`0x1801B60F0` remains the owner of the audio sample and its timestamp globals.
Its call at `0x1801B6152` to the raw cursor function `0x180164FE0` is
redirected through a nearby bridge. The bridge calls the original function
once and captures its result in a thread-local sample belonging to that
invocation. Capture state and frame clocks are thread-local. The loading/command
thread also calls the sampler, but bypasses the correction because it has no
render clock. Other callers of the raw cursor, including the drum worker,
retain their direct calls and receive no new hooks, locks, logging or allocations.

The sampler's format record converts PCM position to chart time:
`ticks = position / ((bits * sample_rate) / 4) * 300`. For 16-bit, 48 kHz
stereo this is `position * 300 / 192000`. The captured value keeps fractional
ticks discarded by the native integer return. It is accepted only with a valid
format, a matching native quantization interval, the same main-frame serial,
and the matching selected source timestamp. The output remains a native float.

After `0x180286EC0` runs, the mod replaces `0x1B008B430` with this audio time.
Without a matching capture it uses the resolved native raw time at
`0x1B008B438`. Startup below three ticks retains the native zero output.
Backward/forward seeks take effect immediately; a held nonzero playback
position stays held. There is no recursive settling delay or extrapolation
past a valid held position. The already-existing missing-source fallback
advances at 300 units per elapsed second, retaining fractional remainders and
charging each frame once. Long stalls are not clipped for this fallback.

Song, visual, pad and pedal offsets remain unchanged. No assumed ASIO buffer
or display latency is added. A backend cursor may itself be quantized or lead
audible output; measuring that relationship remains necessary. Direct sampling
can expose backend cursor steps previously hidden by smoothing.

## Menu and timed-play clocks

`seconds()` uses Windows `QueryPerformanceCounter` and caches
`QueryPerformanceFrequency`. The main-frame wrapper measures consecutive QPC
timestamps. Countdown and missing-position clocks consume the full delta,
without the animation catch-up clamp. They need no dedicated ticking thread:
on the next update the elapsed interval includes the time spent stalled.
Reading a high-resolution clock does not guarantee scheduling, input capture,
audio-buffer service or visible frames during a CPU/process suspension.

The shared menu state starts at `0x1B008B2C0`: subcounter, remaining seconds,
previous seconds, pause/hold flags, and two UI pointers. Native update aliases
`0x180280FE0`, `0x180281170`, and `0x1802D46E0` decrement the seconds field once
per **80 updates**, as confirmed in disassembly. The latter is reached from
multiple menu scenes. This is separate from the native animation clock.

The wrappers evaluate the native pause path each frame and schedule the native
second-boundary action once per elapsed second. Calls through multiple aliases
in the same frame share one clock. The original code still updates UI, warning
sounds, the hold-at-one flag, zero and negative expiry values. Hooks on
`0x180281320` and `0x180281440` reset the fractional phase when setting or
clearing the timer. Inactive scenes accrue no backlog. Render stalls count
their full duration; catch-up calls are bounded by the remaining countdown.
Callers that wait for a negative value retain the original extra zero interval.

The separate timed-play value at `0x182927BB4` is stored in 60 ticks/sec.
Its wrapper at `0x1801F3170` subtracts elapsed ticks with pause/mode/reset
handling, while its other frame work still runs at the actual render cadence.
Only the original `dec eax` at `0x1801F31B5` is removed. No global main-loop
throttling is used.

All new entry and instruction signatures are checked before any mutation.
Installation and rollback share the existing MinHook/byte-patch transaction.

## Validation

`--verify-image` executes the actual sampler, raw-cursor lookup, filter and
menu routines in an isolated copy of the supported image. Audio device position,
AVS wall clock, debug state and sound/UI dependencies use controlled fixtures.
The timed-play wrapper uses an observer for its unrelated frame work. Tests cover:

- A displayed minute lasting a minute at 95/120/144/240 updates/sec, including
  alias calls, warning beeps, pause/resume, reset/clear, hold/release and stalls.
- Native audio/filter paths at 95/110/119/120/144/240 updates/sec, irregular
  intervals, stalls, seeks, held playback, startup and missing-position paths.
- One backend poll per sampler invocation, unchanged native timestamp metadata,
  isolated loading-thread behavior, stale/invalid sample rejection, instruction
  conflict rejection, and the unchanged original game file.
- Native animation evaluation and GF/DM sprite loops at 120/144/240 updates/sec,
  with integer callbacks and fractional movement/alpha. Initial/restarted poses
  require a continuous sample before interpolation is enabled.

The timer's 80-update origin means its observed speed also depends on call
cadence; the reported factor of two alone is not proof of a particular internal
frequency. These tests establish the replacement's duration rather than
inferring cadence from a screenshot or FPS overlay.

## Remaining improvement work

1. Validate the audio-anchored replacement during actual songs and menu changes.
   Establish the backend position's relationship to audible output before adding
   latency compensation. The filtered clock also reaches gameplay logic, so this
   requires broader validation than a draw-only change. Audit additional scene
   counters and GuitarFreaks judgment paths separately.
2. Measure and reduce presentation delay. The main frame presents the previous
   render before sampling/updating the next one. Benchmark double buffering
   and a one-frame D3D9Ex queue limit, while retaining consistent 120 Hz pacing.
   These control different queues. Spice documents that triple buffering can
   add input latency; D3D9Ex exposes the driver queue limit through
   [SetMaximumFrameLatency](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9ex-setmaximumframelatency).
   A lower queue limit is a candidate to measure, not a guaranteed frame-time
   improvement. Check effective spicecfg settings as well as launch arguments.
3. Instrument the existing sound worker and input path. Record QPC timestamps,
   thread IDs, input edges, judgment error, command enqueue/consume times and
   audio position in a bounded memory buffer. Drain it outside the audio thread;
   avoid synchronous logging and allocation there. Measure shared-mutex stalls
   and analog-style pad filtering before changing either. The worker already
   requests high priority and MMCSS; raising process priority is not the first fix.
4. Keep frame work within 8.333 ms with headroom. Profile GPU work, characters,
   auxiliary outputs and the 4K logical canvas separately. The default 1080p output
   uses a 4K render target and a downsample. `NativeRendering=1` reduces the
   main target to 1080p; auxiliary and intermediate targets remain unchanged. Optimize the dominant measured cost.
5. Tune the ASIO buffer only after measuring underruns and pad-to-sound delay.
   The logged 32-sample configuration is already small. A stable buffer has more
   value than a smaller value that introduces dropouts or scheduling jitter.

## What is needed to certify the judgment line

First compare display-clock and drum-worker clock values in the same time
domain, with the actual visual, pad and pedal offsets recorded. Verify normal
play, frame stalls, song transitions and different fall speeds. Then measure
local physical display/audio timing: rendering, queueing, scanout and panel
response are outside the isolated filter test. Sunshine/Moonlight capture
adds another path and cannot establish the local player's calibration.

The target is that zero chart-time error corresponds to the intended note
anchor reaching the line at the player's display, after measured compensation.
One displayed frame lasts 8.333 ms at 120 Hz; this does not force hit judgments
or sound processing to that granularity. At 240 Hz a frame lasts 4.167 ms.
The isolated clock checks do not certify physical alignment.
