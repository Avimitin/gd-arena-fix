# Frame-dependency coverage audit

The expanded static scan and IDA inspection found five additional counters in
three scene paths. These now use the existing elapsed counter adapter; see
[Scene wait timing](remaining-frame-dependencies.md) for producer, consumer,
caller and native-test evidence. Further candidates and indirect paths remain
unclassified. This inventory records coverage and priorities
so further work does not depend only on player bug reports. Addresses refer to
the supported image documented in [Timing](timing.md).

## Method and scope

`tests/frame_dependency_scan.py` reads the original PE without executing it.
The expanded inventory combines 18,880 `.text` unwind ranges with 66,171
nonoverlapping IDA function spans absent from unwind metadata. It decodes
1,238,031 instructions and finds 5,922 candidate writes across 1,763 ranges.
These are candidates, **not 1,763 confirmed bugs**. MSVC can split one function
into several chained ranges. Decode stops leave 15,540 bytes in 324 recorded
spans unresolved; these may include embedded data as well as code.

Direct calls, conditional/unconditional branches and contiguous range
fallthrough produce 1,096 ranges reachable from the normal frame, 2,327 from
the song scene, and 134 from the drum worker. Respectively, 153, 322 and 9
contain candidate writes. These sets overlap. Lifecycle initialization and
four nested factory tables resolve 81 scene records with 303 callback entries
(including service screens). Their combined direct reachability covers 5,392
ranges, 816 with candidate writes.
Resolved update/draw callbacks have been inspected for reads of the shared
controller age. This is not a semantic classification of every private field
or nested callback. Initialization and exit callbacks are not all classified.
Remaining indirect scene callbacks, vtables, undiscovered/undecoded code,
inline arithmetic not matching the heuristic, and external DLLs remain
outside the reachability proof. Nearby register writes are heuristic matches,
not data-flow proofs. The IDA inventory is bound to the image SHA-256.

IDA MCP cross-references, decompilation and instruction-level inspection were
used to validate the representative paths below. AVX pseudocode alone is not
reliable for classifying units. Float constants such as 60 can be geometry,
BPM conversion or authored duration; they must not be changed globally.

## Coverage map

