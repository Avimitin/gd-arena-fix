# Mini-game and scene-event timing

The mini-game called by arena scene `34AFA0` has separate logic in `3FBF40`,
`3FA1E0`, `3F8B60` and `3F82C0`. Its counters are unrelated to the shared
object-animation engine. The conversion is in `minigame_timing.cpp`; addresses,
guards, branch destinations and record layouts are in `game_profile.h`.

- Player, enemy and debris atlases use elapsed authored ticks. The fifty burst
  records expire arithmetically. Updates within the same render consume no
  additional time.
- Player motion and gravity integrate fractional elapsed time. The integral
  retains the native semi-implicit acceleration convention at integer authored
  ticks. Native lane snapping and input checks still execute every render.
- Enemy introductory motion integrates bounded velocity reversals analytically.
  Its firing and death-effect schedules use elapsed deadlines. Missed volleys
  and cosmetic explosions coalesce using current positions.
- Particle motion uses elapsed time. Type 3 splits an interval at the twenty-tick
  turn before applying the new velocity; it cannot miss the exact-tick change.
  Spin reflects at its native bounds without a loop over missed frames.
- Player damage recovery crosses its sound deadline once and ends after three
  seconds. Banner hold/fade and scale/rotation use elapsed time; byte opacity
  retains a fractional sidecar.
- Hit budgets, random-number draws, lane selections, queue indices and enemy
  damage counts remain event counts.

Native image tests exercise these paths at 95/120/144/240/360 Hz, duplicate
calls and multi-second stalls. Input polling is counted to verify that it
continues at the full update frequency. These fixtures replace sound and input
backends; they do not establish live collision or gameplay correctness. Native
collision checks still use the positions available on each update, so a long
stall can skip a collision. The analytic enemy oscillator also needs visual
comparison against the cabinet's authored motion near reversals.

Arena stages `345710` through `347260` additionally contain a shared wait age,
gesture windows and two player voice delays. These use elapsed clocks. The
exact-300 transition uses a crossing; zero-expiry and negative-expiry countdowns
keep their different native meanings.

## Animation-frame events

Elapsed animation sampling alone is insufficient for scene code that tests
`animation.frame == label`. A label can remain visible for several rendered
frames or be passed during a stall.

The object engine now records the authored frames visited in each update,
including recursive children. Consecutive visits are stored as ranges. A scene
consumer can query `animation_event(object, label, consumer)` and consume the
latest matching visit once. Holding a frame does not repeat a cue; seeking or
revisiting it can create a new visit. Different consumers remain independent.

The first twelve guarded consumers are arena sound/voice/stop events. Equality
checks that intentionally keep a wait active at a held animation label retain
their level-triggered meaning. Other scene label consumers still need explicit
classification and conversion; this mechanism alone does not certify them.

Native parent/child engine tests cover held frames and events skipped across a
2.25-second stall. Bridge tests check all twelve branch destinations and displaced
stack/register/call behavior.

## Additional paths

The tutorial practice cue's ten-position window now uses a one-shot crossing.
Its note selection (`2DC290`), judgment (`2DC440`) and miss (`2DBCE0`) routines
already compare supplied practice-clock positions, rather than update counts.
The practice clock itself is converted to 300 units per elapsed second.

Both card-reader slots in `2346B0` now retain their thirty-second startup
timeout. The alternate error updater `234320` shares the elapsed error age with
`233D00`. Native tests exercise both slots and a stalled timeout transition.

Additional reviewed exclusions: `05A920` and `05B770` maintain resource/map
counts; `1BAF40` advances a texture-item loading index and queue count;
`415540`/`4159A0` maintain work/wait notification counts; `4184C0` releases
queued resources; `4359A0`/`4377D0` append vertices. Their unit increments are
not elapsed clocks and remain unchanged.
