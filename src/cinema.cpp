// The `nv3d.cinema` module: slow, soothing preset camera shots that play while
// you work, cross-fading from one shot to the next. Written in Lua on top of
// the public nv3d API so users can read, tweak and extend it.
#include "lua_api.hpp"

const char* kLuaCinema = R"lua(
local cinema = {}
nv3d.cinema = cinema

local TAU = 2 * math.pi
local sin, cos, exp = math.sin, math.cos, math.exp

-- A preset maps time (seconds, already scaled by `speed`) and an intensity
-- factor to an offset from the home view:
--   yaw/pitch/roll in degrees, zoom as a distance multiplier (< 1 = closer),
--   x/y as a target offset in world units (1 unit = one text line).
-- Periods are long and mutually prime-ish so the motion never feels looped.
cinema.presets = {
  -- Barely-there breathing sway.
  drift = function(t, a)
    return {
      yaw = 9 * a * sin(TAU * t / 23),
      pitch = 5 * a * sin(TAU * t / 31 + 1),
      roll = 0.8 * a * sin(TAU * t / 37),
      zoom = 1 - 0.03 * a * (1 - cos(TAU * t / 29)),
    }
  end,
  -- The camera traces a slow ellipse around the text.
  orbit = function(t, a)
    local w = TAU * t / 48
    return { yaw = 16 * a * sin(w), pitch = 7 * a * cos(w), zoom = 0.96 }
  end,
  -- Gentle push in towards the cursor, then ease back out.
  dolly = function(t, a)
    local p = 0.5 - 0.5 * cos(TAU * t / 34)
    return { zoom = 1 - 0.22 * a * p, yaw = 5 * a * sin(TAU * t / 51), pitch = 3 * a * p }
  end,
  -- Rise up and look down onto the page, then settle.
  crane = function(t, a)
    local p = 0.5 - 0.5 * cos(TAU * t / 40)
    return { pitch = 16 * a * p, yaw = -6 * a * sin(TAU * t / 57), zoom = 1 - 0.06 * a * p }
  end,
  -- Sideways tracking shot with parallax.
  truck = function(t, a)
    local s = sin(TAU * t / 44)
    return { yaw = -12 * a * s, x = 3 * a * s, zoom = 0.97 }
  end,
  -- Looking up at the code from below, slowly swinging.
  lowangle = function(t, a)
    return {
      pitch = -9 * a + 2.5 * a * sin(TAU * t / 27),
      yaw = -10 * a + 4 * a * sin(TAU * t / 41),
      zoom = 0.92,
    }
  end,
  -- A subtle tilted ("dutch") angle that rocks back and forth.
  dutch = function(t, a)
    return {
      roll = 3 * a * sin(TAU * t / 33),
      yaw = 6 * a * sin(TAU * t / 47),
      pitch = 4 * a * sin(TAU * t / 39),
    }
  end,
}

-- The order shots play in. Presets added with cinema.add() are appended.
cinema.order = { 'drift', 'orbit', 'dolly', 'crane', 'truck', 'lowangle', 'dutch' }

cinema.config = {
  shots = nil,          -- list of preset names; nil = cinema.order
  duration = 45,        -- seconds per shot
  transition = 8,       -- seconds of cross-fade between shots
  intensity = 1,        -- scales every angle/offset (0.5 = calmer)
  speed = 1,            -- scales time (0.5 = half as fast)
  follow = 0.5,         -- 0 = frame the screen, 1 = centre on the cursor
  follow_rate = 0.8,    -- how quickly the framing drifts after the cursor
  shuffle = false,      -- random order instead of cinema.order
  stop_on_input = true, -- stop when you move the camera with the mouse
  announce = false,     -- print the name of each new shot
}

local S = nil -- state while running
local hook = nil

local function smootherstep(u)
  if u <= 0 then return 0 elseif u >= 1 then return 1 end
  return u * u * u * (u * (u * 6 - 15) + 10)
end

local function lerp(a, b, u) return a + (b - a) * u end

local function lerp_pose(p, q, u)
  return {
    yaw = lerp(p.yaw, q.yaw, u), pitch = lerp(p.pitch, q.pitch, u),
    roll = lerp(p.roll, q.roll, u), distance = lerp(p.distance, q.distance, u),
    x = lerp(p.x, q.x, u), y = lerp(p.y, q.y, u), z = lerp(p.z, q.z, u),
  }
end

local function pose_of(cam)
  return {
    yaw = cam.yaw, pitch = cam.pitch, roll = cam.roll, distance = cam.distance,
    x = cam.target.x, y = cam.target.y, z = cam.target.z,
  }
end

-- A shot that holds a fixed pose (the camera at start, or mid-blend).
local function frozen(pose)
  return { name = '(hold)', t = 0, eval = function() return pose end }
end

local function preset_shot(name)
  local fn = cinema.presets[name]
  local shot = { name = name, t = 0 }
  shot.eval = function(ctx)
    local r = fn(shot.t, ctx.opts.intensity) or {}
    local h = ctx.home
    return {
      yaw = h.yaw + (r.yaw or 0), pitch = h.pitch + (r.pitch or 0),
      roll = h.roll + (r.roll or 0), distance = h.distance * (r.zoom or 1),
      x = ctx.base.x + (r.x or 0), y = ctx.base.y + (r.y or 0), z = ctx.base.z,
    }
  end
  return shot