| System | Evidence | Current status |
| --- | --- | --- |
| Session handshake wait | `2D170F` increments `1B00991C8`, compares `>30`; scene `0x35` | Converted with the existing counter adapter. Native deadline tested at fixed/variable cadences and across stalls. |
| Scene `0x39` request/lifetime ages | `24E45D`, `24E467`; consumers `24E6DC`, `24FAA9` | Converted request timeout and minimum lifetime, preserving independent request resets and continuous fractional age across state changes. |
| Scene `0x41` hold and close fallback | `25ADA7`, `25AD37` inside `25ACD0` | Converted both deadlines; native tests verify per-call input/completion polling, same-frame release/repress, stalls and expiry. Exact screen label remains unresolved. |
| Shared menu countdown | `280FE0`, `281170`, `2D46E0`; state `1B008B2C0`; native boundary every 80 calls | Converted to elapsed seconds, with native pause/reset/hold/UI behavior. |
| Timed-play duration | `1F3170`, decrement `1F31B5`, remaining ticks `182927BB4` | Converted to elapsed 60-unit time; other work still runs each frame. |
| Song position/filter | `1B60F0`, cursor call `1B6152`, `286EC0` | Uses the existing audio sample; frame-independent elapsed fallback. |
| Drum hit judgment/keysounds | Worker `150F80`, callback `1FE4D0`, raw position `164FE0` | Audio-worker path already independent of main rendering. Actual latency, locking and scheduling need measurement. |
| AFP normal timeline | `1A3F2D` loads fixed step; import return `1A3F3B` | Measured QPC delta supplied at this call only. Other AFP entry paths remain to audit. |
| Native object animations | `185E70`, `1869D0`, `185230` | Optional elapsed authored ticks and interpolation. Full elapsed time is retained; stateful callbacks still require ordered catch-up. |
| GF/DM sprite-effect batches | `3B81C0`, `3B8290`, helper `3B8120` | Individual records have elapsed clocks. Batches run every render; new effects start immediately. Atlas phases skip arithmetically after stalls. |
| DM eight-tick hit counters | `3B6150` writes `1B04C3784`; its code callers are inside `289EF0` | Covered by the existing outer gameplay compensation when enabled. Avoid a second correction on the helper. |
| Scene update/draw age | Main `1A3CD8` increments object `+18`; `1A491E` increments `+1C` | Audited resolved update/draw callbacks. Four service-screen blink/phase consumers now read elapsed sidecar time. Native zero/first-update checks keep their ordinal semantics. Indirect/unresolved consumers remain a coverage limit. |
| Song-start delay | `2876DE..287700`: increments `1B008B46C`, compares `>20`, advances state 3 to 4 | Converted. Native-code tests verify 21/60 seconds at multiple cadences, stall crossing and the immediate bypass. |
| Procedural color phase | `1F332A` increments `1AF6BEC40`; `283480`/`2834E0` feed it to RGB phase helpers `283680`/`283770` using `%180` and `/60` | Converted to elapsed phase. Session/report ages updated by the same helper are also corrected. |
| Result color/effect phase | `3932C0` reads `182F5A678`; `390590` used from `396EF0` | Converted with the regular-result clock; periodic cabinet output crosses elapsed boundaries once. |
| Result tally | `3ABA60`; age `1B04C2460` | Converted. Six one-shot cues, row count-ups and timeout use elapsed time. Obsolete narration coalesces after stalls. |
| Regular results | `396EF0`; age `182F583C0`, state `182F5A658` | Converted identified age consumers, exact-age cues, score/gauge count-ups, two-player reveal countdowns and queued sounds. Native input and rendering remain per call. |
| Battle results | `3A4180`, outer exit in `3A6200`; age `1B04C1404` | Converted phases, property reveals, roulette, periodic awards, cue sequences and countdowns. Arithmetic catches up without losing awards. Event counts for wins/losses and awards remain native. |
| Cabinet light patterns | `0DAD00`, nested `0DA300`; color/brightness ages and eight-tick pattern index | Converted. Output stays per call, pattern dispatch crosses elapsed boundaries once and coalesces stalls. Native tests cover wraps and duplicate calls. |
| Secondary scene/controller timers | `3C7FE0`: local `+1C` increments at `3C996D`; equality at 20, transitions after 180/300; local `+24` countdowns | Converted with an elapsed age, one-shot crossing at 20 and saturating countdowns. Native tests cover transitions, equality crossing, full-rate input and stalls. Caller is `2C0A70`; the apparent data-table reference is EH metadata, not evidence of another callback. Exact player-facing scene label remains unresolved. |
| Option-preview lane movement | `3D7580` arrays at `+49C8`, wrap -120 to 1080; `3D6D90` converts positions to float | Converted, including the separate 600-tick preview-effect schedule. Three draw sites interpolate fractional position in mode 2; missed effects coalesce without losing phase. Full native update tests cover both players, all ten scroll phases and twenty effect timers; complete screen execution still needs live validation. |
| Short/long lane hit effects | `3836AB`, `383831` inside `3834C0`; lifetimes 8/13 ticks | Converted, with immediate first response and elapsed expiry. Comparisons retain native flags; tests cover deadlines and stalls. |
| GF short effect/combo hold | `365FF0` age `182F531D8`; `383F00` countdown `1808F9800` | Converted individual lifetimes without throttling the surrounding callbacks. |
| DM shutter | `3B57E0`; notification `1B04C37BC`, fades `1B04C37C0` | Converted one-second notification and two ten-tick fades. Mode 2 samples fractional opacity each render. |
| Ending cue | `3B8620`; age `1808F74F8` | Converted zero/90-tick cue to one-shot elapsed crossing. Native skip/input routines still run every invocation. |
| GF procedural visual ages/HUD | `36F2AF`, `3710AC`; HUD `3A8110` | Converted identified elapsed/phase increments, HUD wrap and per-player 39-tick effects. Other GF procedural arithmetic remains in the queue. |
| GF shutter/status overlays | `362080`, per-player stride 112 | Converted 180-tick hold, two float fades, status ages, eight-tick feedback and 362-tick effect. Native tests cover both players and expiry-crossing stalls. |
| Song controller waits | `2B5760`, `2BA060`, `2B2540`, `2B1B40`, `28A940` | Converted load/disconnect/state waits, special and ordinary readiness paths, per-player holds, exit/network delays and visual phase. Native update ordinals stay native. |
| Judgment feedback/debug holds | `2717D0`, `275190`, called from `35C070` | Converted the identified 480/120/60-tick cooldowns and holds. Note/event totals and audio-position comparisons remain native. |
| Service screens and I/O | `1D0050`, `1CCF70`, `1D9D00`, `1D9F30`, `1CF810` | Converted dialog waits, word countdowns, I/O holds and 30 Hz output schedule. Zero-entry initialization remains immediate; stale output polls coalesce. |
| System/startup/entry waits | `1E4FE0`, `229230`, `2299D0`, `22A650`, `22B1D0`, `22F380`, `2279F0`, `2400F0`, `23B040`, `2C24F0`, `2FE8F0` | Identified counters use the profile's `system_counters` table. Includes the shared entry countdown and per-player completion waits. Private login countdowns are listed separately below. Complete startup and entry flows still need live validation. |
| Private login/registration countdowns | `2BAB40`, `2BB420`, `2BC3D0`, `2BD3E0`, `2BE530`, `2BFDE0`, `2C0A70` | 30 decrement sites use elapsed time; 11 digit gates compare native displayed seconds; the exact-1200 warning uses a crossing. Full `2BFDE0` timeout/input/UI fixture and all descriptor operand tests pass at multiple cadences. Other complete callbacks require live validation. |
| Service button gestures | `205AB0` and alias, `220020`, `221C60`, `4733C0` | Converted confirmation holds and gesture windows, retaining input edges on every call. Native gesture tests cover continuous age across states 3/4. |
| Tutorial | `2DE520`, countdown `1B009B558`, practice position `1B009B560` | Converted instruction countdown, 300-unit practice clock, six local/countdown sites, gauge easing and 30 exact-value cue comparisons. Scripted pause windows integrate intervals that cross their boundaries during a stall. Native tests cover timing and cue behavior with inactive player fixtures; active-player judgment/drawing still needs validation. |
| Summary results | `3AF420`, phase `1808F9824` | Converted overall/stage ages, native age-based row count-ups, eight periodic chime gates and the exact-60 reveal. Native tests exercise row arithmetic, chimes and exit waits. |
| Drum tutorial | `2E4B20` | Converted countdown, practice position, pause integration, 35 exact-time cues, seven cabinet pulse sites, input/effect delays and gauge recurrence. Native inactive-player tests pass; active-player validation remains open. |
| Result panel | `3ADCA0` | Converted two ages and queued sound countdowns; native tests check elapsed expiry and one sound after a stall. |
| Loading spinner | `1F5A10`, age step `1F5C02` | Converted atlas age and fade; native tests cover duplicate calls and stalled fade. |
| Shared overlays / AFP holds | `1FA6D0` and standalone aliases | Converted clamped opening/closing age, character hide wait and two notice countdowns. Native completion predicates retain exact terminal states. |
| Movie lifecycle and card removal | `250ED0`, `251240`, `251330`, `28E710`, `261740` | Converted identified waits, recorded play duration and animation-label cues; this does not certify movie decoder internals. |
| Diagnostic schedules | `1CEE60`, `233D00` | QC phase/brightness and card-error retry deadlines use elapsed time; missed I/O work coalesces. Device polling and input-edge counts remain native. |
| Card-reader startup | `2346B0`, alternate error updater `234320` | Both slot startup deadlines and alternate error age converted; native tests cover the thirty-second timeout and stalls. |
| Mini-game | `3FBF40`, `3FA1E0`, `3F8B60`, `3F82C0` | Identified movement, acceleration, atlases, recovery, spawn/death cadence, particle turn/spin and banner lifetime converted. Native tests check multiple cadences and full-rate input; live collision/motion validation remains open. |
| Arena stage/gesture waits | `345710` through `347260` | Shared age, 300-tick event, gesture windows and player voice countdowns converted. Handler/signature coverage; complete scene execution remains to validate. |
| External animation-label events | Arena and card sound/voice/stop consumers; selection cue; two gameplay completion queries; GF icon/number predicates | Shared authored-frame visit history handles held and skipped frames, with separate level predicates and one-shot events. Remaining scene consumers require classification. |
| GF combo-number easing | `36F603` inside `36F090` | Existing seven-step display transition follows elapsed time; native target/slope calculation and per-call digit handling remain. Arithmetic is compared with the original instruction block. |
| Battle/lobby controller | `26B750`, inner `2685F0`; match/retry waits, send interval and chat timestamp | Converted. Native tests verify deadlines and one restart after a long stall. Message sequence numbers and retry-attempt counts remain event counts. |
| Periodic bookkeeping | Main `1A3C45..1A3C61` formerly checked frame counter modulo 3600 before `1B9800` | Converted to one elapsed-minute invocation; other callers remain immediate. Stalls coalesce because the native routine uses timestamps internally. |
| Native load/I/O timeout | `14DDC0` compares AVS time differences against 30000 and measures each operation | Already time-based on this path; retry counts are attempts, not seconds. |
| Allocation bookkeeping | `14FE60` increments/decrements block count while coalescing memory | False positive. Must remain an event count. |
| Audio effect parameters | `1C64D0`/`1C65E0` divide a supplied parameter by 60; initialization calls from `1C68F0` | Not evidence of a per-render clock. Leave unchanged until parameter units and consumers are traced. |
| In-song option repeat and fall-speed transition | `389A20`, `389CF0`, getter `389960` | Converted eight repeat channels and two 16-tick speed transitions. New edges/releases remain immediate; repeats coalesce after stalls. Mode 2 draws fractional transition samples. Other menu repeat/debounce paths remain unresolved. |
| GuitarFreaks judgment, movie decoder, retained Live2D special scenes, network-library timeouts | External/indirect paths | Not certified by this pass. |

