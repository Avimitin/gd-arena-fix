# gd-display-mod — experimental GITADORA Delta display/timing hook

External x64 DLL loaded through Spice2x `-k`. Version 0.1.4 is a prototype.
The original game DLL is never edited on disk.

## Supported build

`gdxg.dll` SHA-256:
`0ed4aabc75f5bd997c237759d5fcf17143aa69073110b5c9235d1515cf0fd32c`

The mod checks the file hash and the instruction bytes it hooks. A mismatch
disables initialization and writes the reason to `gd-display-mod.log`.
Existing game patches are allowed if they leave these hook sites intact.
Use the matching installation's AFP/AVS libraries.

## Run

Extract the binary package anywhere writable. Start with the 60 FPS mode:

```powershell
.\Start-Gitadora.ps1 -Contents 'D:\M32-007-2025122301\contents' -Mode 60
```

After checking menus, gameplay layout, movies, and the auxiliary display,
try the experimental mode:

```powershell
.\Start-Gitadora.ps1 -Contents 'D:\M32-007-2025122301\contents' -Mode 120
```

The helper follows this installation's GuitarFreaks launch arguments.
It saves the selected mode to the mod's adjacent INI, sets per-process
environment variables, adds the matching
`-graphics-force-refresh` value, and loads the hook with `-k`.
It does not edit Spice configuration or the original batch files.
For DrumMania or another configuration, use your own launch command.

Version 0.1.1 fixes a launcher handoff problem observed in testing: the
display switched to 120 Hz while the DLL retained its 60 FPS configuration
and 14 ms wait, producing about 68 FPS. The mode now persists in the mod's
INI so it survives a launcher restart that loses environment variables.
It remains selected until the next launch with a different `-Mode`.
To check the complete configuration handoff without launching the game,
add `-CheckConfig`; this tests the native reader both with and without
the inherited environment variables.

Manual integration: append `-k "C:\path\gd-display-mod.dll"` to the
normal launch command and set the adjacent INI. Use `-k`, not the early
`-z` option. The 120 FPS mode requires all three INI settings:

```ini
[display]
Width=1920
Height=1080
Refresh=120
FPS=120
Experimental=1
```

Also change an existing `-graphics-force-refresh 60` override to 120.
Keep other required hook DLLs in the command, for example
`-k ifs_hook.dll -k "C:\path\gd-display-mod.dll"`.
Command-line DLL hooks may override the list configured in SpiceCfg.
The helper accepts additional flags through `-ExtraSpiceArguments`.
Use `-ServerUrl 'http://localhost:8083/'` to select the local test server.

Windowed output follows the desktop's refresh rate; the DLL cannot make a
60 Hz desktop display 120 distinct frames. Fullscreen requires the driver
to enumerate 1920x1080 at the requested refresh. Disable conflicting
resolution/rotation overrides while testing this prototype.

Removing this DLL's `-k` argument returns to the original behavior on the
next launch. Hot unloading is not supported; the DLL is pinned once active.

## What it changes

Both modes:
- Intercept the game's Direct3DCreate9Ex import, preserving Spice's wrapper.
- Override only the main output to 1920x1080 at the selected refresh.
- Give the game a separate 3840x2160 logical backbuffer through the device
  and main swapchain interfaces.
- Downsample the completed main canvas just before the game presents.
- Recreate the canvas on Reset/ResetEx.
- Match the actual main window's client size to 1080p in fullscreen and
  request that size in windowed mode, including Reset/ResetEx, so pointer
  input spans the displayed image. Windows may constrain decorated windows;
  windowed presentation and input use that actual client size.
- Log presentation-call cadence and copy/presentation costs every five seconds.

The internal main renderer remains 4K, and auxiliary targets retain their
original sizes. This version does not deliver native-1080p rendering savings.
It is intended to establish layout correctness first.

Version 0.1.3 fixes a fullscreen input sizing omission observed on DrumMania.
The game opens a 4K HWND and uses `D3DCREATE_NOWINDOWCHANGES`; earlier builds
resized only windowed HWNDs when switching the output to 1080p. Its native
mouse/touch handler divides coordinates by `GetClientRect`, so the oversized
fullscreen client could restrict the pointer to half the logical range.
The mod now resizes the main client on creation and reset, positions
fullscreen output on the selected adapter's monitor, and logs the resulting
input dimensions. Mouse/touch values are not multiplied or patched.
Auxiliary panel windows retain their existing sizes and input behavior.