end

local function home_shot()
  return { name = '(home)', t = 0, eval = function(ctx) return pose_of(ctx.home) end }
end

local function begin_transition(to)
  if S.from then
    -- Interrupting a cross-fade: continue from wherever the camera is now.
    S.from = frozen(S.last)
  else
    S.from = S.cur
  end
  S.cur = to
  S.blend = 0
  S.timer = 0
end

local function pick_next()
  local list = S.list
  if #list == 1 then return list[1] end
  if S.opts.shuffle then
    local name
    repeat name = list[math.random(#list)] until name ~= S.cur.name
    return name
  end
  S.index = S.index % #list + 1
  return list[S.index]
end

local function finish()
  if hook then nv3d.off(hook) end
  hook, S = nil, nil
end

local function frame(dt)
  local home = nv3d.camera.home()
  local ctx = { home = home, opts = S.opts, base = S.base }

  -- Let the framing drift lazily towards the cursor.
  local cx, cy = nv3d.cursor()
  local f = S.opts.follow
  local want_x = lerp(home.target.x, cx, f)
  local want_y = lerp(home.target.y, cy, f)
  local k = 1 - exp(-S.opts.follow_rate * dt)
  S.base.x = lerp(S.base.x, want_x, k)
  S.base.y = lerp(S.base.y, want_y, k)
  S.base.z = home.target.z

  local step = dt * S.opts.speed
  S.cur.t = S.cur.t + step
  S.timer = S.timer + dt
  if not S.stopping and S.timer >= S.opts.duration then
    local name = pick_next()
    begin_transition(preset_shot(name))
    if S.opts.announce then print('cinema: ' .. name) end
  end

  local pose = S.cur.eval(ctx)
  if S.from then
    S.from.t = S.from.t + step
    S.blend = S.blend + dt
    local u = smootherstep(S.blend / math.max(0.01, S.opts.transition))
    pose = lerp_pose(S.from.eval(ctx), pose, u)
    if u >= 1 then S.from = nil end
  end
  S.last = pose

  nv3d.camera.set {
    yaw = pose.yaw, pitch = pose.pitch, roll = pose.roll, distance = pose.distance,
    target = { x = pose.x, y = pose.y, z = pose.z },
  }

  if S.stopping and not S.from then
    finish()
    nv3d.camera.reset()
  end
end

--- Start playing shots. `opts` overrides fields of cinema.config.
function cinema.start(opts)
  local o = {}
  for key, v in pairs(cinema.config) do o[key] = v end
  for key, v in pairs(opts or {}) do o[key] = v end
  local list = o.shots or cinema.order
  if type(list) == 'string' then list = { list } end
  for _, name in ipairs(list) do
    if not cinema.presets[name] then error('cinema: unknown shot "' .. tostring(name) .. '"', 2) end
  end
  if #list == 0 then error('cinema: no shots to play', 2) end

  local start_pose = S and S.last or pose_of(nv3d.camera.get())
  local home = nv3d.camera.home()
  local base = S and S.base or { x = home.target.x, y = home.target.y, z = home.target.z }
  local index = o.shuffle and math.random(#list) or 1
  S = {
    opts = o, list = list, index = index, base = base, timer = 0, blend = 0,
    cur = frozen(start_pose), last = start_pose,
  }
  begin_transition(preset_shot(list[index])) -- cross-fades from start_pose
  if o.announce then print('cinema: ' .. list[index]) end
  if not hook then hook = nv3d.on('frame', function(dt) if S then frame(dt) end end) end
end

--- Stop. By default the camera glides back to the home view over one
--- transition; with `immediate` it stays exactly where it is.
function cinema.stop(immediate)
  if not S then return end
  if immediate then
    finish()
    return
  end
  if S.stopping then return end
  S.stopping = true
  begin_transition(home_shot())
end

function cinema.toggle(opts)
  if S and not S.stopping then cinema.stop() else cinema.start(opts) end
end

--- Cut (well, cross-fade) to the next shot now.
function cinema.next()
  if not S or S.stopping then return end
  local name = pick_next()
  begin_transition(preset_shot(name))
  if S.opts.announce then print('cinema: ' .. name) end
end

function cinema.running() return S ~= nil and not S.stopping end

--- Name of the shot currently playing (nil when stopped).
function cinema.current() return S and not S.stopping and S.cur.name or nil end

--- Register a custom shot: fn(t, intensity) -> { yaw=, pitch=, roll=, zoom=, x=, y= }.
function cinema.add(name, fn)
  assert(type(name) == 'string' and type(fn) == 'function', 'cinema.add(name, fn)')
  if not cinema.presets[name] then table.insert(cinema.order, name) end
  cinema.presets[name] = fn
end

-- Grabbing the camera yourself ends the show, leaving the camera where it is.
nv3d.on('camera_input', function()
  if S and S.opts.stop_on_input then cinema.stop(true) end
end)
)lua";
