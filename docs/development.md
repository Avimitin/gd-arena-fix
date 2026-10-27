# Development

## Build

Install Visual Studio 2022 with the x64 C++ tools, a Windows SDK, and Python
3.10 or newer. MinHook is vendored; no game files are needed to build.

From an **x64 Native Tools Command Prompt**, install the developer tools and run:

```bat
python -m pip install -r requirements-dev.txt
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

The preset uses Ninja, C++20, a static MSVC runtime, and an optimized build with
debug symbols. `build.cmd` is a convenience wrapper that sets up the x64 compiler
environment and invokes the same CMake preset. For IDE or local overrides, use
an ignored `CMakeUserPresets.json`.

The build produces `dist/gd-arena-fix.dll`, its default INI, a PDB, and
`dist/gd-arena-fix-test.exe`. The test executable is a development tool and
is excluded from the binary distribution.

For a portable SDK installation, set these environment variables before
running `build.cmd`:

```text
GD_SDK_HEADERS=<Microsoft.Windows.SDK.CPP>/c/Include/<SDK version>
GD_SDK_LIBROOT=<Microsoft.Windows.SDK.CPP.x64>/c
GD_SDK_BIN=<Microsoft.Windows.SDK.CPP>/c/bin/<SDK version>/x64
```

`GD_SDK_BIN` supplies the resource compiler. `src/licenses.rc` embeds the
project license and the complete MinHook/HDE notices as `GD_LICENSE` and
`MINHOOK_LICENSE` RCDATA resources in the DLL.

## Formatting

The developer requirements pin clang-format so local and CI output agree.
Run `python tools/format.py` to format project C++ or add `--check` to check it
without changes. CMake exposes the same commands as `format` and `format-check`
targets. Vendored MinHook and assembly are excluded; include order is preserved.

## Packaging

With Python 3.10 or newer, run:

```powershell
cmake --build --preset windows-release --target dist-zip
```

Alternatively, run `python package.py --output-dir artifacts`; `--dist-dir`
selects a custom CMake output directory. The binary ZIP contains exactly two
files at its root: `gd-arena-fix.dll` and `gd-arena-fix.ini`.
Packaging always takes the INI from the source root,
so a developer's active `dist` settings cannot change the shipped defaults.
The packager checks embedded license text, archive integrity, the exact
binary file list, and the archived payload hashes.

GitHub provides source downloads automatically. `SHA256SUMS.txt` lists the
binary ZIP hash. The package version is set in `package.py`.
Omit `--output-dir` to place these outputs in the source directory.

## Game build profiles

[`src/game_profile.h`](../src/game_profile.h) is the single source for game
addresses, instruction signatures, replacement bytes and native layout offsets.
Addresses use IDA's preferred virtual addresses; `at()` relocates them against
the selected profile's image base. The DLL selects a profile once by SHA-256
before installing any game hooks. Unknown images are rejected.

For a new game build, verify the corresponding routines in IDA, add a named
`Profile` in that header, and register it in `supported`. Keep version checks out
of hook implementations. The common `layout` section describes the currently
supported native ABI; a changed object layout or calling convention requires
new verification, not just a replacement hash. Build support is not established
until instruction guards and the native image fixtures pass for that image.

The Python probes read the same literal profile definitions. Run
`py -3 tests/profile_data.py` to check that game addresses have not escaped into
other source or test files; this check also runs in CI. Direct3D/Win32 API slots
and generic x64 bridge instructions are independent of game build profiles.

See the [frame-dependency audit](frame-dependency-audit.md) for confirmed
coverage, remaining paths and the reproducible scanner.

## GitHub Actions

`.github/workflows/build.yml` runs only on tag pushes using `windows-2022`.
It checks formatting, builds with CMake, runs CTest, packages both
ZIPs, and uploads the ZIPs and checksums as separate artifacts with 30-day
retention. After all checks pass, it also uploads both ZIPs and
`SHA256SUMS.txt` to the pushed tag's GitHub Release, creating the release if
needed. Actions are pinned to commits. The workflow uses the built-in
`GITHUB_TOKEN` with `contents: write` permission to publish release assets;
no additional secret is needed.

Before tagging a release, update `VERSION` in `package.py` to match the
release version, since that value determines the ZIP filenames.

CI does not run the game or the Direct3D smoke test. Those checks require
the supported game image or a suitable graphics environment.

## Local checks

On a fresh build, check the public 120 Hz / 120 FPS defaults without starting
the game:

```powershell
.\dist\gd-arena-fix-test.exe --check-config 120 0
```

The final argument is the expected `DisableLive2D` value. This check uses
the same INI reader as the DLL. `GDMOD_<field>` environment variables override
the corresponding INI values; clear them when checking file-only defaults.

Run the native graphics checks on a machine with a Direct3D 9Ex adapter:

```powershell
.\dist\gd-arena-fix-test.exe
```

This checks fractional timing arithmetic, unsupported-image rejection,
device/swapchain access to the logical 4K canvas, four-quadrant downsampling,
BMP color and format, reset behavior, and main-window input dimensions.
A hidden fullscreen-style window verifies pointer normalization and monitor
placement; it is not an actual fullscreen game test.

The game-image check maps file-backed sections without loading the game's
dependencies. Supply your own supported `gdxg.dll`:

```powershell
.\dist\gd-arena-fix-test.exe --verify-image 'C:\path\to\gdxg.dll' 1 120
```

The trailing arguments are `DisableLive2D` (`0`/`1`), FPS (e.g. `60`/`120`/`144`/`240`), and
optional `AnimationTiming` (`0`/`1`/`2`, default `2` in this test command).
Run all four combinations after changes to the character patches. The test
checks instruction guards, MinHook relocation, the selected timing patches,
unchanged shared constants and game-file hash, and retained character IDs.
It executes two real character-pair builders with synthetic empty state and
the patched gameplay ID block. Each character patch's conflict guard must
abort before installing hooks. It does not load models or test scene changes.

With animation hooks enabled, the test also executes the real native
animation evaluator, recursive updater, GF/DM sprite batches and standalone
sprite updater. A substitute draw function observes fractional movement and
alpha without a graphics device. These tests check callback frequency,
pose restoration, manual overrides, stopped animations, native register
preservation and avoidance of double throttling. Modes `0`, `1`, `2` and
the 60 FPS path should all be checked after animation changes.

`--test-animation-clock` checks elapsed-time accumulation at several render
rates, jitter, inactive objects and bounded catch-up without a game image.
This arithmetic check also runs in CI. Detailed scope is in
[Animation timing](animations.md).

`--test-timing` checks configuration and elapsed-time clocks at 60/90/120/144/
240/360 Hz, duplicates, pauses, stalls and inactive scenes without a game image.
It also runs in CI. Above 60 FPS, `--verify-image` executes the native menu
countdown aliases, reset/clear functions, audio sampler, cursor lookup and song
filter using synthetic state. The timed-play wrapper uses a substitute callback
for its unrelated frame work. Run at 120/144/240 with animation modes 0/1/2 after
clock changes. See [Timing](timing.md) for exact coverage and hardware limits.

## Diagnostics

Above 60 FPS, song-list and category readiness use each asset's `[loop, move)` label interval
instead of requiring a poll at one exact frame. AFP executes intermediate
actions during catch-up, but C++ sees only the final position; missing the
`loop` frame otherwise leaves a queued scroll permanently busy. Label lookup
needs no new timer or frame-number exception. Intro/closing phases, native
scroll motion and the other readiness ranges retain their existing behavior.

`Diagnostics=1` also logs shared song-list state and accepted/rejected scroll
requests at most once per second after scrolling input. These passive probes
help investigate stalls affecting both the main list and touchscreen grid.
They do not reset locks or change scrolling behavior.

`Diagnostics=1` adds three observation-only hooks to the GuitarFreaks note
path. Every five seconds they report note-batch cadence, repeated/changed
song positions, alpha ranges of upcoming note heads inside the lane, the
speed scalar, and two fade-option flags. Post-judgment notes are counted
separately. Zero counts do not establish that all notes are opaque; these
hooks do not cover DrumMania's note path.

With the game focused, Ctrl+Shift+F8 saves paired 4K and 1080p BMPs from the
same frame before presentation. GPU readback can briefly stall rendering.
Compare a low fall speed with the speed at which faintness appears on the
same chart, using the same visibility settings. The capture test verifies
24-bit BGR output and ignores alpha when writing the BMP.

## References

- [Timing, input, audio and judgment line](timing.md), including the earlier fixed-divisor filter probe.
- [Implementation and limitations](implementation.md)
- [Spice2x launcher](https://github.com/spice2x/spice2x.github.io/tree/main/src/spice2x/launcher)
- [MinHook](https://github.com/TsudaKageyu/minhook/tree/v1.3.4)
- [D3D9 presentation parameters](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent-parameters)
