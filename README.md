# NeoVim3D

![NeoVim3D typing a C++ program while the camera follows the cursor, with an LSP hover window floating above the code](docs/demo.webp)

NeoVim3D is a Neovim *frontend*. It starts `nvim --embed` and talks to it over
Neovim's msgpack-RPC UI protocol (`ext_linegrid` + `ext_multigrid`), the same way
GUIs like Neovide and nvim-qt do. That is why everything Neovim does just works:
keybindings, plugins, `:terminal`, LSP, and your existing `~/.config/nvim/init.lua`.

What NeoVim3D adds is the rendering:

* Every glyph is a real 3D mesh. Font outlines (FreeType) are triangulated and
  extruded, so text has depth, side walls and lighting.
* Every Neovim window is its own grid in 3D space. Floating windows (completion
  menus, LSP hovers, Telescope, …) hover above the text in their own layers.
* A camera you can orbit, pan and zoom. The *home* view frames the editor
  pixel-for-pixel like a normal 2D editor; from there you can fly around.
* An embedded Lua 5.4 interpreter (`nv3d` API) configures the 3D side, and Neovim
  can drive it too (`require('neovim3d')`, `:Nv3d`).

```
 ┌───────────────┐   keys / mouse (nvim_input)    ┌──────────────┐
 │   NeoVim3D    │ ─────────────────────────────▶ │ nvim --embed │
 │ GLFW + OpenGL │ ◀───────────────────────────── │              │
 │  Lua 5.4      │   redraw events (msgpack-RPC)  └──────────────┘
 └───────────────┘
```

## Building

