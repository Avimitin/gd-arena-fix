# System and tutorial timing

All addresses, instruction signatures, data-layout offsets and continuations
are defined in `src/game_profile.h`. The original game image is unchanged.

## System counters

`system_timing.cpp` handles the profile's audited global and object counters.
Each descriptor specifies the displaced instruction form, its source value,
destination and any state identity. Native reset writes start a new elapsed
epoch. Repeated calls in one render update cannot advance it twice.

Incrementing counters consume their first marker immediately: several native
screens use zero to initialize themselves. Later increments represent elapsed
time at 60 authored units per second. Countdowns retain fractional time and
saturate at zero. LEA-based pre-increment comparisons receive the corresponding
elapsed pre-increment sample. Displaced register writes and comparisons are
reproduced, including INC/DEC carry preservation.

The entry countdown is shared by two player callbacks. Those callbacks set
update requests; `2400F0` consumes them. Only the countdown's actual decrement
is converted. Network countdown synchronization, second rounding and input
requests keep their original semantics. The per-player state age in `2C24F0`
also remains continuous across stages that do not reset it.

Native service-button gesture tests check release and double-press deadlines
without delaying new edges. System counter tests cover all descriptor forms,
duplicate calls and long stalls. Whole-game entry/network flows are not
executed by these isolated tests.

## Login and registration countdowns

These screens also have private per-player timers, separate from both the
shared entry countdown and the general menu seconds counter. The callbacks
at `2BAB40`, `2BB420`, `2BC3D0`, `2BD3E0`, `2BE530`, `2BFDE0`, and `2C0A70`
decrement their own fields once per update. This explains why correcting the
shared menu timer did not correct login/registration at high refresh rates.

`timers.cpp` adapts 30 decrement sites through the existing main-frame elapsed
context. Aliases share fractional progress by native field address. Native
resets restart progress; inactive callbacks accumulate no backlog. The original
callbacks still process input, network replies, state transitions and expiry
on every invocation. Their counters retain authored 60-unit seconds.

Eleven UI gates originally refreshed only when `remaining % 60 == 59`.
They now compare the native computed seconds against the native per-player
display cache. This prevents repeated countdown beeps on held ticks and stale
digits after a stalled update skips a modulo boundary. An exact-1200 warning
uses a one-shot crossing. Addresses, signatures, operands and offsets are in
the selected `game_profile.h` profile.

The native-image regression runs the full `2BFDE0` callback with UI and input
dependencies substituted. It checks independent player deadlines and digits,
60/75/95/120/144/165/240/360 Hz, variable cadence, duplicate calls, stalls,
inactivity and immediate input. Operand tests cover all 30 producer descriptors
and the warning crossing. Other complete login/registration flows still need
live validation; descriptor coverage is not a claim of complete scene coverage.

## Tutorial

The instruction countdown and practice-note position are separate clocks.
The first counts down in authored 60-unit time; the second advances at 300 units
per second and respects the tutorial's pause flag. The two scripted pause
windows are integrated explicitly so a stall crossing both boundaries does
not incorrectly advance the practice notes through the paused interval.

Thirty exact-value comparisons become descending deadline crossings. Their
native branches still run the original cue actions. Repeated narration in the
same cue group coalesces after a stall; duplicate update calls do not replay it.
One comparison also controls a signed conditional move, so its equality flag
is changed independently of its ordering flags.

Local effect/hold countdowns retain native reset markers. Gauge easing replays
the original integer recurrence for the elapsed authored steps, stops at its
fixed point and uses a fractional draw sample in interpolation mode. It does
not loop through an unbounded stalled interval after convergence.

Tests execute the original tutorial routine with inactive player fixtures and
sound dependencies replaced. They check twelve-second timelines, all repeated
narration groups, duplicate calls, scripted pauses and stalls. Gauge arithmetic
is compared with the authored recurrence. Active-player tutorial integration
remains in the audit queue.

## Summary results and diagnostics

`summary_timing.cpp` converts the result scene at `3AF420`. Native count-up
formulas now consume elapsed stage age. Eight periodic chime gates fire once
per crossed interval within their native first-second gate, and the exact-60
reveal fires once even if that integer tick is skipped. Already obsolete
count-up chimes beyond the first second are omitted.

The QC schedule advances its visual phase and brightness through all elapsed
periods while coalescing obsolete I/O operations. The card-error schedule keeps
device polling per invocation and converts only its retry/deadline clock.

Native image tests run these paths at 95, 120, 144, 240 and 360 Hz. This verifies
the intercepted instructions and selected state-machine behavior, not every
scene or external audio/network/graphics dependency. The full conversion is
still tracked in [the coverage audit](frame-dependency-audit.md).
