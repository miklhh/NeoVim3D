#include "lua_api.hpp"

#include <lua.hpp>

#include <chrono>
#include <cmath>
#include <functional>

#include "input.hpp"

namespace {

constexpr float kDeg = 3.14159265358979f / 180.0f;

LuaEngine& engine(lua_State* L) {
    return *static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
}

// ---- msgpack <-> Lua ------------------------------------------------------

void push_value(lua_State* L, const mp::Value& v) {
    if (v.is_bool()) {
        lua_pushboolean(L, v.as_bool());
    } else if (v.is_int()) {
        lua_pushinteger(L, lua_Integer(v.as_int()));
    } else if (v.is_double()) {
        lua_pushnumber(L, v.as_double());
    } else if (v.is_str()) {
        lua_pushlstring(L, v.as_str().data(), v.as_str().size());
    } else if (v.is_array()) {
        lua_createtable(L, int(v.size()), 0);
        int i = 1;
        for (auto& e : v.as_array()) {
            push_value(L, e);
            lua_rawseti(L, -2, i++);
        }
    } else if (v.is_map()) {
        lua_createtable(L, 0, int(v.size()));
        for (auto& [k, e] : v.as_map()) {
            push_value(L, k);
            push_value(L, e);
            lua_rawset(L, -3);
        }
    } else {
        lua_pushnil(L);
    }
}

// ---- options ---------------------------------------------------------------

struct OptionDesc {
    const char* name;
    std::function<void(lua_State*, Options&)> get;  // pushes the value
    std::function<bool(lua_State*, int, Options&)> set;
};

template <typename T>
OptionDesc num(const char* name, T Options::*field) {
    return {name, [field](lua_State* L, Options& o) { lua_pushnumber(L, double(o.*field)); },
            [field](lua_State* L, int idx, Options& o) {
                if (!lua_isnumber(L, idx)) return false;
                o.*field = T(lua_tonumber(L, idx));
                return true;
            }};
}

OptionDesc boolean(const char* name, bool Options::*field) {
    return {name, [field](lua_State* L, Options& o) { lua_pushboolean(L, o.*field); },
            [field](lua_State* L, int idx, Options& o) {
                o.*field = lua_toboolean(L, idx);
                return true;
            }};
}

// A vector option, written as {x, y[, z]} (missing components are 0).
OptionDesc vec3(const char* name, Vec3 Options::*field) {
    return {name,
            [field](lua_State* L, Options& o) {
                const Vec3& v = o.*field;
                lua_createtable(L, 3, 0);
                lua_pushnumber(L, v.x), lua_rawseti(L, -2, 1);
                lua_pushnumber(L, v.y), lua_rawseti(L, -2, 2);
                lua_pushnumber(L, v.z), lua_rawseti(L, -2, 3);
            },
            [field](lua_State* L, int idx, Options& o) {
                if (!lua_istable(L, idx)) return false;
                float v[3] = {0, 0, 0};
                for (int i = 0; i < 3; ++i) {
                    lua_rawgeti(L, idx, i + 1);
                    v[i] = float(lua_tonumber(L, -1));
                    lua_pop(L, 1);
                }
                o.*field = Vec3(v[0], v[1], v[2]);
                return true;
            }};
}

const std::vector<OptionDesc>& option_table() {
    static const std::vector<OptionDesc> t = {
        {"font", [](lua_State* L, Options& o) { lua_pushstring(L, o.font.c_str()); },
         [](lua_State* L, int idx, Options& o) {
             if (!lua_isstring(L, idx)) return false;
             o.font = lua_tostring(L, idx);
             return true;
         }},
        num("font_size", &Options::font_size),
        num("linespace", &Options::linespace),
        num("depth", &Options::depth),
        num("layer_gap", &Options::layer_gap),
        num("window_gap", &Options::window_gap),
        num("fov", &Options::fov),
        num("smoothing", &Options::smoothing),
        num("ambient", &Options::ambient),
        num("specular", &Options::specular),
        num("background_dim", &Options::background_dim),
        num("msaa", &Options::msaa),
        boolean("panels", &Options::panels),
        boolean("cursor_blink", &Options::cursor_blink),
        boolean("follow_cursor", &Options::follow_cursor),
        vec3("light_dir", &Options::light_dir),
        vec3("follow_offset", &Options::follow_offset),
    };
    return t;
}

const OptionDesc* find_option(const char* name) {
    for (auto& d : option_table())
        if (std::string(d.name) == name) return &d;
    return nullptr;
}

void set_one(lua_State* L, const char* name, int idx) {
    const OptionDesc* d = find_option(name);
    if (!d) luaL_error(L, "nv3d: unknown option '%s'", name);
    if (!d->set(L, idx, engine(L).host.options()))
        luaL_error(L, "nv3d: invalid value for option '%s'", name);
}

int l_set(lua_State* L) {
    if (lua_istable(L, 1)) {
        lua_pushnil(L);
        while (lua_next(L, 1)) {
            if (lua_type(L, -2) == LUA_TSTRING) set_one(L, lua_tostring(L, -2), lua_gettop(L));
            lua_pop(L, 1);
        }
    } else {
        set_one(L, luaL_checkstring(L, 1), 2);
    }
    engine(L).host.options_changed();
    return 0;
}

int l_get(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const OptionDesc* d = find_option(name);
    if (!d) return luaL_error(L, "nv3d: unknown option '%s'", name);
    d->get(L, engine(L).host.options());
    return 1;
}

// ---- camera ----------------------------------------------------------------

float field(lua_State* L, int idx, const char* name, float def) {
    lua_getfield(L, idx, name);
    float v = lua_isnumber(L, -1) ? float(lua_tonumber(L, -1)) : def;
    lua_pop(L, 1);
    return v;
}

void push_camera_state(lua_State* L, const CameraState& c) {
    lua_createtable(L, 0, 5);
    lua_pushnumber(L, c.yaw / kDeg), lua_setfield(L, -2, "yaw");
    lua_pushnumber(L, c.pitch / kDeg), lua_setfield(L, -2, "pitch");
    lua_pushnumber(L, c.roll / kDeg), lua_setfield(L, -2, "roll");
    lua_pushnumber(L, c.distance), lua_setfield(L, -2, "distance");
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, c.target.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, c.target.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, c.target.z), lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "target");
}

