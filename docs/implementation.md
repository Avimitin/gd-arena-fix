# Implementation and limitations

## Supported game image

The supported `gdxg.dll` has SHA-256:

```text
0ed4aabc75f5bd997c237759d5fcf17143aa69073110b5c9235d1515cf0fd32c
```

Initialization checks the on-disk hash and expected instruction bytes.
An unsupported image or a conflicting patch disables initialization and
records the reason in `gd-arena-fix.log`. Other patches can coexist if they
leave these sites intact. Use the matching game's AFP/AVS libraries.
Preferred addresses below assume image base `0x180000000`; the mod relocates
them through `gd::at()`.

## Initialization and display

DllMain swaps only the game's Direct3DCreate9Ex import. Configuration,
hashing, allocation, and hook installation run on the first D3D call outside
loader lock. The original factory is retained to preserve spice2x's wrapper.
The DLL is pinned once active; hot unloading is unsupported.

The main output uses 1920×1080 presentation parameters, while the game sees
a separate 3840×2160 render target through device and main-swapchain
GetBackBuffer calls. Immediately before the game's presentation callback,
the mod uses StretchRect to downsample that canvas. Reset and ResetEx
recreate it. Auxiliary render targets retain their original dimensions.
This does not provide native-1080p rendering savings.

COM hooks replace individual slots in the shared vtable and retain the
original methods. A limited clone of the public COM vtable is unsafe here:
the native runtime also uses private slots. A thread-local internal-call
guard permits access to the actual physical backbuffer without recursion.

The game's 4K main window uses D3DCREATE_NOWINDOWCHANGES. Its mouse/touch
handler normalizes positions with GetClientRect, so resizing only the
backbuffer can restrict pointer reach to half the logical range. The mod
fits the main client to 1080p on device creation and reset. Fullscreen is
positioned at the selected monitor's origin. Windows can constrain a
decorated window; presentation and input use its actual client size.
Auxiliary input windows are not resized.

## Frame timing

At 60 FPS the original timing logic is retained. The 120 FPS mode additionally:

- Removes the normal frame's 14 ms wait at `0x1801A39F1`, retaining display synchronization.
- Measures elapsed time in the normal-frame hook at `0x1801A3910`.
- Supplies that delta only to the normal-frame AFP ordinal-15 call.
- Redirects the two song-filter divisions at `0x180287060` and `0x180287133`
  to a private 120.0 constant, preserving the shared 60.0 value.
- Converts the identified audio/song fallback `+5` advances into elapsed
  time with fractional 300-Hz tick accumulation.

The main presentation hook is at `0x180420360`. It logs callback cadence,
maximum gap, and CPU time spent submitting the copy and presentation calls.
These call durations are not GPU timings.

Song-filter compensation assumes a stable 120 FPS cadence. Dropped frames
and variable frame rates need further validation. Per-update scene/draw
counters have not all been converted, so transitions, timeouts, effects,
and gameplay behavior can still run at incorrect speeds. A 120 Hz display
mode or 120 presentation callbacks does not prove 120 distinct, correctly
timed frames. Judgment accuracy, keysound synchronization, and visual
correctness require actual game tests.

Players have reported no noticeable timing mismatch after selecting the
correct 120 FPS configuration, but faint notes at higher fall speeds remain
under investigation. The mod does not claim to fix that behavior.

## Optional Live2D suppression

Zero character IDs already select the game's no-character branches.
`DisableLive2D=1` masks local IDs at three guarded instruction sites:

| Site | Behavior |
| --- | --- |
| `0x180306192` | Zero both menu character IDs; retain the pair builder's existing cleanup. |
| `0x180288DB4` | Zero both alternate gameplay IDs and test the masked register. |
| `0x18028B29B` | Replace two saved-ID reads with zero in the inlined gameplay pair creation. |

This avoids creating the optional left/right models and their command
queues. The saved selection globals remain intact. Global Cubism update,
initialization, and cleanup still run. Separate creation paths for special
scene characters and cut-ins remain enabled so their motion-completion
callbacks can finish. Other preloading and scene work can still cost CPU/GPU
time; the FPS benefit needs measurement on the target installation.

These patches join the timing patches' transactional installation and
rollback. Disabling the INI option leaves all three sites untouched.

## Source and validation

Project source is MIT licensed. MinHook v1.3.4 is vendored from commit
`c3fcafdc10146beb5919319d0683e44e3c30d537` under its own license, including
the HDE notices. The DLL embeds both license files as RCDATA resources.
Game files, assets, and IDA databases are excluded from distributions.

See [Development](development.md) for build instructions and the distinction
between local synthetic checks and actual game validation.
