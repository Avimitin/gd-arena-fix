# Overlay, lifecycle and tutorial timing evidence

Addresses below are preferred VAs with `0x180000000` omitted for code only.
The executable profile remains the source of patch addresses and signatures.

## What a frame dependency means

The confirmed paths usually do not query the display's refresh rate. They
increment or decrement a field once per update, with durations authored for
60 updates per second. Calling the same update 120 times per second therefore
halves that duration. A counter being present is not sufficient evidence:
allocation counts, input counts and loop indices must retain their meaning.

Multiple patch sites can update the same clock. For example, `250EF4` and four
sites in `28E710` update `1AFFCAC2C`. They share one elapsed counter keyed by
the value's address; alternate callers do not each accumulate another delta.
Likewise, one native animation engine clock covers many UI objects. Its pose
samplers are not independent timers.

## Additional confirmed paths

| Path | Native behavior | Elapsed behavior |
| --- | --- | --- |
| Drum tutorial `2E4B20` | Countdown minus one; practice position plus five; 35 exact-time cues; periodic cabinet pulses | 60-unit countdown, 300-unit practice position, cue crossings, integrated pause windows and coalesced overdue narration |
| Drum tutorial gauge | Demonstration target ramps and displayed integer easing advance together once per call | Both advance through the same authored recurrence; rendering may sample a fractional result |
| Result panel `3ADCA0` | Stage/overall ages and queued sound countdowns advance per call | Elapsed ages and saturating countdowns; queued sound fires once on expiry |
| Lifecycle prologue `228B90` | Increments its wait field twice while below ten | Retains the actual five-authored-tick duration using 120 units per second |
| Initialization `230C90`, cue in `22F380` | Startup age plus one; exact cue at 4320 | Elapsed age and one-shot crossing at 72 seconds |
| Movie lifecycle `250ED0`, `251240`, `251330`, `28E710` | Poll age, recorded play duration divided by 60, exit delay | Shared elapsed ages, including all observed poll aliases; decoder internals are a separate audit item |
| Card removal `261740` | Reader release ages, animation timeout and exact voice/end label checks | Shared per-reader elapsed ages; authored-frame visits bridge skipped labels |
| Selection `247720` | Private object age and sound at a label plus three | Elapsed object age and one-shot sound crossing |
| Options preview `3D7580` | Ten scrolling phases and twenty repeating effects per player | Existing preview hooks retain periods and overshoot; added full native-function tests exercise both players |
| Shared overlay `1FA6D0` | Opening age plus one, closing age minus one; most opening styles clamp at 45 | Elapsed age, immediate zero-entry consumption, exact terminal clamp and zero; equality-based completion queries remain valid |
| AFP character hide | Age reaches 480 in `1FA6D0` or `1FC7A0` | One shared elapsed clock across both callers |
| AFP notices | Two hold fields decremented in the shared controller and standalone aliases `27E980`, `27E320`, `389830` | One elapsed countdown per field across every identified producer |

The options preview was already handled in `scene_timing.cpp`. An audit pass
proposed duplicate hooks there; those duplicates were removed before deployment.
The installer now rejects overlapping patch ranges across inline hooks, function
detours and byte patches before applying the transaction. Synthetic tests cover
duplicate, contained and partially overlapping ranges; adjacent ranges are valid.

## Animation consumers and exclusions

`36DCB0` and `3B8570` have callers in gameplay/song transitions. Their completion
queries retain level semantics when a label is held, and can observe a label
crossed during catch-up. A one-shot sound cue instead consumes a visit once.
Missing-object / negative-label sentinel behavior is preserved.

`2815B0`, `358E40`, `358ED0`, `372DC0` have no confirmed executable callers or
exports in this image. Apparent references were `.pdata` entries or chained
unwind metadata in `.rdata`. Raw pointer/RVA searches corroborated this. They
are not evidence of script bindings and receive no hooks. Indirect runtime
construction remains a general static-analysis limitation.

Within the animation engine, `185040` constructs objects; `1869D0`, `186DA0`,
`187370` and `187610` sample pose/color data at a supplied frame. `187C90` maps
child time from its parent's frame. The observed caller of `1860A0` is the
already hooked update `185E70`; it recursively updates children and processes
authored callbacks. Its exact-label dispatch executes inside ordered catch-up,
so it must not receive a second elapsed correction. `186360` has only an unwind
reference and no established live caller. These findings do not certify every
engine entry path.

`1F8200`'s equality at 45 is a terminal-state check, not a one-shot event.
The overlay age must clamp to 45; changing this check into an event would alter
the behavior of repeated callers. The high-word modulo counter at `1AFEC38DC`
has no observed duration consumer and is left unchanged.

Other exclusions verified from their producers and readers include resource
allocation/queue counts (`180890`, `235BA1`, `2427D0`, `25ED69`, `42DFCD`),
database loop totals (`2661FE`), event statistics (`1EF500`, `2717D0`, `2798C0`,
`34C76B`, `3809D0`), network attempts/sequences (`268628`, `494E20`, `495030`,
`4A0D70`), and input-gated configuration operations (`1DCDB0`, `20F950`,
`212F30`, `347621`). They are not converted into wall clocks.

The global-step cross-check also distinguishes `1E50AD` and `1E5174` from the
session timer's real increment sites: those branches initialize a new state
with the constant one. `22004D` changes a selected menu item after an input,
and `1D9D00` / `1D9F30` change a dialog state in the low word; their timed holds
use separate high-word fields. Neither should be converted as an elapsed age.

The only observed reference to the AFP ordinal-15 update import is `1A3F35`,
the call already receiving measured elapsed time. This bounds that import's
callers in `gdxg.dll`; it does not prove how other AFP exports or dynamically
resolved entry points behave inside the external libraries.

## Validation limits

The image fixture executes the original native routines where dependencies can
be isolated, replacing graphics/audio/network endpoints. Tests exercise several
cadences, duplicate calls and long stalls. DM tutorial tests cover narration,
practice position, pause windows, cabinet pulses, gauge recurrence and input
polling with inactive player fixtures. Active-player judgment/drawing and complete
live scene flows still need validation. Several new service and lifecycle sites
have signature/handler coverage rather than complete scene execution.

All elapsed time is retained. This does not make input handling run while the
game thread is suspended, nor guarantee that sampled collision tests observe
everything between two updates. See the remaining queue in
[the audit](frame-dependency-audit.md).
