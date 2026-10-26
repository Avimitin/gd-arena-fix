# GITADORA Arena Fix

A GITADORA Delta display and frame-rate mod loaded through spice2x.

## Features

- 1920×1080 main-screen output at 60 or 120 Hz.
- Experimental 120 FPS support.
- Correct cursor reach across the full 1080p screen.
- Optional removal of the left/right Live2D characters to reduce rendering and animation work.

The game still renders internally at 4K and scales to 1080p. The mod applies
changes in memory; the original game DLL stays intact.

## Download and install

1. Open the [latest release](https://github.com/Avimitin/gd-arena-fix/releases/latest)
   and download the asset ending in **`-binary.zip`**.
2. Extract `gd-arena-fix.dll` and `gd-arena-fix.ini` into your game's
   `contents\modules` directory, keeping both files together.
3. Append this argument to your existing spice2x launch command, keeping your
   other arguments and DLL hooks:

   ```bat
   -k gd-arena-fix.dll
   ```

4. Edit the INI as described below, then start the game using your usual script.

For 120 Hz, your display must support 1920×1080 at 120 Hz. If your launch command
already uses `-graphics-force-refresh`, set it to `120`. In windowed mode, set
the Windows desktop refresh rate to 120 Hz as well.

If the mod does not activate, check `gd-arena-fix.log` beside the DLL for
unsupported settings, an unsupported game build, or a conflicting patch.
Remove this DLL's `-k` argument to stop loading the mod.

## INI settings

All settings belong under `[display]` in the INI beside the DLL. The files must
have matching names: `gd-arena-fix.dll` reads `gd-arena-fix.ini`.
Restart the game after changing a setting.

```ini
[display]
Width=1920
Height=1080
Refresh=60
FPS=60
Experimental=0
DisableLive2D=0
Diagnostics=0
```

| Field | Default | Meaning |
| --- | --- | --- |
| `Width` | `1920` | Main-screen output width. Only `1920` is supported. |
| `Height` | `1080` | Main-screen output height. Only `1080` is supported. |
| `Refresh` | `60` | Requested fullscreen refresh rate: `60` or `120` Hz. |
| `FPS` | `60` | Target game frame rate: `60` or `120`. Use the same value as `Refresh`. |
| `Experimental` | `0` | Set to `1` to allow 120 FPS. That mode also requires `FPS=120` and `Refresh=120`; timing and visual issues may still occur. |
| `DisableLive2D` | `0` | `1` disables the optional left/right cabinet characters in menus and normal gameplay. `0` follows the game's settings. Saved selections are preserved; special scene characters and cut-ins can still appear. |
| `Diagnostics` | `0` | `1` enables extra GuitarFreaks note logging and **Ctrl+Shift+F8** screenshots while the game is focused. Captures are saved beside the DLL and may briefly pause rendering. Leave at `0` for normal play. |

To enable 1080p at 120 Hz / 120 FPS, change these three fields together:

```ini
Refresh=120
FPS=120
Experimental=1
```
