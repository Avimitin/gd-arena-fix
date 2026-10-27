# Scene wait timing

Static inspection found **five counters in three scene paths** that counted
update calls against fixed duration thresholds. All five now use the existing
elapsed counter adapter in `system_timing.cpp`, alongside the previously
converted system waits. Input and native state updates still run every call.
These fixes activate above 60 FPS, independently of `AnimationTiming`.

Image: `gdxg.dll`, SHA-256
`0ed4aabc75f5bd997c237759d5fcf17143aa69073110b5c9235d1515cf0fd32c`.
Addresses below are preferred virtual addresses with base `0x180000000`.
Findings were checked with IDA MCP decompilation/xrefs and original PE
instructions, then compared with the current hook profile and patch plan.

## Native duration rules

| Path | Native duration rule | Stock at 60 updates/s | Uncorrected at 120 updates/s |
| --- | --- | --- | --- |
| Session handshake, scene `0x35` | Increment age, leave state 3 when `age > 30` | 31 calls, about 0.517 s | About 0.258 s |
| Request timeout, scene `0x39` | Increment request age, select timeout state when `age > 1200` | 1,201 calls, about 20.017 s | About 10.008 s |
| Minimum scene lifetime, scene `0x39` | Increment total age; state 17 stays active while `age < 60` | 60 calls, about 1 s | About 0.5 s |
| Held input, scene `0x41` | Increment while held; enter closing state at `age >= 180` | 180 calls, about 3 s | About 1.5 s |
| Close fallback, scene `0x41` | Increment in state 3; clear active flag when `age > 120` | 121 calls, about 2.017 s | About 1.008 s |

Times are derived from call counts, assuming one callback per frame and a zero
starting counter; they are not live measurements. Input, an earlier completion
condition or a state change can finish a wait sooner. The minimum lifetime
guard counts the whole scene lifetime, not a fresh one-second wait on entry
to state 17. Exact player-facing names for scenes `0x39` and `0x41` are unresolved.
The adapter retains the native immediate first increment, then accumulates
elapsed authored ticks. Native-code tests compare the interval from that first
call to expiry against the stock 60 Hz interval, within one rendered frame.

### Session handshake

Scene descriptor `0x1808C2260` registers update `0x180243B20`, which calls
`0x1802D1380` at `0x180243B52`. The latter contains session-manager send/sync
calls. In state 3:

```text
0x1802D1709  load age from 0x1B00991C8
0x1802D170F  increment age
0x1802D1711  store age to 0x1B00991C8
0x1802D1717  compare age with 30
0x1802D171A  remain waiting if age <= 30
0x1802D1720  set state at 0x1B0099148 to 4
```

The decrements in its following state consume event/credit counts. They must
not be included merely because they are adjacent to this timer.

### Request and scene lifetime

Descriptor `0x1807D86B0` registers scene `0x39`, init `0x18024FB60`, update
`0x18024E420`. Initialization clears the containing object. Each active update
increments request age `0x1AFFC9CC8` at `0x18024E45D`, storing at
`0x18024E469`, and total age `0x1AFFC9CCC` at `0x18024E467`, storing at
`0x18024E470`.

`0x18024E6DC` compares request age with 1200. Values above it set state 12.
The native routine resets request age during request setup, including
`0x18024E564`, `0x18024E78C` and wider state/age stores. These resets matter:
one scene age cannot replace every request deadline.

`0x18024FAA9` compares total age with 60 in state 17 to decide whether the
callback remains active. Both producers now use elapsed time without tying
their clock identity to the state value: ordinary state transitions must not
discard fractional time or add another initial tick.

### Hold and close fallback

Descriptor `0x1807D8890` registers scene `0x41`, update `0x18025AF30`.
It calls `0x18025ACD0` with object `0x182EFAF68`.

In state 2, the routine polls `0x180442800` for both players every call.
When either is held, `0x18025ADA7` increments object `+8`; `0x18025ADAA`
compares it with 180 and starts closing when that threshold is reached.
Releasing resets `+8` to zero.

In state 3, a native completion predicate can clear the active flag. The
fallback at `0x18025AD37` increments object `+0xC`; `0x18025AD3A` compares
it with 120, clearing the flag above that value. Both producers now use elapsed
increments. The displaced comparisons retain the native signed branch behavior.

## Values that should remain unchanged

- Normal-frame Live2D processing subtracts the previous native timestamp
  (`0x195E2D618`), converts the difference to seconds and supplies it to
  `0x1800A9B40`. This entry path already has elapsed time. Special entry paths
  and internal stepping are not certified by that observation.