Experimental 120 FPS mode additionally:
- Removes the normal frame's 14 ms wait, retaining display synchronization.
- Supplies a measured delta to the normal-frame AFP animation call.
- Redirects the two song-filter division operands to a private 120.0 value;
  the game's shared 60.0 constant is untouched.
- Converts identified +5-per-call audio/song fallback advances to elapsed
  time with fractional 300-Hz tick accumulation.
- Hooks the normal frame to measure elapsed time.

The song filter's compensation targets a stable 120 FPS cadence.
Dropped frames and variable frame rates need further validation.
Per-update scene/draw counters have NOT all been converted: transitions,
timeouts, effects, and gameplay behavior may still run at incorrect speeds.
Judgment accuracy, keysound synchronization, fullscreen behavior, and
compatibility with the installed Spice wrappers require an actual game test.
A 120 Hz display mode and 120 Present callbacks do not prove 120 distinct,
correctly timed game frames.

## Optional Live2D performance setting

Version 0.1.4 adds an optional switch in the INI beside the loaded DLL:

```ini
[display]
DisableLive2D=1
```

`1` prevents creation of the optional left/right cabinet Live2D characters
in menus and normal gameplay. It uses the game's existing zero-character
branches, avoiding those models' animation and drawing work. `0` follows
the game's normal character settings; this is the package default.
Restart the game after changing the value. The PowerShell launcher preserves
this preference across launches and 60/120 mode changes. Advanced integrations
can override it with the process environment variable `GDMOD_DisableLive2D`.

The patch changes three guarded instruction sites in memory. It does not
change saved character selections, disable whole side-screen outputs, or
stop the global Cubism update/cleanup machinery. Special scene characters
and cut-ins using separate creation paths remain enabled. This scope avoids
blocking their motion-completion callbacks. Both 60 and 120 FPS support it.

For a versioned DLL, the INI must have the matching basename, for example
`gd-display-mod-0.1.4.dll` reads `gd-display-mod-0.1.4.ini`.
The log reports `DisableLive2D=1` and the three guarded sites when enabled.
Actual gameplay behavior and the FPS benefit still require an in-game test;
these checks do not establish a measured speedup.

## Verification performed

Built with MSVC x64 and a Windows SDK. No compiler warnings.

The included `gd-mod-test.exe` is a hidden-window Direct3D smoke test:
- Driver enumeration found 1920x1080@120 on adapter 0.
- Device and swapchain GetBackBuffer both expose the 4K canvas.
- Four colored quadrants survive downsampling to a 1080p output.
- ResetEx recreates the canvas and the same pixel checks pass.
- A hidden fullscreen-style 4K window is corrected to a 1080p client;
  right/bottom pointer normalization reaches the full game input range.
- CreateDeviceEx and ResetEx restore the main input dimensions, including
  after an artificially oversized window; the target monitor origin is checked.
- Fractional tick accumulation preserves 300 units per second.
- Equal elapsed time produces equal AFP advancement at 60 and 120 calls/sec.
- The hash gate rejects an unsupported executable.

An additional test maps game sections into an isolated process without
loading the game's dependencies:

```powershell
.\gd-mod-test.exe --verify-image 'D:\M32-007-2025122301\contents\modules\gdxg.dll'
# Optional final arguments: DisableLive2D (0/1), FPS (60/120).
.\gd-mod-test.exe --verify-image 'D:\M32-007-2025122301\contents\modules\gdxg.dll' 1 120
```

It verifies exact instruction guards, MinHook relocation/activation including
the note diagnostics, the selected timing patches, and unchanged shared
constants and original file hash. The Live2D checks passed for all four
combinations of 60/120 FPS and switch off/on. They exercise the two real
character-pair builders on their empty-character paths with synthetic state,
check the patched gameplay ID instructions, and reject a conflicting byte
at each of the three sites before modifying the game image. They do not
load models or exercise actual game scene transitions.

