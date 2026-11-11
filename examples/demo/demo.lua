-- NeoVim3D demo: types a small C++ program with the camera following the
-- cursor, and records it. Run it through examples/demo/record.sh.
--
-- Environment:
--   NV3D_DEMO_SOURCE  file whose contents get typed (default: hello.cpp next to this script)
--   NV3D_DEMO_OUT     output file: .webp, .gif or .mp4 (default: demo.webp)
--   NV3D_DEMO_WIDTH   output width in pixels (default: 880, GitHub's README width)
--   NV3D_DEMO_FPS     frames per second (default: 24)
--   NV3D_DEMO_QUALITY WebP quality 0-100 (default: 82; lower = smaller file)
-- The defaults keep the demo under GitHub's 10 MB limit for images.
--   NV3D_DEMO_METHOD  WebP effort 0-6 (default: 4; 6 is ~7x slower for ~2% smaller)
--   NV3D_DEMO_COLORS  GIF palette size (default: 128; fewer = smaller file)
--   NV3D_DEMO_HOVER_KEYS  keys that open the LSP hover window (default: <Space>i)
--   NV3D_DEMO_FONT    font family (default: MesloLGS NF; falls back to monospace)
--   NV3D_DEMO_DEPTH   character depth in line heights (default: 0.45)

local script_dir = debug.getinfo(1, 'S').source:match('^@(.*/)') or './'
local SOURCE = os.getenv('NV3D_DEMO_SOURCE') or (script_dir .. 'hello.cpp')
local OUT = os.getenv('NV3D_DEMO_OUT') or 'demo.webp'
local WIDTH = tonumber(os.getenv('NV3D_DEMO_WIDTH') or '880')
local FPS = tonumber(os.getenv('NV3D_DEMO_FPS') or '24')
local QUALITY = tonumber(os.getenv('NV3D_DEMO_QUALITY') or '82')
local METHOD = tonumber(os.getenv('NV3D_DEMO_METHOD') or '4')
local HOVER_KEYS = os.getenv('NV3D_DEMO_HOVER_KEYS') or '<Space>i'
local COLORS = tonumber(os.getenv('NV3D_DEMO_COLORS') or '128')

nv3d.set {
  -- A Nerd Font, for the icons used by the completion menu, statusline etc.
  font = os.getenv('NV3D_DEMO_FONT') or 'MesloLGS NF',
  font_size = 16,  -- 12 pt (16 px at 96 DPI), where MesloLGS looks its best
  -- How far characters are extruded, in line heights (NeoVim3D's default is
  -- 0.12); deeper glyphs catch more light on their sides and "pop" more.
  depth = tonumber(os.getenv('NV3D_DEMO_DEPTH') or '0.45'),
  smoothing = 2,   -- slow, gliding camera
  cursor_blink = false,
  -- Aim a little above the cursor, so the lines already written
  -- stay in the shot.
  follow_offset = { 0, 2 },
  -- Lift floating windows (the LSP hover) well clear of the text.
  layer_gap = 1.2,
}

local function read_file(path)
  local f = assert(io.open(path, 'r'), 'demo: cannot read ' .. path)
  local text = f:read('a')
  f:close()
  return text
end

-- ---------------------------------------------------------------------------
-- A tiny coroutine sequencer driven by frame time (fixed steps while recording).

local wait_left = 0
local function wait(seconds)
  wait_left = wait_left + seconds
  coroutine.yield()
end

local function keys(k) nv3d.input(k) end

-- Natural-looking typing: deterministic jitter, longer pauses at line ends.
-- Each character is sent as its own input, so it also works on the command line.
math.randomseed(3)
local function type_text(text)
  for ch in text:gmatch(utf8.charpattern) do
    keys(ch == '<' and '<lt>' or ch)
    local d = 0.035 + math.random() * 0.04
    if ch == ';' or ch == '{' or ch == '}' then d = d + 0.12 end
    if ch == ' ' and math.random() < 0.15 then d = d + 0.08 end
    wait(d)
  end
end

-- The area the typed program covers, to frame it at the end. Sampled right
-- after each line is typed, when the cursor is certainly at the end of that
-- line (sampling every frame also catches moments where Neovim parks the
-- cursor on the command line, e.g. while running a command).
local bounds
local function sample_cursor()
  local x, y, z = nv3d.cursor()
  if not bounds then
    bounds = { x0 = x, x1 = x, y0 = y, y1 = y, z = z }
  else
    bounds.x0, bounds.x1 = math.min(bounds.x0, x), math.max(bounds.x1, x)
    bounds.y0, bounds.y1 = math.min(bounds.y0, y), math.max(bounds.y1, y)
  end
end

-- Type a whole program in insert mode, line by line. Neovim configs
-- auto-indent and continue comments on <CR>; clear whatever was inserted for
-- us and type the real indentation, so the result is exact whatever the
-- config. The clearing goes in the same input as the <CR>, so it never shows.
-- (Not <C-u>: at the start of a line it would delete the line break.)
local NEWLINE = '<CR><Cmd>call setline(".", "") | call cursor(line("."), 1)<CR>'
local function type_program(source)
  local first = true
  for line in (source:gsub('\n$', '') .. '\n'):gmatch('(.-)\n') do
    if not first then
      keys(NEWLINE)
      wait(0.25 + math.random() * 0.12)
    end
    first = false
    sample_cursor() -- start of the line
    type_text(line)
    sample_cursor() -- end of the line
  end
end

local function scenario()
  local home = nv3d.camera.home()

  -- Start on the flat, normal-looking editor...
  wait(0.6)

  -- ...then glide into a close, angled shot that follows the cursor.
  nv3d.camera.set { yaw = -22, pitch = 14, distance = home.distance * 0.52 }
  nv3d.set('follow_cursor', true)
  wait(1.2)

  keys('i')
  wait(0.2)
  type_program(read_file(SOURCE))
  wait(0.3)
  keys('<Esc>')
  wait(0.5)

  -- Ask clangd about std::cout (line 4): its hover window is a floating
  -- window, so it gets its own grid, lifted towards the camera.
  keys('4G')
  wait(0.35)
  keys('fc')
  wait(0.5)
  keys(HOVER_KEYS)
  wait(0.5)
  -- Swing round to the side so the gap between the window and the text
  -- shows, aiming at the middle of the (wide) hover window.
  -- A quick glide, then a steady hold: still frames are cheap to encode,
  -- a moving panel full of fine text is not.
  nv3d.set('smoothing', 5)
  nv3d.set('follow_offset', { 9, -4 })
  nv3d.camera.set { yaw = 26, pitch = 16, distance = home.distance * 0.55 }
  wait(2.8)
  nv3d.set('smoothing', 2)
  keys('G') -- moving the cursor closes the hover window
  wait(0.5)

  -- Save it, typing the command so the command line is in the shot: once
  -- ':' puts the cursor on the command line, frame the whole command (and
  -- the "written" message that replaces it) and hold the camera there.
  local command = 'w hello-world.cpp'
  keys(':')
  wait(0.1)
  local cx, cy, cz = nv3d.cursor()
  nv3d.set('follow_cursor', false)
  nv3d.camera.set {
    yaw = -14, pitch = 12, distance = home.distance * 0.52,
    -- Centred on the "written" message that follows (~40 characters).
    target = { x = cx + 8, y = cy + 1.5, z = cz },
  }
  wait(0.9)
  type_text(command)
  wait(0.45)
  keys('<CR>')
  wait(1.6)

  -- Pull back slowly to show the whole program from a new angle.
  nv3d.set('follow_cursor', false)
  nv3d.set('smoothing', 1.2)
  nv3d.camera.set {
    yaw = 16, pitch = -10, distance = home.distance * 0.45,
    target = { x = (bounds.x0 + bounds.x1) / 2, y = (bounds.y0 + bounds.y1) / 2, z = bounds.z },
  }
  wait(3)

  nv3d.record.stop()
  nv3d.quit()
end

local co = coroutine.create(scenario)

nv3d.on('ready', function()
  nv3d.record.start {
    path = OUT, fps = FPS, width = WIDTH, quality = QUALITY, method = METHOD, colors = COLORS,
  }
  nv3d.on('frame', function(dt)
    if coroutine.status(co) == 'dead' then return end
    wait_left = wait_left - dt
    while wait_left <= 0 and coroutine.status(co) ~= 'dead' do
      local ok, err = coroutine.resume(co)
      if not ok then
        print('demo failed: ' .. tostring(err))
        nv3d.record.stop()
        nv3d.quit()
      end
    end
  end)
end)
