# Development

## Build

Install Visual Studio 2022 with the x64 C++ tools and a Windows SDK, then run
`build.cmd` from the checkout. MinHook is vendored; a normal clone contains
all required source. No game files are needed to build the mod.

The build produces `dist/gd-arena-fix.dll`, its default INI, a PDB, and
`dist/gd-arena-fix-test.exe`. The test executable is a development tool and
is excluded from the binary distribution.

For a portable SDK installation, set these environment variables before
running the build:

```text
GD_SDK_HEADERS=<Microsoft.Windows.SDK.CPP>/c/Include/<SDK version>
GD_SDK_LIBROOT=<Microsoft.Windows.SDK.CPP.x64>/c
GD_SDK_BIN=<Microsoft.Windows.SDK.CPP>/c/bin/<SDK version>/x64
```

`GD_SDK_BIN` supplies the resource compiler. `src/licenses.rc` embeds the
project license and the complete MinHook/HDE notices as `GD_LICENSE` and
`MINHOOK_LICENSE` RCDATA resources in the DLL.

## Packaging

With Python 3.10 or newer, run:

```powershell
py -3 package.py --output-dir artifacts
```

The binary ZIP contains exactly two files at its root: `gd-arena-fix.dll`
and `gd-arena-fix.ini`. Packaging always takes the INI from the source root,
so a developer's active `dist` settings cannot change the shipped defaults.
The packager checks embedded license text, archive integrity, the exact
binary file list, and the archived payload hashes.

The source ZIP includes code, tests, build scripts, documentation, the
workflow, and dependency notices. `SHA256SUMS.txt` is a separate file listing
the hashes of both ZIPs. The package version is set in `package.py`.
Omit `--output-dir` to place these outputs in the source directory.

## GitHub Actions

`.github/workflows/build.yml` runs on pushes, pull requests, and manual
dispatch using `windows-2022`. It builds the DLL and test executable,
checks the default INI, packages both ZIPs, and uploads the ZIPs and checksums
as separate artifacts. Actions are pinned to commits and use read-only
repository permissions. Artifact retention is 30 days.

CI does not run the game or the Direct3D smoke test. Those checks require
the supported game image or a suitable graphics environment.

## Local checks

On a fresh build, check the public defaults without starting the game:

```powershell
.\dist\gd-arena-fix-test.exe --check-config 60 0
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

The last two arguments are `DisableLive2D` (`0`/`1`) and FPS (`60`/`120`).
Run all four combinations after changes to the character patches. The test
checks instruction guards, MinHook relocation, the selected timing patches,
unchanged shared constants and game-file hash, and retained character IDs.
It executes two real character-pair builders with synthetic empty state and
the patched gameplay ID block. Each character patch's conflict guard must
abort before installing hooks. It does not load models or test scene changes.

## Note diagnostics

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

- [Implementation and limitations](implementation.md)
- [Spice2x launcher](https://github.com/spice2x/spice2x.github.io/tree/main/src/spice2x/launcher)
- [MinHook](https://github.com/TsudaKageyu/minhook/tree/v1.3.4)
- [D3D9 presentation parameters](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent-parameters)