- `0x18040C1B0`, `0x18040DA90`, `0x18040F230` and `0x180410370` use `60/BPM`
  while building Live2D command sequences from chart records. Their list and
  record counters are not a render clock. `0x180490E50` also converts BPM.
- `0x180273D50` uses a double 60 as a window around music-position data, not
  as an increment to a per-frame clock. `0x1803B5E40` uses 60 in geometry.
- `0x18020D480`, `0x18020E2C0` and `0x180211BC0` contain menu selection and
  configuration increments driven by input events. `0x1801D9450` is an input
  edge/position test, not a timer.
- `0x180234AC0` decrements a card-reader FIFO count after removing an item.
  `0x1800C9560`/`0x1800C9740` increment a Live2D string identifier.
- The original AFP fixed delta and the shared color-phase `/60` reads still
  appear in the original binary. Their active producers/call sites already
  have mod hooks; their mere presence does not establish a remaining bug.

## Scan coverage and reproduction

`tests/frame_dependency_scan.py` combines the PE unwind inventory with
optional nonoverlapping IDA spans. The expanded pass decoded 1,238,031
instructions, yielding 5,922 candidate writes in 1,763 ranges. Candidates
include unit increments, LEA-based increments, scalar float updates and
references to selected timing constants. These counts include covered sites
and false positives; they are not counts of clocks or required hooks.

The scan resolves 81 scene descriptors and 303 callbacks. Direct conditional
branches across split unwind ranges are included alongside calls, jumps and
fallthrough. Indirect calls, undecoded/undiscovered code, external DLLs and
arithmetic outside the heuristic remain limitations. There are 324 undecoded
span suffixes totaling 15,540 bytes; some may be embedded data. Semantic
inspection of all candidates is not complete.

The expanded inventory has 55 references to the selected constants. It finds
no exact `1/60` or `1000/60` float/double references in decoded instructions;
the slightly different native AFP step is found explicitly. This does not
exclude frame-based timers: the confirmed gaps above use integer increments.

Export IDA function addresses/sizes (for example using IDA MCP `func_query`),
bind the export to the original image hash, and save:

```json
{
  "sha256": "<SHA-256 of the analyzed DLL>",
  "functions": [{"addr": "0x180001000", "size": "0x20"}]
}
```

Addresses/sizes are hexadecimal strings. Names and other fields are optional
and ignored. Spans overlapping unwind ranges are not added; conflicting
spans are rejected from the supplementary inventory. A hash mismatch aborts.

```powershell
py tests/frame_dependency_scan.py 'C:/path/to/gdxg.dll' `
  --ida-functions evidence/ida-functions.json `
  --patch-plan evidence/timing-patches.json `
  --output evidence/frame-dependencies-expanded.json
```

The patch plan comes from `gd-arena-fix-test.exe --dump-timing-sites` and is
optional. Physical overlap is only a hint; wrappers, branch bypasses and
shared producers still require inspection. Generated inventories remain in
the ignored `evidence/` directory.

## Implementation and validation

Five entries in `Profile::system_counters` use the shared elapsed-frame context
and existing `ElapsedNativeCounter`. No new time source, scheduler or state
machine was added. Addresses, instruction signatures and native fixture layouts
remain in `src/game_profile.h`.

Two instruction forms preserve the operations displaced beside the increments:
`IncR8LoadRdx` also sign-extends the native scene state into RDX;
`IncEcxStoreR8` also stores the updated request age from R8. The two hooks are
adjacent but disjoint. IDA xrefs show only normal fallthrough into their
interior instructions. The existing `IncEax` and `IncMemoryCmp` forms cover the
other three sites.

`native_remaining_wait_test` executes the original scene routines with only
external input/controller dependencies stubbed. It first measures all five
stock call thresholds with elapsed correction inactive, then verifies:

- The same deadlines at 60/75/90/95/120/144/165/240/360 Hz and a repeating
  variable-cadence sequence, including duplicate calls in a single frame.
- Deadline crossing after stalls, and saturation without signed wrap after an
  extreme elapsed interval.
- Both players polled every call, one closing trigger, immediate release/repress
  and native completion within the current frame.
- Fractional ages retained across state changes; a request reset does not reset
  total lifetime; inactive scenes accumulate no backlog.

The image verification passes with FPS/AnimationTiming pairs `240/2`, `120/1`,
`144/0` and `60/0`; 60 FPS retains the native hooks-disabled path. Instruction
guards, patch overlap checks, shared-clock arithmetic and bridge ABI checks
also pass. These are isolated native-code tests, not a live cabinet validation
of the complete scene, network or graphics behavior.