Short code addresses in the table have preferred base `0x180000000` added;
full global addresses are written explicitly.

## Remaining conversion queue

Follow the [timing architecture](timing-architecture.md): adapt confirmed
dependencies in the existing engine, share its clock owners, and preserve
native input/judgment/state-machine execution.

1. Remaining animation-label consumers in scene code: distinguish one-shot
   cues/transitions from held-label waits. The first twelve arena cues now use
   recorded frame crossings; this does not cover every external consumer.
2. Remaining nested selection/option/entry callbacks and private counters.
   Converted shared clocks do not certify every callback in the factory tables.
3. Live mini-game collision/motion comparison, active-player tutorial behavior,
   and reset/retrigger cases. Tutorial position-based judgment helpers have
   been traced, and its short practice cue window is converted.
4. Remaining indirect/undecoded/initialization paths, AFP entry paths, GuitarFreaks
   judgment/keysound scheduling, movie and retained Live2D. Then perform live
   scene/song validation and inspect an updated candidate inventory.

`3AA840` increments `1B04C23C0` and `1B04C243C`, but the observed xrefs
only reset/increment these values; no duration consumer is established.
`36F090`'s `1808F9690` counts observed song-position changes rather than
unconditionally advancing per render. These are recorded separately from
confirmed elapsed clocks.

