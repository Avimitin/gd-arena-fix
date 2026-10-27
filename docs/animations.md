# Animation timing

The AFP import already receives measured elapsed time. AFP's internal
timeline update only dispatches frame events when its timeline advances.
The game also has a separate native animation engine whose integer frame
counter advances once per call. Its unmodified speed doubles when called
120 times per second. This is a concrete additional source of accelerated
menu and gameplay effects; it does not prove every reported effect uses it.

## Modes

`AnimationTiming=0` leaves these additional hooks disabled. `1` runs the
identified native and sprite animation clocks at 60 authored ticks/second.
`2` additionally interpolates supported native transforms and color/alpha
for rendering between ticks. These modes apply with `FPS>60`, including 144/240.
The public default remains `0` pending actual play validation.

## Hooked paths

Addresses are preferred VAs in the supported gdxg image.

| Address | Purpose |
| --- | --- |
| `0x180185E70` | Native animation update. Each top-level object gets an elapsed-time clock; recursive children follow the same tick. |
| `0x1801869D0` | Observe the actual frame evaluated and its resulting pose. |
| `0x180185230` | Temporarily apply an interpolated pose during drawing. |
| `0x1803B8120` | Standalone 24-byte sprite-effect record update. |
| `0x1803B81E4` | Inlined DM sprite update inside the per-render batch. |
| `0x1803B82B1` | Inlined GF sprite update inside the per-render batch. |
| `0x180289EF0` | Run the original gameplay visual callback every frame, then compensate two DM eight-tick counter banks when their observed write matches the original increment. |

The counter banks start at `0x182F55B98` (nine entries, 184-byte stride) and
`0x1B04C3784` (nine DWORDs). Newly activated counters are retained.
Each sprite record has its own clock; the surrounding batches run every render.
Fresh records receive an immediate sample, including between shared authored ticks.
Missed atlas phases are calculated arithmetically, with one final native update.
The helper's optimized callers retain loop state in R10/R11, although these
registers are volatile in the public Windows ABI. An assembly entry shim
preserves those registers and XMM0–5 across the C++ replacement.

All five entry signatures and both inline sites are checked before installation. They participate
in the existing hook transaction. Neither a 60 FPS configuration nor mode
zero installs these animation hooks.

## Interpolation

Frame callbacks, labels, creation and destruction run on integer animation
ticks. They are not executed again to obtain an intermediate pose.
Two 312-byte scratch objects are sampled with the original evaluator at
adjacent frames. Only known position, scale, pivot, rotation and color fields
are blended into the object for drawing; its logical pose is then restored.
Writes made by draw callbacks are preserved.

The sampler rejects stale/replaced objects, manual pose/frame overrides,
stopped animations, discontinuous frame jumps, unsupported group time scales,
end boundaries and any object that would require texture lookup. Texture IDs,
visibility, sprite frames and timeline callbacks are never interpolated.
This deliberately holds some animations on their authored frames. It does
not synthesize additional sprite artwork or apply video frame blending.

Clocks start new objects immediately and do not accumulate inactive time.
Full elapsed time is retained after stalls. Stateful native object callbacks
replay in order, so long stalls can cause substantial catch-up work; sprite
atlas catch-up does not replay every missed cell. Old per-object state is periodically discarded.
The five-second log reports native ticks, held updates, interpolated draws
and standalone sprite ticks; these counts are not GPU performance timings.

## Validation and limits

Isolated tests execute the supported game image's native evaluator/update
and GF/DM sprite loops using synthetic objects, with GPU drawing substituted.
They verify 60 native ticks/callbacks per second at 120/144/240 updates/sec, child clocks,
midpoint position and alpha, pose restoration, manual/stopped behavior, and
the private register convention. Clock tests cover 60/90/120/144/240 Hz,
irregular frame durations and stalls. Native sprite arithmetic is compared
byte-for-byte against authored-step execution, including signed counter wrap
and whole-day expiry. Off mode and 60 FPS are also checked.

These are not full game tests. Menu flow, actual hit effects, loops/scene
changes and frame cost on the target machine still require comparison.
Unrelated scene counters and additional procedural effects have not all been
converted. AFP, Live2D, song-clock filtering and judgment windows are not
changed by this option. The earlier high-fall-speed transparency report is
still unproven; this option should not be presented as its fix.

## Input, judgment and sound status

The main frame `0x1801A3910` calls `0x1800D9F40` at `0x1801A3B1A`.
That dispatches the cabinet input object's vtable slot +16. In the identified
BI3A/BI2X implementations (`0x1800D8A60` / `0x1800D7AD0`), a ready device
updates its status and copies previous/current button states. This path is
outside the animation scheduler and remains tied to actual game frames.
At a sustained 120 FPS it gets approximately 120 update opportunities/sec;
an FPS drop reduces that frequency. This does not establish the hardware,
MIDI, USB, or spice2x event-source polling frequency.

Song position is sampled through `0x1801B60F0` from audio playback and
represented in 300 units/sec. That is a timestamp representation, not proof
of a 300 Hz input or judgment thread. The binary contains WASAPI/ASIO audio
backends; this mod does not alter their sample rate, buffer size or scheduling.
Audio playback is not a video-FPS setting. Further tracing identifies a
separate sound-engine worker that updates drum edges, judges hits against
playback position, and dispatches keysounds. The cabinet update above must
not be treated as the complete drum-input path. See
[Timing, input, audio and judgment line](timing.md) for addresses, the
reproduced song-filter defect, and remaining end-to-end measurement limits.
