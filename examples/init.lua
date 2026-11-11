-- Example NeoVim3D frontend config. Copy to ~/.config/neovim3d/init.lua
-- (or pass it with --config). This file configures the 3D frontend only;
-- your normal Neovim config (~/.config/nvim/init.lua) is used as usual.

nv3d.set {
  font = 'MesloLGS NF Regular', -- any fontconfig family; falls back to "monospace"
  font_size = 16,            -- em size in pixels at the home camera position
  depth = 0.15,              -- how far glyphs are extruded (in line heights)
  layer_gap = 0.8,           -- distance between stacked floating windows
  fov = 35,                  -- vertical field of view in degrees
  smoothing = 12,            -- camera smoothing; 0 = instant
  light_dir = { -0.35, 0.55, 1.0 },
}

local cam = nv3d.camera

-- Extra frontend key maps. These are handled before keys reach Neovim.
nv3d.map('<C-S-r>', function() cam.reset() end)
nv3d.map('<C-S-t>', function()
  -- A dramatic "tilted" view of the current file.
  cam.reset()
  cam.orbit(-30, 25)
  cam.zoom(0.8)
end)

-- Slowly spin around the text while <C-S-s> is toggled on.
local spin
nv3d.map('<C-S-s>', function()
  if spin then
    nv3d.off(spin)
    spin = nil
  else
    spin = nv3d.on('frame', function(dt) cam.orbit(20 * dt, 0) end)
  end
end)

-- Cinema mode (Ctrl+Shift+C): slow preset camera shots while you work.
nv3d.cinema.config.duration = 60   -- seconds per shot
nv3d.cinema.config.intensity = 0.8 -- a little calmer than the default

-- Add a shot of your own to the rotation.
nv3d.cinema.add('sway', function(t, a)
  return { yaw = 8 * a * math.sin(t / 6), pitch = 2 * a * math.sin(t / 9), zoom = 0.95 }
end)

-- Runs once Neovim has drawn its first screen.
nv3d.on('ready', function()
  print('NeoVim3D ready')
  -- Uncomment to start every session in cinema mode:
  -- nv3d.cinema.start { shuffle = true }
end)
