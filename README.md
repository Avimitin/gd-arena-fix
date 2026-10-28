# GITADORA Arena Fix

A GITADORA Delta display and frame-rate mod loaded through spice2x.

## Features

- Native 720p, 1080p, 1440p and 4K main-screen output with high-refresh support, including 120, 144 and 240 Hz.
- Elapsed-time menu countdowns and an audio-based song clock above 60 Hz.
- Animation-speed correction and smoother native animation at higher frame rates, enabled by default.
- Smoother GuitarFreaks and DrumMania note motion when audio position updates arrive in steps.
- Correct cursor reach across the full screen.
- Main rendering follows the selected output resolution to reduce GPU work at lower resolutions.
- Left/right Live2D characters and side video/effect compositing disabled by default, keeping main and touch output.

The mod changes memory only; the original game DLL stays intact.

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

Choose a resolution and refresh rate your display supports. Match any existing
`-graphics-force-refresh` argument to that rate. In windowed mode, match the
Windows desktop refresh rate too. Higher-rate modes still need gameplay testing.

For audio setup, see the [spice2x Arena audio guide](https://github.com/spice2x/spice2x.github.io/wiki/GITADORA-Arena-Model#audio).
With ASIO, try **128 samples or lower** in your audio driver's control panel.
Choose the smallest supported buffer that plays without crackling or dropouts,
then restart the game after changing it. With WASAPI, start with the device
defaults. High-refresh rendering does not require a particular audio buffer size.

Our test environment:

| Component | Configuration |
| --- | --- |
| CPU | Intel Core i5-12490F |
| Audio interface | TOPPING E4x4 Pre, using the TOPPING Pro USB Audio Device ASIO driver |
| ASIO buffer | **8 samples** |

If the mod does not activate, check `gd-arena-fix.log` beside the DLL for
unsupported settings, an unsupported game build, or a conflicting patch.
Remove this DLL's `-k` argument to stop loading the mod.

## INI settings

All settings belong under `[display]` in the INI beside the DLL. The files must
have matching names: `gd-arena-fix.dll` reads `gd-arena-fix.ini`.
The packaged INI defaults to 1920x1080 at 120 Hz with animation and note smoothing enabled.
Restart the game after changing a setting.

```ini
[display]
Width=1920
Height=1080
Refresh=120
DisableLive2D=1
DisableSideCompositing=1
AnimationTiming=2
NoteSmoothing=1
Diagnostics=0
```

| Field | Default | Meaning |
| --- | --- | --- |
| `Width` | `1920` | Main-screen rendering and output width. Use one of the resolution pairs below. |
| `Height` | `1080` | Main-screen rendering and output height. Use one of the resolution pairs below. |
| `Refresh` | `120` | Requested display rate in Hz, at least `60`. Above `60`, enables high-refresh timing fixes. Fullscreen requires display support; in windowed mode match the desktop rate. Actual FPS depends on hardware and spice2x settings. |
| `DisableLive2D` | `1` | `1` disables the optional left/right cabinet characters in menus and normal gameplay. Set both this and `DisableSideCompositing` to `0` to follow the game's character settings. Saved selections are preserved; special scene characters and cut-ins can still appear. |
| `DisableSideCompositing` | `1` | `1` skips video/effect compositing for the left/right cabinet screens and also disables their optional characters. Use `0` if those screens are needed. |
| `AnimationTiming` | `2` | Above 60 Hz: `0` leaves native animation speed unchanged; `1` corrects supported animation and sprite-effect speed; `2` also smooths supported movement and fades. Sprite artwork retains its original frame count. |
| `NoteSmoothing` | `1` | Smooths GuitarFreaks and DrumMania note movement above 60 Hz while following song time. Audio playback and hit judgment are unchanged. Use `0` to draw directly from the reported song position. |
| `Diagnostics` | `0` | `1` enables extra note/selection logging and **Ctrl+Shift+F8** screenshots while the game is focused. Captures are saved beside the DLL and may briefly pause rendering. Leave at `0` for normal play. |

Supported `Width` / `Height` pairs:

| Output | Width | Height |
| --- | --- | --- |
| 720p | `1280` | `720` |
| 1080p | `1920` | `1080` |
| 1440p | `2560` | `1440` |
| 4K | `3840` | `2160` |

For a 60 Hz display, use:

```ini
Refresh=60
AnimationTiming=0
```

For 144 or 240 Hz, set `Refresh` to that rate.