Additional exclusions verified during this pass: `1CD8D0` increments counters
only when the device event sequence changes. `2AA100`'s `1B008C648` counts
configuration operations under the shared dialog's zero-entry marker.
`22F380`'s high word at `1AFFC9074` is changed inside its one-time initialization
branch and compared with the audio source; it is not the scene's per-frame
delay (`1AFFC9078`). None of these was converted into a wall clock.

Implementation and test details for the later groups are in
[System and tutorial timing](system-and-tutorial-timing.md).
Further evidence is in [Mini-game and frame events](minigame-and-frame-events.md).
The GF label predicates and display transition are described in
[Guitar HUD timing](guitar-hud-timing.md).
Earlier producer/consumer findings and false positives are recorded in
[Overlay and lifecycle timing](overlay-and-lifecycle-timing.md).

Countdowns, music and animation clocks retain full elapsed time across stalls.
The former 100 ms animation cap has been removed. Sprite atlas catch-up is
arithmetic; stateful native object callbacks replay in order and can be expensive
after a long stall. QPC does not make execution immune to interruption.

The image tests now exercise native regular/battle results and shutter/ending
paths at 95/120/144/240/360 Hz. Battle score/property output is compared against
native authored-step arithmetic; award conservation is checked across stalls.
Some effect hooks have handler/signature coverage only. Tests replace graphics,
sound and network dependencies and do not certify complete live game behavior.

## Reproduce

Install the developer dependencies `pefile` and `capstone`, then run:

```powershell
py -3 tests/frame_dependency_scan.py 'C:/path/to/gdxg.dll' --output evidence/frame-dependencies.json
```

The JSON contains addresses, instruction descriptions and candidate categories,
not game bytes. It is generated evidence and is ignored by Git. A clean scan
does not imply full coverage: undiscovered/indirect paths and semantic false
positives must remain explicit in subsequent audit updates.

For an optional annotation of actual patch locations, export
`gd-arena-fix-test.exe --dump-timing-sites` to a UTF-8 JSON file and supply it
with `--patch-plan`. The annotation reports physical instruction overlap only.
It does not certify semantic coverage or infer coverage from a global address
appearing in the header: profiles also contain read-only test/support addresses.
Function wrappers and branch bypasses require separate attribution.

To include the IDA spans used by the expanded pass, supply `--ida-functions
evidence/ida-functions.json`. Its format and the expanded command are documented
in [Scene wait timing](remaining-frame-dependencies.md).
