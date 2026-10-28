# GITADORA Arena Fix

A GITADORA Delta display and frame-rate mod loaded through spice2x.

## Features

- 1920Ã—1080 main-screen output with experimental high-refresh support, including 120, 144 and 240 Hz.
- Elapsed-time menu countdowns and an audio-based song clock above 60 FPS.
- Animation-speed correction and smoother native animation at higher frame rates, enabled by default.
- Correct cursor reach across the full 1080p screen.
- Optional removal of the left/right Live2D characters to reduce rendering and animation work.

Optional native 1080p rendering reduces main-screen GPU work. It is still
being tested; the default keeps 4K rendering and scales to 1080p. The mod
changes memory only; the original game DLL stays intact.

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

Choose a refresh rate your display supports at 1920Ã—1080. Match any existing
`-graphics-force-refresh` argument to that rate. In windowed mode, match the
Windows desktop refresh rate too. Higher-rate modes still need gameplay testing.

If the mod does not activate, check `gd-arena-fix.log` beside the DLL for
unsupported settings, an unsupported game build, or a conflicting patch.
Remove this DLL's `-k` argument to stop loading the mod.

## INI settings

All settings belong under `[display]` in the INI beside the DLL. The files must
have matching names: `gd-arena-fix.dll` reads `gd-arena-fix.ini`.
The packaged INI defaults to 120 Hz / 120 FPS with animation smoothing enabled.
Restart the game after changing a setting.

```ini
[display]
Width=1920
Height=1080
Refresh=120
FPS=120
DisableLive2D=0
AnimationTiming=2
NativeRendering=0
Diagnostics=0
```

| Field | Default | Meaning |
| --- | --- | --- |
| `Width` | `1920` | Main-screen output width. Only `1920` is supported. |
| `Height` | `1080` | Main-screen output height. Only `1080` is supported. |
| `Refresh` | `120` | Requested fullscreen refresh rate in Hz, such as `60`, `120`, `144` or `240`. Requires display support. |
| `FPS` | `120` | Target frame rate, at least `60`. Use the same value as `Refresh`; actual performance depends on the hardware and spice2x settings. |
| `DisableLive2D` | `0` | `1` disables the optional left/right cabinet characters in menus and normal gameplay. `0` follows the game's settings. Saved selections are preserved; special scene characters and cut-ins can still appear. |
| `AnimationTiming` | `2` | Above 60 FPS: `0` leaves native animation speed unchanged; `1` corrects supported animation and sprite-effect speed; `2` also smooths supported movement and fades. Sprite artwork retains its original frame count. |
| `NativeRendering` | `0` | `1` renders the main screen at 1080p to reduce GPU work. Experimental; use `0` for the original 4K rendering and downsampling if graphics look wrong. Assets and auxiliary screens are unchanged. |
| `Diagnostics` | `0` | `1` enables extra note/selection logging and **Ctrl+Shift+F8** screenshots while the game is focused. Captures are saved beside the DLL and may briefly pause rendering. Leave at `0` for normal play. |

For a 60 Hz display, use:

```ini
Refresh=60
FPS=60
AnimationTiming=0
```

For 144 or 240 Hz, change both `Refresh` and `FPS` to that rate.