int l_cam_get(lua_State* L) {
    push_camera_state(L, engine(L).host.camera().goal);
    return 1;
}

int l_cam_home(lua_State* L) {
    push_camera_state(L, engine(L).host.camera_home());
    return 1;
}

int l_cam_set(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    CameraState& c = engine(L).host.camera().goal;
    c.yaw = field(L, 1, "yaw", c.yaw / kDeg) * kDeg;
    c.pitch = field(L, 1, "pitch", c.pitch / kDeg) * kDeg;
    c.roll = field(L, 1, "roll", c.roll / kDeg) * kDeg;
    c.distance = field(L, 1, "distance", c.distance);
    lua_getfield(L, 1, "target");
    if (lua_istable(L, -1)) {
        int t = lua_gettop(L);
        c.target = Vec3(field(L, t, "x", c.target.x), field(L, t, "y", c.target.y),
                        field(L, t, "z", c.target.z));
    }
    lua_pop(L, 1);
    engine(L).host.request_redraw();
    return 0;
}

int l_cam_orbit(lua_State* L) {
    engine(L).host.camera().orbit(float(luaL_checknumber(L, 1)) * kDeg,
                                  float(luaL_optnumber(L, 2, 0)) * kDeg);
    engine(L).host.request_redraw();
    return 0;
}

int l_cam_roll(lua_State* L) {
    engine(L).host.camera().goal.roll += float(luaL_checknumber(L, 1)) * kDeg;
    engine(L).host.request_redraw();
    return 0;
}

int l_cam_pan(lua_State* L) {
    engine(L).host.camera().pan(float(luaL_checknumber(L, 1)), float(luaL_optnumber(L, 2, 0)));
    engine(L).host.request_redraw();
    return 0;
}

int l_cam_zoom(lua_State* L) {
    engine(L).host.camera().zoom(float(luaL_checknumber(L, 1)));
    engine(L).host.request_redraw();
    return 0;
}

int l_cam_dolly(lua_State* L) {
    engine(L).host.camera().dolly(float(luaL_checknumber(L, 1)));
    engine(L).host.request_redraw();
    return 0;
}

int l_cam_reset(lua_State* L) {
    engine(L).host.camera_reset();
    return 0;
}

int l_cam_snap(lua_State* L) {
    engine(L).host.camera().snap();
    engine(L).host.request_redraw();
    return 0;
}

// ---- misc --------------------------------------------------------------------

int l_cursor(lua_State* L) {
    Vec3 p = engine(L).host.cursor_world();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    lua_pushnumber(L, p.z);
    return 3;
}

