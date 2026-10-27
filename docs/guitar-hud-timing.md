# GuitarFreaks HUD timing

IDA tracing of `0x18036F090` identified two animation-label consumers and a
separate seven-step number transition. These are visual operations in the
existing HUD update; the surrounding update and input paths still execute
on every call.

## Label consumers

`0x18036F3F0` compares an icon animation's current frame with a stored label
before calling `0x18036DBF0`. The callee selects the `CIC_ICONA` texture.
This is an idempotent update, so its held-label condition remains true.

`0x18036F70C` compares a combo digit's current frame with its hidden label
before seeking `open00` when a nonzero value needs to be shown. This also
retains the original held-label behavior.

Both now use the existing animation visit history, through the same predicate
adapter as the two previously converted completion queries. A crossed label
is available once after catch-up; a current label remains true on repeated
reads. Neither consumer creates a clock. The native true/false branches,
texture selection and label seeks remain in the game.

Arena's `loop00`, `loop01` and `loop02` comparisons are held conditions that
gate existing age counters. They must not be changed into one-shot events.
The paired `open00_end` condition in `0x1803468F0` and the hide transitions in
`0x180346600`/`0x180347260` still need asset/lifecycle verification before any
additional patch is justified.

## Number transition

At `0x18036F5AC`, the game detects a changed target, resets the display age
and computes a signed slope `(target - displayed) / 7`, including its native
multiple-of-ten adjustment. The following block at `0x18036F603` applies
one slope step on each call until age seven; the next call snaps to target.
That block therefore ran twice as fast at 120 FPS.

The adapter leaves target detection and slope calculation native. It advances
the existing display age using the common elapsed-counter helper, keyed by the
age field in the effect module's existing map. Each player's number transition
can restart independently. The first step remains immediate; later steps use
elapsed time. Digit traversal resumes at `0x18036F63E` on every call.

Catch-up uses at most eight arithmetic iterations and coalesces the display
to its final value. It does not replay input, sounds, judgment or the HUD
function. Duplicate calls in the same main update hold the number and cannot
report another digit change. These integer digits retain native arithmetic;
they are not a new interpolation engine.

The image fixture copies and executes the original arithmetic block, including
the native slope calculation, as its reference. It compares increasing and
decreasing values, the multiple-of-ten case, tiny differences, final snaps,
duplicate calls and stalls at 60/95/120/144/240/360 updates per second.
Label fixtures check both native branch destinations, skipped labels and held
predicates against the real animation engine. Complete HUD rendering and
asset-specific transitions still require live validation.
