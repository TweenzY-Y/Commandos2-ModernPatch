# Commandos2-ModernPatch — technical notes

Target: Steam `Comm2.exe` (8 379 904 bytes, SHA-256
`3645321656e05fab2cb91cff7d416152c9276d821ffef85cd0399bd4a15d1aaa`), 32-bit, fixed
image base `0x400000` (no ASLR). The executable has full MSVC RTTI (≈950 classes),
which was used to name objects; `src/rtti_vtables.h` maps vtable addresses to class
names for the diagnostics. Many code references go through an incremental-linking
jump table at `0x401000+`.

The mod is a proxy `ddraw.dll` (the game imports only `DirectDrawCreate`). At load it
checks the build (string `NoLimitedFps` at `0xAEC13C`, a known vtable entry) and
patches `call rel32` sites, vtable slots and a few function entries. It forwards
DirectDraw to cnc-ddraw (`Commandos2-ModernPatch\cnc-ddraw\ddraw.dll`, config passed through
`CNC_DDRAW_CONFIG_FILE`) or to the system `ddraw.dll`.

## Main loop

`WinMain` loop (≈`0x4051f0`–`0x40536f`), one iteration =

1. message pump `0x4027e0`, DirectInput/cursor update `0x8ed9d0`,
2. `CJuego::Update` `0x7956f0` — one logic tick (screen `vtbl[2]`; for the mission
   `0x44bc10`, tick counter `mission+0x5A4`),
3. canvas init `0x7caf00`, `Lock` `0x7cde80`,
4. `CJuego::Frame` `0x792e10` — screen `vtbl[3]` draw, cursor `0x81e990`, and the
   frame limiter `0x7922b0` (sleeps to `1000/25` ms; target fps `mission+0x5B0`),
5. `Unlock` `0x7cdf00`, `Present` `0x7ce430`, canvas reset `0x7cacd0`.

Globals: `CJuego*` `0xBCBAD4` (screen state `+0x2C`, transition `+0xEC`, limiter flag
`+0xC4`, cursor object `+0xD0`), mission `0xBD57EC`, adaptive frame skipping
`0xBB432C`. Screen state 1 = mission (`CMAJuego`), 7 = inventory (`CMAInventario`);
`0x790f40` returns the current screen object.

Rendering is software, 16-bit 565, into a locked DirectDraw surface; the WinMain stack
frame holds the canvas (`esp+0x44`), video object (`+0x5C`), lock info (`+0x14`) and
surface info (`+0xE8`).

## Frame pacing (hooks at `0x405239`, `0x40529b`, `0x40535c`)

- After `Update`: record the tick time on a fixed 40 ms schedule, clear adaptive
  frame skipping.
- `Frame`: disable the game's limiter (`CJuego+0xC4`), draw with interpolation
  factor 0.
- End of iteration: until the next tick is due, repeat the WinMain sequence
  (init canvas, lock, `Frame`, unlock, present) with a growing interpolation factor,
  then wait for the tick deadline.

## Interpolation

- **Camera** — world viewer `CVentanaVisorMundo` (vtable `0xAF37C4`), `vtbl[2]` is its
  draw. Camera object at `viewer+0xDC`: position `+0x68/+0x6C`, zoom `+0x74`. Replaced
  during the draw and restored.
- **Objects** — every renderable container is projected to the screen by `0x7f7240`
  (thiscall, 4 args; call sites `0x7e6960`, `0x7e8d80`, `0x80382f`) from its world
  position at `+0x14`. Projections are cached per draw id (`mission+0x634`), so extra
  frames re-project. Containers holding `CVisualGraficoPolygon` (`0xAF7024`, vision
  cones; visuals array `+0x8C`, count `+0x90`) or drawn at several positions within one
  tick are not interpolated. Jumps over 150 units snap.

## Per-draw animations (must not advance in extra frames)

Found with heap diffs and hardware write watchpoints (`Debug=2`):

- 3D portraits: `CVisorSecuencias3D` draw `0x92a5f0` advances one keyframe per call —
  extra frames jump to its draw-only tail `0x92a673`.
- Animated arrow cursor: `0x437d30` → `0x51c270` (15 frames, called from the mission draw
  at `0x44bd10`) — skipped in extra frames.