int l_map(lua_State* L) {
    std::string keys = normalize_keys(luaL_checkstring(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    auto& maps = engine(L).keymaps;
    if (auto it = maps.find(keys); it != maps.end()) luaL_unref(L, LUA_REGISTRYINDEX, it->second);
    lua_pushvalue(L, 2);
    maps[keys] = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

int l_unmap(lua_State* L) {
    std::string keys = normalize_keys(luaL_checkstring(L, 1));
    auto& maps = engine(L).keymaps;
    if (auto it = maps.find(keys); it != maps.end()) {
        luaL_unref(L, LUA_REGISTRYINDEX, it->second);
        maps.erase(it);
    }
    return 0;
}

int l_on(lua_State* L) {
    std::string ev = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    LuaEngine& e = engine(L);
    std::vector<int>* list = ev == "frame"          ? &e.frame_hooks_
                             : ev == "ready"        ? &e.ready_hooks_
                             : ev == "camera_input" ? &e.camera_input_hooks_
                                                    : nullptr;
    if (!list) return luaL_error(L, "nv3d.on: unknown event '%s'", ev.c_str());
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    list->push_back(ref);
    lua_pushinteger(L, ref);
    return 1;
}

int l_off(lua_State* L) {
    lua_Integer ref = luaL_checkinteger(L, 1);
    LuaEngine& e = engine(L);
    for (auto* hooks : {&e.frame_hooks_, &e.ready_hooks_, &e.camera_input_hooks_})
        for (auto it = hooks->begin(); it != hooks->end(); ++it)
            if (*it == ref) {
                luaL_unref(L, LUA_REGISTRYINDEX, *it);
                hooks->erase(it);
                return 0;
            }
    return 0;
}

int l_command(lua_State* L) {
    engine(L).host.nvim_command(luaL_checkstring(L, 1));
    return 0;
}

int l_input(lua_State* L) {
    engine(L).host.nvim_input(luaL_checkstring(L, 1));
    return 0;
}

int l_exec_lua(lua_State* L) {
    engine(L).host.nvim_exec_lua(luaL_checkstring(L, 1));
    return 0;
}

int l_time(lua_State* L) {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    lua_pushnumber(L, duration<double>(steady_clock::now() - start).count());
    return 1;
}

int l_record_start(lua_State* L) {
    RecordOptions o;
    if (lua_isstring(L, 1)) {
        o.path = lua_tostring(L, 1);
    } else {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_getfield(L, 1, "path");
        o.path = luaL_optstring(L, -1, "");
        lua_pop(L, 1);
        o.fps = int(field(L, 1, "fps", float(o.fps)));
        o.width = int(field(L, 1, "width", 0));
        o.colors = int(field(L, 1, "colors", float(o.colors)));
        o.quality = int(field(L, 1, "quality", float(o.quality)));
        o.method = int(field(L, 1, "method", float(o.method)));
        lua_getfield(L, 1, "sharp_yuv");
        if (!lua_isnil(L, -1)) o.sharp_yuv = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "dither");
        o.dither = luaL_optstring(L, -1, o.dither.c_str());
        lua_pop(L, 1);
    }
    if (o.path.empty()) return luaL_error(L, "nv3d.record.start: missing path");
    std::string err;
    if (!engine(L).host.record_start(o, err)) return luaL_error(L, "nv3d.record.start: %s", err.c_str());
    return 0;
}

int l_record_stop(lua_State* L) {
    std::string err;
    if (!engine(L).host.record_stop(err)) return luaL_error(L, "nv3d.record.stop: %s", err.c_str());
    return 0;
}

int l_record_active(lua_State* L) {
    lua_pushboolean(L, engine(L).host.recording());
    return 1;
}

int l_quit(lua_State* L) {
    engine(L).host.quit();
    return 0;
}

int l_print(lua_State* L) {
    std::string out;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += "\t";
        out += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    engine(L).host.message(out, false);
    return 0;
}

void register_fns(lua_State* L, LuaEngine* self, const luaL_Reg* fns) {
    for (; fns->name; ++fns) {
        lua_pushlightuserdata(L, self);
        lua_pushcclosure(L, fns->func, 1);
        lua_setfield(L, -2, fns->name);
    }
}

}  // namespace

LuaEngine::LuaEngine(LuaHost& h) : host(h), L_(luaL_newstate()) {
    luaL_openlibs(L_);

    lua_newtable(L_);
    static const luaL_Reg api[] = {
        {"set", l_set},         {"get", l_get},       {"cursor", l_cursor},
        {"map", l_map},         {"unmap", l_unmap},   {"on", l_on},
        {"off", l_off},         {"command", l_command}, {"input", l_input},
        {"exec_lua", l_exec_lua}, {"time", l_time},   {"quit", l_quit},
        {nullptr, nullptr}};
    register_fns(L_, this, api);

    lua_newtable(L_);
    static const luaL_Reg cam[] = {
        {"get", l_cam_get},     {"home", l_cam_home},   {"set", l_cam_set},     {"orbit", l_cam_orbit},
        {"roll", l_cam_roll},   {"pan", l_cam_pan},     {"zoom", l_cam_zoom},
        {"dolly", l_cam_dolly}, {"reset", l_cam_reset}, {"snap", l_cam_snap},
        {nullptr, nullptr}};
    register_fns(L_, this, cam);
    lua_setfield(L_, -2, "camera");

    lua_newtable(L_);
    static const luaL_Reg rec[] = {
        {"start", l_record_start}, {"stop", l_record_stop}, {"active", l_record_active},
        {nullptr, nullptr}};
    register_fns(L_, this, rec);
    lua_setfield(L_, -2, "record");
    lua_setglobal(L_, "nv3d");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_print, 1);
    lua_setglobal(L_, "print");

    exec(kLuaCinema, "=cinema");
}

