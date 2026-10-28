# Notes and display brightness

## Dark fullscreen output

If the entire screen, including the Spice overlay, becomes dark only while the
game is focused, check HDR in the active fullscreen mode. An SDR desktop does
not guarantee that the fullscreen output also uses SDR.

Disable Windows HDR while that output mode is active, then restart the game.
The setting is managed by Windows; the DLL does not enforce it on launch.
Borderless windowed mode (`-w -windowborder 1`) can avoid the fullscreen color
mode switch by retaining the desktop output.

Desktop resolution and the signal sent to the display can differ: GPU scaling
can send a 4K signal from a 1080p desktop. Check **Active signal mode** in
Windows Advanced display settings when comparing modes.

## Thin or faint notes

With `NativeRendering=0`, the mod renders the logical 4K canvas and downsamples
it to 1080p with linear `StretchRect`. With `NativeRendering=1`, the main canvas
is rendered at 1080p and copied without downsampling. It does not change note opacity or size. The native note path
uses full alpha for upcoming notes when Hidden/Sudden options are disabled;
its width helper depends on lane position, mode and skin rather than fall
speed. This does not establish the opacity of final GPU pixels or perceived
brightness during motion. Speed-dependent note visibility remains unresolved.

## Diagnostics

Set `Diagnostics=1` to log note alpha, fall-speed scalar and visibility flags.
With the game focused, **Ctrl+Shift+F8** saves matched `notes-*-4k.bmp` and
`notes-*-1080p.bmp` captures beside the DLL. Native rendering uses
`notes-*-canvas.bmp` for the first image instead. Captures may briefly pause rendering.

Compare low and high fall speeds with the same skin and visibility options.
The paired images help distinguish source artwork from downsampling effects;
a static screenshot cannot measure display motion or the TV's brightness.
