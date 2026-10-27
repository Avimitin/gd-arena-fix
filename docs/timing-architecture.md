# Timing architecture

The mod is a compatibility layer for running the existing game above 60 FPS.
It retains the game's state machines, input processing, judgment and rendering
code. Changes target confirmed dependencies on update frequency; they do not
replace the game loop or introduce a new gameplay engine.

## One elapsed-time source

The main-update hook in `src/timing.cpp` samples `QueryPerformanceCounter` once
per update. `SceneFrame` holds that thread's update serial, elapsed duration
and active flag. Menu timers, scene handlers, results, animation playback and
audio-clock fallbacks all read this same context. Animation entry does not
increment a second serial or accept a separate delta.

Other QPC reads in presentation diagnostics measure performance; they do not
advance gameplay. Normal musical position comes from the existing audio
backend. The loading/audio worker retains its native behavior.

## Progress belongs to the existing state

`ElapsedCounter`, `ElapsedNativeCounter`, `SceneTimeline` and `AnimationClock`
are arithmetic state, not Windows timers, threads or scheduled callbacks.
They retain fractional progress, reset information and event history using
the shared elapsed interval.

| Owner | Retained state | Sharing rule |
| --- | --- | --- |
| Menu countdown | Partial second and native reset/pause state | Its three update aliases share one counter. |
| Scene age | Partial authored tick and native state/epoch | Cues and transitions consume the same scene timeline. |
| Independent countdown field | Partial tick and expected native value | Producers in the domain share the entry keyed by that field. |
| Animation root | Partial authored frame and track identity | Children follow native parent traversal; they do not advance again. |
| Event consumer | Last delivered crossing | Reads existing progress; owns no elapsed-time source. |

Separate state is necessary when two objects can start, reset or pause
independently. Combining those into one phase would change behavior. The
module maps group this state by native owner rather than by hooked function.
There is no need for a global timer registry or a clock for every function.

The common native-counter helper detects resets and inactive gaps. The older
scene adapter delegates to it; it no longer has a second implementation of
reset/advance logic. Result-panel queue handling belongs to the result module.

## Boundaries for further patches

Trace a counter's producer, reset and duration consumers before changing it.
Prefer a shared producer or existing animation engine boundary when it covers
multiple callers. Hook a consumer separately only when its semantics require
it, such as an exact-frame sound cue that could otherwise be skipped.

Keep input polling and native scene work on every update. Authored units such
as 60 ticks per second describe durations, not a 60 Hz cap on the game. Preserve
the distinction between one-shot events and conditions that remain true at an
animation label. Attempt counts, input edges and list indices remain counts.

Addresses, signatures and native structure offsets stay in `game_profile.h`.
Native-image tests cover shared aliases, independent resets, duplicate entry,
stalls and several refresh rates. The [audit](frame-dependency-audit.md) records
coverage gaps; isolated fixtures do not establish complete live-game behavior.