LuaEngine::~LuaEngine() { lua_close(L_); }

bool LuaEngine::pcall(int nargs, int nresults) {
    if (lua_pcall(L_, nargs, nresults, 0) != LUA_OK) {
        std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        host.message("nv3d: " + err, true);
        return false;
    }
    return true;
}

bool LuaEngine::exec(const std::string& code, const std::string& chunk_name) {
    if (luaL_loadbuffer(L_, code.data(), code.size(), chunk_name.c_str()) != LUA_OK) {
        host.message(std::string("nv3d: ") + lua_tostring(L_, -1), true);
        lua_pop(L_, 1);
        return false;
    }
    return pcall(0, 0);
}

bool LuaEngine::exec_file(const std::string& path) {
    if (luaL_loadfile(L_, path.c_str()) != LUA_OK) {
        host.message(std::string("nv3d: ") + lua_tostring(L_, -1), true);
        lua_pop(L_, 1);
        return false;
    }
    return pcall(0, 0);
}

void LuaEngine::call(const std::string& path, const mp::Value& args) {
    lua_getglobal(L_, "nv3d");
    size_t start = 0;
    while (start <= path.size()) {
        size_t dot = path.find('.', start);
        std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!lua_istable(L_, -1)) {
            lua_pop(L_, 1);
            host.message("nv3d: no such function: " + path, true);
            return;
        }
        lua_getfield(L_, -1, part.c_str());
        lua_remove(L_, -2);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        host.message("nv3d: no such function: " + path, true);
        return;
    }
    for (auto& a : args.as_array()) push_value(L_, a);
    pcall(int(args.size()), 0);
}

bool LuaEngine::handle_key(const std::string& keys) {
    auto it = keymaps.find(keys);
    if (it == keymaps.end()) return false;
    lua_rawgeti(L_, LUA_REGISTRYINDEX, it->second);
    pcall(0, 0);
    return true;
}

void LuaEngine::run_frame_hooks(double dt, double time) {
    std::vector<int> hooks = frame_hooks_;  // hooks may remove themselves
    for (int ref : hooks) {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        lua_pushnumber(L_, dt);
        lua_pushnumber(L_, time);
        pcall(2, 0);
    }
}

void LuaEngine::run_camera_input_hooks() {
    std::vector<int> hooks = camera_input_hooks_;
    for (int ref : hooks) {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        pcall(0, 0);
    }
}

void LuaEngine::run_ready_hooks() {
    std::vector<int> hooks;
    hooks.swap(ready_hooks_);
    for (int ref : hooks) {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        luaL_unref(L_, LUA_REGISTRYINDEX, ref);
        pcall(0, 0);
    }
}

const char* kLuaPrelude = R"lua(
-- Default NeoVim3D camera key maps. Override or remove them with
-- nv3d.map()/nv3d.unmap() in ~/.config/neovim3d/init.lua.
local cam, cinema = nv3d.camera, nv3d.cinema
-- Moving the camera by hand ends cinema mode, keeping the current view.
local function manual(fn)
  return function()
    cinema.stop(true)
    fn()
  end
end
nv3d.map('<C-S-Left>',     manual(function() cam.orbit(-15, 0) end))
nv3d.map('<C-S-Right>',    manual(function() cam.orbit(15, 0) end))
nv3d.map('<C-S-Up>',       manual(function() cam.orbit(0, -10) end))
nv3d.map('<C-S-Down>',     manual(function() cam.orbit(0, 10) end))
nv3d.map('<C-S-PageUp>',   manual(function() cam.zoom(0.8) end))
nv3d.map('<C-S-PageDown>', manual(function() cam.zoom(1.25) end))
nv3d.map('<C-S-Home>',     manual(function() cam.reset() end))
nv3d.map('<C-S-End>',      function() nv3d.set('follow_cursor', not nv3d.get('follow_cursor')) end)
-- Cinema mode: soothing preset camera shots while you work.
nv3d.map('<C-S-c>',        function() cinema.toggle() end)
nv3d.map('<C-S-n>',        function() cinema.next() end)
)lua";
