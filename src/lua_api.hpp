// Embedded Lua 5.4 interpreter exposing the `nv3d` API for configuring the
// 3D frontend (camera, rendering options, frontend key maps, frame hooks).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "camera.hpp"
#include "msgpack.hpp"
#include "options.hpp"
#include "recorder.hpp"

struct lua_State;

// What the Lua API needs from the application.
class LuaHost {
public:
    virtual ~LuaHost() = default;
    virtual Options& options() = 0;
    virtual void options_changed() = 0;
    virtual Camera& camera() = 0;
    virtual void camera_reset() = 0;
    // The pose that frames the editor like a flat 2D window.
    virtual CameraState camera_home() = 0;
    virtual Vec3 cursor_world() = 0;
    virtual void nvim_command(const std::string& cmd) = 0;
    virtual void nvim_input(const std::string& keys) = 0;
    virtual void nvim_exec_lua(const std::string& code) = 0;
    virtual void message(const std::string& msg, bool error) = 0;
    virtual void request_redraw() = 0;
    virtual bool record_start(const RecordOptions& opts, std::string& error) = 0;
    virtual bool record_stop(std::string& error) = 0;
    virtual bool recording() = 0;
    virtual void quit() = 0;
};

class LuaEngine {
public:
    explicit LuaEngine(LuaHost& host);
    ~LuaEngine();
    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    bool exec(const std::string& code, const std::string& chunk_name = "=nv3d");
    bool exec_file(const std::string& path);

    // Call `nv3d.<path>(args...)` with msgpack values converted to Lua.
    void call(const std::string& path, const mp::Value& args);

    // Frontend key maps: returns true if `keys` was handled.
    bool handle_key(const std::string& keys);
    bool has_frame_hooks() const { return !frame_hooks_.empty(); }
    void run_frame_hooks(double dt, double time);
    // Run (and drop) the one-shot "ready" hooks.
    void run_ready_hooks();
    // The user moved the camera by hand (mouse drag / Ctrl+wheel).
    void run_camera_input_hooks();

    lua_State* state() { return L_; }

    // Accessed from the C callbacks.
    LuaHost& host;
    std::map<std::string, int> keymaps;  // normalised keys -> registry ref
    std::vector<int> frame_hooks_;
    std::vector<int> ready_hooks_;
    std::vector<int> camera_input_hooks_;

private:
    bool pcall(int nargs, int nresults);
    lua_State* L_;
};

// The built-in Lua that runs before the user's config (default key maps).
extern const char* kLuaPrelude;
// The `nv3d.cinema` module (preset camera shots); loaded by LuaEngine itself.
extern const char* kLuaCinema;