Launcher checks also passed with both switch values at both frame rates,
with and without inherited environment variables. INI comments, unrelated
keys, and other sections survive mode changes.

These tests passed. The initial 120 Hz boot still used the old 60 FPS
timing configuration; that launcher issue was fixed in 0.1.1.
The corrected build now logs approximately 120 presentation callbacks per
second. The user reports no noticeable timing mismatch, but reports notes
becoming consistently faint at higher fall speed. Visual correctness is
still under investigation; this is not a confirmed transparency fix.

## Note transparency diagnostics

Add `-Diagnostics` to the launcher to observe the GuitarFreaks note path.
This works with either `-Mode 120` or `-Mode 60`. Normal launches leave these
extra hooks disabled. The diagnostic hooks pass all original arguments,
including note alpha, through unchanged.

Every five seconds the log records note batch cadence, repeated versus
changed song positions, alpha ranges for upcoming note heads inside the
renderer's Y bounds, the internal speed scalar, and two fade-option flags.
Post-judgment notes are counted separately because their fade is intentional.
Zero note counts mean this path supplied no qualifying notes; they do not
prove that all notes are opaque or that the renderer is broken.

With the game focused, press **Ctrl+Shift+F8** to save paired `notes-*-4k.bmp`
and `notes-*-1080p.bmp` files beside the DLL, from the same completed frame.
The captures contain the logical canvas and its downsampled output before
presentation. Reading GPU surfaces can cause a brief pause.

For this installation, `contents\gita-note-diagnostic.bat` starts diagnostic
120 FPS with `http://localhost:8083/`. An optional `60` argument runs the
comparison at 60 FPS. Capture at a low speed and at the speed where the
faintness appears, ideally on the same chart with the same visibility options.
The ordinary `gita-1080p120.bat` remains available for normal play.

The diagnostic build also passes guarded MinHook installation checks for
all seven hook targets in the isolated game image. Its Direct3D test verifies
the screenshot format, orientation, and RGB contents in addition to scaling.

## Build

Install Visual Studio 2022 with the x64 C++ tools and a Windows SDK.
Run `build.cmd`; output is written to `dist`.

MinHook is vendored, so a normal clone includes all required source files.
No game files are needed to compile or package the mod.

After building, create the binary/source ZIPs with Python 3.10 or newer:

```powershell
py -3 package.py --output-dir artifacts
```

The output directory contains `gd-display-mod-0.1.4-binary.zip`,
`gd-display-mod-0.1.4-source.zip`, and `SHA256SUMS.txt`. The binary ZIP
includes a per-file `SHA256.json` manifest and the conservative default INI.
The packager verifies ZIP integrity and the packaged manifest before exiting.
Omit `--output-dir` to write the archives to the source directory.

### GitHub Actions

The [Build distribution workflow](.github/workflows/build.yml) runs on pushes,
pull requests, and manual dispatch. It compiles the x64 DLL and test executable
on `windows-2022`, checks the default INI, and builds both ZIPs. Download the
binary ZIP, source ZIP, or checksums from the successful run's **Artifacts**
section. Artifacts are retained for 30 days.

The workflow uses commit-pinned GitHub actions and read-only repository
permissions. It uploads build artifacts; it does not create GitHub releases.
Game-image validation and the Direct3D smoke test remain local checks because
they require the supported game image or a suitable graphics environment.

If using a portable Microsoft SDK NuGet package, set these before building:

```text
GD_SDK_HEADERS=<Microsoft.Windows.SDK.CPP>/c/Include/<version>
GD_SDK_LIBROOT=<Microsoft.Windows.SDK.CPP.x64>/c
```

Source is MIT licensed. MinHook v1.3.4 is included under its own license,
at commit `c3fcafdc10146beb5919319d0683e44e3c30d537`.
The package contains no game DLL, IDA database, or game assets.

Implementation references:
- [Spice2x launcher and -k option](https://github.com/spice2x/spice2x.github.io/tree/main/src/spice2x/launcher)
- [MinHook](https://github.com/TsudaKageyu/minhook/tree/v1.3.4)
- [D3D9 presentation parameters](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent-parameters)
