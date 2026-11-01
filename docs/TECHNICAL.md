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

Frame clock: all frames (the tick's own frame and the extra frames) follow one steady
clock of `1000 / MaxFps` ms. After the update the tick's frame waits for its slot (before
the surface is locked) and is drawn with the matching interpolation factor; extra frames
take the following slots until the next tick is due. With `Debug=1` the log contains a
histogram of frame intervals (4 ms buckets) every 5 seconds.

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

- **Rotation** (interiors, Alt + middle mouse button) — angle at `cam+0x28` (2^32 = one
  turn) and the projection matrix at `cam+0x38` (see `0x7e1c60`: `x = c*px + s*py + T0`,
  `y = s*kB*px - c*kB*py + ... + T1`, rows at `+0x38/+0x3C`, `+0x44/+0x48`,
  `+0x50/+0x54`, translation `+0x5C/+0x60`; screen = (proj - pos) * `+0x70`). The angle
  is interpolated, the matrix rebuilt, and the position chosen so the ground point in the
  middle of the view stays in place (the game rotates around it). Interiors are rendered
  in 3D into an image (`cam+0xA0`) only when the camera's changed flag `cam+0x124` is set
  (world draw `0x7ea8a7`), once per tick; during a rotation the flag is set for extra
  frames so the room is rendered with the interpolated angle. While the middle button is
  held the cursor is not polled in extra frames (the rotation uses the cursor movement).
- **Tilt** (peeking through a door up/down) — the z column of the matrix (`+0x40/+0x4C/+0x58`)
  and the row factors kA/kB change; they are interpolated with the angle. The view-centre
  pivot is only used in the main view (at least half the screen wide) with a usable tilt;
  small viewers (door peek window) interpolate the position linearly, as they turn around
  another point.

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
- Inventory pick-up shine (bonus books): the inventory manager's draw `0x8a00b0` advances
  the effect (`0x92fd70`, call `0x8a01b9`: sweep offset +6 per step, then fade level -4)
  and draws it (`0x92fe20`, call `0x8a01d3`) on every draw. The step only runs in tick
  frames; extra frames draw an in-between frame made with the effect's frame generator
  `0x92fbc0` (pure: returns a new image) and delete it afterwards.
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

### Game bug: holes in the map when zoomed out

The map tile cache is set up at `0x858730`: (screen tiles + 1) × zoom-out factor
columns and rows, with the columns capped at 24 (`mov eax, 0x18` at `0x8587ce`, matching
the blitter arrays above). Zoomed out at 1920×1080 about 62 columns are visible; tiles
outside the cache are skipped by the blitter (tile state `!= 2`), leaving holes that show
old frame contents. Since large blits are split, the cap is raised to 96 columns.

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

### Loading screens

The mission loading screen (object `[0xBCBB14]`, begin `0x4079d0` called at `0x44c901`;
generic "Loading, please wait" begin `0x4075c0` called at `0x44d4c5`/`0x44e076`) draws a
fixed 640×480 layout and presents frames itself. Originally the display was 640×480
whenever it appeared from the pause menu; with the HD pause menu, and in the mission
(quick save / load), it appeared in a corner or small in the middle. Before it starts the
patch switches the video mode to 640×480 with the video object's own mode change
(`[0xC15BDC]`: `0x8ed510` get, `0x8ed530` set) — the game's SetMode `0x402a00` would also
notify the half-loaded mission (`0x7920d0`) and crash. After the update, if the game is
still on the same screen (quick save), the previous mode is restored; otherwise the
briefing / mission set their resolution. `CMAJuego::Activate` (`0x7abc10`) keeps 640×480
while a mission reload is pending (byte `+9`, reload `0x447640` at `0x7abc46`) and switches
to the game resolution afterwards.

## Diagnostics

Always on: a vectored exception handler logs access violations inside the game code
(address, registers and the last map-background blits) to `Commandos2-ModernPatch.log`
before the game terminates.

`Debug=2`: heap scan for RTTI objects, per-extra-frame memory diffs with owner
attribution, automatic captures in menus/inventory and hardware watchpoints (DR0–DR3 via
`SetThreadContext` + vectored exception handler) logging the writing instruction and
the call stack.

## Cheats

The mission input handler `0x4a87b0` (called at `0x4477ae`) passes typed characters
(`WM_CHAR`) to a password matcher (`0x5ca430` on `0xBCF134`, call at `0x4a8826`); on a
match it sets `CJuego+0xEA`, which enables the cheat keys (table `0xB933A0`, 7 entries of
0x1C bytes: modifiers, key, handler, arguments). The Steam build changed the password
from `GONZOANDJON` (original, Legacy) to `PYROFOREVER`. The patch replaces the matcher
to accept both in any letter case, and with `Cheats=1` sets the flag before every input
event.

## Configuration file

`GetPrivateProfile*` does not understand a UTF-8 byte order mark: the section header
would not match and every setting would silently fall back to its default. A BOM is
stripped from the ini at startup.

## Performance: water rendering

Water (`CBitmapAgua`, vtable `0xB1B508`) is drawn by a software polygon rasterizer
(`0x834000`–`0x83b000`) every frame, so with extra frames its cost scales with the frame
rate. Two hot functions are replaced (`Optimizations=1`, default):

- `0x839840` — texture coordinates at a polygon edge for a scanline (x87 code with a
  manual float-to-int truncation) — SSE version.
- `0x83a2d0` — the water span renderer for one run of the water mask on a screen row
  (256×256 8-bit texture through a 16-bit palette); the original keeps its pixel loop
  state in global variables. Literal C translation with the loop in registers.

Both are compared with the originals on random inputs at startup and only applied if
the results are identical.

## Profiler

`Profile=1` (development only) starts a thread that samples the main thread's EIP about
every millisecond and logs the hottest game code addresses (16-byte buckets) and, for
samples outside the game, the nearest game return address, every 10 seconds.

## Performance: map background cache

The camera's map draw `0x7e7de0` (thiscall(canvas, context), called from the world
draw at `0x7eaeab` before the objects) draws the background tiles and the animated
water. Between two logic ticks its output only depends on the camera, so the patch
keeps a copy of what the tick frame drew (per camera, canvas clip rectangle) and, in
extra frames whose camera state (position, zoom, view size, flags) and canvas are
identical, copies it back instead of drawing again. While the camera moves everything
is drawn normally. Part of `Optimizations=1`.

### Game bug: quick clicks ignored in the inventory

The game detects double clicks itself (`0x7967b0`): a left click within the Windows
double-click time and 5 pixels of the previous one becomes event `0x208` instead of
`0x202`. The inventory grid (`CVentanaCuadriculaInv`, handler `0x930790`) only reacts to
`0x202`, so quickly picking up and dropping an item in the same place did nothing. Its
switch maps `event - 0x201` through the byte table at `0x93092C`; the patch sends `0x208`
to the `0x202` case, as the game already does for `0x20A`/`0x204`. Applied independent of
`Enabled`.