Dependencies: a C++20 compiler, Meson, Ninja, pkg-config, GLFW 3.3+, libepoxy,
FreeType, Fontconfig, Lua 5.4, optionally libwebp (for `.webp` recordings), and
Neovim 0.10+ at runtime (any install works,
e.g. snap, AppImage or your distro's package; use `--nvim <path>` to pick one).

On Debian/Ubuntu:

```sh
sudo apt install meson ninja-build g++ pkg-config \
    libglfw3-dev libepoxy-dev libfreetype-dev libfontconfig-dev liblua5.4-dev \
    libwebp-dev
```

Then:

```sh
meson setup build
meson compile -C build
meson test -C build        # unit tests + an end-to-end test against a real nvim
./build/neovim3d somefile.cpp
```

`meson install -C build` installs the `neovim3d` binary.

## Usage

```
neovim3d [options] [--] [nvim arguments / files...]
```

All arguments not listed here are passed straight to `nvim`.

| Option | Meaning |
| --- | --- |
| `--nvim <path>` | Neovim executable to run (default `nvim`, or `$NEOVIM3D_NVIM`) |
| `--config <file>` | Frontend Lua config (default `~/.config/neovim3d/init.lua`) |
| `--clean-3d` | Don't load the frontend Lua config |
| `--screenshot-3d <file.png>` | Render once Neovim has drawn, save a PNG and exit |
| `--size-3d <WxH>` | Initial window size (default 1280x800) |
| `--help-3d`, `--version-3d` | Help / version |

### Camera controls

| Input | Action |
| --- | --- |
| Right-drag, or Alt+left-drag | Orbit |
| Middle-drag | Pan |
| Ctrl+wheel | Zoom |
| Ctrl+Shift+←/→/↑/↓ | Orbit in steps |
| Ctrl+Shift+PageUp / PageDown | Zoom in / out |
| Ctrl+Shift+Home | Reset to the home (2D-looking) view |
| Ctrl+Shift+End | Toggle "follow cursor" |
| Ctrl+Shift+C | Toggle cinema mode (see below) |
| Ctrl+Shift+N | Next cinema shot |

Left click, wheel scrolling and plain right/middle *clicks* go to Neovim, and the
mouse is ray-cast into 3D, so clicking works from any camera angle.

### Cinema mode

Press **Ctrl+Shift+C** while you work and the camera starts playing slow, gentle
preset shots, cross-fading from one to the next every 45 seconds. The framing
drifts lazily after your cursor so what you are editing stays in view. Press
Ctrl+Shift+C again to glide back to the normal view. If you grab the camera
yourself (mouse drag, Ctrl+wheel or the camera keys), cinema mode stops and
leaves the camera where you put it.

| Shot | Motion |
| --- | --- |
| `drift` | Barely-there breathing sway |
| `orbit` | Slow ellipse around the text |
| `dolly` | Gentle push in towards the cursor and back out |
| `crane` | Rises to look down onto the page, then settles |
| `truck` | Sideways tracking shot with parallax |
| `lowangle` | Looks up at the code from below, slowly swinging |
| `dutch` | A subtly tilted angle that rocks back and forth |

Tune it, or add your own shots, from Lua:

```lua
nv3d.cinema.config.duration = 60      -- seconds per shot
nv3d.cinema.config.intensity = 0.6    -- calmer angles
nv3d.cinema.config.speed = 0.7        -- slower motion

-- A custom shot: offsets from the home view as a function of time.
nv3d.cinema.add('sway', function(t, a)
  return { yaw = 8 * a * math.sin(t / 6), zoom = 0.95 }
end)

-- Start automatically, with only some shots, in random order.
nv3d.on('ready', function()
  nv3d.cinema.start { shots = { 'drift', 'orbit', 'sway' }, shuffle = true }
end)
```

| Function | Description |
| --- | --- |
| `nv3d.cinema.start(opts)` | Start; `opts` overrides `nv3d.cinema.config` for this run |
| `nv3d.cinema.stop(immediate)` | Glide back home (or stop on the spot with `true`) |
| `nv3d.cinema.toggle(opts)` / `.next()` | Toggle / cross-fade to the next shot now |
| `nv3d.cinema.running()` / `.current()` | Is it playing / name of the current shot |
| `nv3d.cinema.add(name, fn)` | Register a shot: `fn(t, intensity)` → `{yaw=, pitch=, roll=, zoom=, x=, y=}` (degrees, distance factor, world units) |

`nv3d.cinema.config` fields: `shots` (list, default all in `nv3d.cinema.order`),
`duration` (45), `transition` (8), `intensity` (1), `speed` (1), `follow`
(0.5: 0 frames the screen, 1 centres the cursor), `follow_rate` (0.8),
`shuffle` (false), `stop_on_input` (true), `announce` (false: print shot names).

From Neovim: `require('neovim3d').cinema.toggle()` or `:Nv3d nv3d.cinema.start()`.

## Configuration

Neovim itself is configured exactly as usual. `guifont` (e.g.
`vim.o.guifont = "JetBrains Mono:h13"`) and `linespace` are honoured.

The 3D frontend is configured in Lua, in `~/.config/neovim3d/init.lua`. See
[`examples/init.lua`](examples/init.lua). The `nv3d` API:

| Function | Description |
| --- | --- |
| `nv3d.set(name, value)` / `nv3d.set{...}` | Set options (below) |
| `nv3d.get(name)` | Read an option |
| `nv3d.map(keys, fn)` / `nv3d.unmap(keys)` | Frontend key maps, handled before Neovim sees the key |
| `nv3d.on('frame', fn(dt, time))` → id | Per-frame hook (for animation); `nv3d.off(id)` removes it |
| `nv3d.on('ready', fn)` | Run once Neovim has drawn its first screen |
| `nv3d.on('camera_input', fn)` | The user moved the camera with the mouse |
| `nv3d.camera.orbit(dyaw, dpitch)` | Rotate around the target (degrees) |
| `nv3d.camera.pan(dx, dy)` / `.zoom(f)` / `.dolly(d)` / `.roll(deg)` | Move the camera |
| `nv3d.camera.get()` / `.set{yaw=, pitch=, roll=, distance=, target={x=,y=,z=}}` | Camera state |
| `nv3d.camera.reset()` / `.snap()` | Home view / skip the smoothing animation |
| `nv3d.camera.home()` | The home pose (same shape as `get()`) |
| `nv3d.cursor()` → x, y, z | World position of the text cursor |
| `nv3d.command(cmd)` / `nv3d.input(keys)` / `nv3d.exec_lua(code)` | Talk to Neovim |
| `nv3d.time()` | Seconds since start |
| `nv3d.record.start{path=, fps=20, width=, quality=75, method=4, sharp_yuv=true, colors=256}` | Record to `.webp` (built-in; `quality` 0–100, `method` 0–6 effort, `sharp_yuv` keeps coloured text crisp) or `.gif`/`.mp4` (via `ffmpeg`, `colors` for GIF); `width` scales the output |
| `nv3d.record.stop()` / `.active()` | Finish the file / is a recording running |
| `nv3d.quit()` | Close NeoVim3D |

Options:

| Option | Default | Description |
| --- | --- | --- |
| `font` | `""` (monospace) | Fontconfig family |
| `font_size` | `16` | Em size in pixels at the home view |
| `linespace` | `0` | Extra pixels between lines |
| `depth` | `0.12` | Glyph extrusion depth (in line heights) |
| `layer_gap` | `0.6` | Z distance between stacked floating windows |
| `window_gap` | `0` | Z offset of split windows above the base grid |
| `fov` | `35` | Vertical field of view (degrees) |
| `smoothing` | `12` | Camera smoothing, `0` = instant |
| `ambient`, `specular` | `0.45`, `0.25` | Lighting |
| `light_dir` | `{-0.35, 0.55, 1}` | Light direction |
| `background_dim` | `0.55` | Brightness of the void around the text, relative to the background colour |
| `panels` | `true` | Draw a backing slab behind each window |
| `cursor_blink` | `true` | Honour `guicursor` blinking |
| `follow_cursor` | `false` | Keep the camera centred on the cursor |
| `follow_offset` | `{0, 0}` | Aim this far from the cursor when following (line units, `+y` = above) |
| `msaa` | `4` | Multisampling (read at startup) |

### Recording demos

`nv3d.record.start{ path = 'out.webp' }` records what is rendered. Time advances
in fixed steps of `1/fps` while recording, so the result plays back perfectly
smoothly however long each frame took to render, and scripted keys sent with
`nv3d.input` are waited on so every frame shows Neovim's response.

* **`.webp`** (recommended for READMEs): an animated WebP, encoded in-process
  with libwebp. True colour, about half the size of a GIF at twice the frame
  rate, and it autoplays and loops like a GIF wherever images are shown.
* **`.gif`**: via `ffmpeg`, with a two-pass palette tuned to what changes.
* **`.mp4`** and other video formats: via `ffmpeg`.

The animation at the top of this README is produced by a script:

```sh
examples/demo/record.sh            # writes docs/demo.webp (~20 MB, 30 fps, 26 s)
examples/demo/record.sh demo.mp4   # or a GIF / an .mp4
```

[`examples/demo/demo.lua`](examples/demo/demo.lua) types
[`hello.cpp`](examples/demo/hello.cpp) with natural pacing while the camera
follows the cursor, opens the LSP hover window on `std::vector` (a floating
window, so it hovers on its own layer while the camera swings round), saves
with `:w hello-world.cpp` on the command line, and pulls back to show the
whole program. The script checks that the saved file matches the source.

Neovim runs with **your own config**, so your colour scheme, completion menu
and LSP server (clangd for C++) are what the recording shows. The hover is
opened with `<Space>i`; set `NV3D_DEMO_HOVER_KEYS` to your own mapping. For a
recording that doesn't depend on personal config, use `NV3D_DEMO_CLEAN=1`
(Neovim then runs with `--clean` plus [`nvim.lua`](examples/demo/nvim.lua),
which sets up clangd from your `PATH` or Mason).

A window opens while it records; it can be covered, but leave it alone until
it closes. Tune the output with `NV3D_DEMO_QUALITY` (default 90),
`NV3D_DEMO_FPS` (30), `NV3D_DEMO_WIDTH` (960) and `NV3D_DEMO_COLORS` (GIF).
Larger files take longer to load for README visitors: quality 85 or 24 fps
each bring the demo to ~16 MB.

### Driving the frontend from Neovim

Inside Neovim, `vim.g.neovim3d` is set and a `neovim3d` module is available, so
your regular Neovim config can control the 3D view:

```lua
if vim.g.neovim3d then
  local n3d = require('neovim3d')
  n3d.set('depth', 0.3)
  vim.keymap.set('n', '<leader>3', function() n3d.camera.orbit(-30, 20) end)
  vim.keymap.set('n', '<leader>0', function() n3d.camera.reset() end)
end
```

…or interactively: `:Nv3d nv3d.camera.orbit(45, 0)`.

## Project layout

| Path | Contents |
| --- | --- |
| `src/msgpack.*` | MessagePack encoder / streaming decoder |
| `src/nvim.*` | Spawning `nvim --embed`, msgpack-RPC requests/notifications |
| `src/ui_state.*` | Mirror of Neovim's grids, highlights, cursor and modes from `redraw` events |
| `src/font.*` | Fontconfig/FreeType font loading, glyph outline → extruded mesh |
| `src/scene.*` | Lays grids out in 3D and produces glyph/box instances |
| `src/renderer.*` | OpenGL 3.3 instanced renderer |
| `src/camera.*` | Smoothed orbit camera, picking rays |
| `src/input.*` | GLFW key events → Neovim key notation |
| `src/lua_api.*` | Embedded Lua 5.4 and the `nv3d` API |
| `src/cinema.cpp` | Cinema mode (`nv3d.cinema`), written in embedded Lua |
| `src/app.*` | Window, event loop and glue |
| `src/recorder.*` | Recording: animated WebP via libwebp, GIF/video via `ffmpeg` |
| `third_party/earcut` | [earcut.hpp](https://github.com/mapbox/earcut.hpp) polygon triangulation (ISC) |
| `examples/demo/` | The scripted README demo and its recording script |
| `tests/` | Unit tests and an end-to-end test against a real Neovim |