- Text input caret (save name, "keyed mission"): the text field draw `0x87f090` does
  `inc [this+0x1C]` per call and toggles the caret (`[this+0x20]`) every 4 calls — in
  extra frames the counter is pre-decremented (call sites `0x79c057`, `0x7b7ecf`,
  `0x7ba3de`, `0x7bb1f8`, `0x87f19a`).
- Briefings (`CMABriefing` `0xB1333C`): the map fly-over advances per draw — no extra
  frames on that screen.

## Mouse

All cursor reads use `GetCursorPos` (IAT `0xC19DF8`). The arrow sprite position is
polled once per tick by `0x402e00`; the mod calls it before every extra frame.
Inventory drag: inventory manager `[[0xBD57F0]]`, grid window `+0x4C`
(`CVentanaCuadriculaInv`, dragged item `+0xA0`, outline position `+0xA4/+0xA8`); the
item is drawn by the manager's draw `0x8a00b0` at `+0x58/+0x5C`. These are shifted by
the cursor movement since the tick during extra frames.

## Resolution

Video mode index at options (`0xBCBADC`) `+0x54`; sizes come from switch tables in
`0x413f20`–`0x4140b0` and `0x7b38c0`. Mode 3 (1280×720) immediates are patched to
`HDResolution`. Menus are fixed 640×480 (`0x414020`/`0x414030`); `CMAMenuInGame`
(`0xB176AC`) activation (`0x7ab1a9`, `0x7ab1b5`) is patched to keep the game resolution
— its draw (`0x7aaec0`) and the menu layout (`0x7ae010`) already centre a 640×480 menu
on larger screens.

### Game bug: crash when zooming at high resolutions

With a zoom other than 1.0 the map background is drawn by the scaled blitters
`0x856880` / `0x856ea0` (called from `0x857570` at `0x857851` / `0x85788f`; zoom 1.0
uses `0x854440`). They split the source area into 64×64 tiles and keep one entry per
tile column/row in fixed stack arrays: 24 columns (`esp+0x64`, 12-byte entries) and 22
rows (`esp+0x188`, 16-byte entries). At 1920×1080 a zoomed view spans ~28 columns, the
array overflows and the game crashes (also without the rest of the patch; 1280×720 stays
below the limit). The patch hooks both call sites and splits large blits into rectangles
of at most 16 tiles per side:

```
bool Blit(uint16_t* dst, int pitchPx, float srcX, float srcY, int w, int h, uint32_t extra)
```

(thiscall, `ret 0x1c`; `this+0x30` = source pixels per screen pixel, `this+0x20` =
source pixels per world unit; note that the function pushes `ecx` early, so its own
`[esp+...]` argument references are shifted by 4). This fix is applied whenever the
build check passes, independent of `Enabled`.

## Full-screen pause menu

The pause menu is told the screen is 640×480 (`GetScreenSize` `0x8ed510` at `0x7aaee8`
and, via `0x7ab077`, at `0x7ae026`), so it lays out in the top-left corner. Per frame:
the world is drawn in HD (`0x7aaed8`), the corner is saved; the translucent backdrop
blit (`0x7aaf61` → `0x7d3fe0`) is rendered once onto black and white to recover alpha
and premultiplied colour (cached, pre-scaled); menu windows are what differs from
"backdrop over world". After the frame the corner is restored, the backdrop is blended
over the HD world and the windows are drawn scaled. The arrow is drawn afterwards at the
real mouse position (`0x793199`, `0x791138`). `GetCursorPos` for logic (`0x8e1567`,
`0x8ed9f5`, thunk `0x4067dc`) and mouse messages (window subclass) are mapped back to
menu space; the arrow poll `0x402e41` is not, because the game also draws the arrow
immediately on mouse moves (`0x81ea20` → `0x81e830`).

## Diagnostics

Always on: a vectored exception handler logs access violations inside the game code
(address, registers and the last map-background blits) to `Commandos2-ModernPatch.log`
before the game terminates.

`Debug=2`: heap scan for RTTI objects, per-extra-frame memory diffs with owner
attribution, automatic captures in menus/inventory and hardware watchpoints (DR0–DR3 via
`SetThreadContext` + vectored exception handler) logging the writing instruction and
the call stack.
