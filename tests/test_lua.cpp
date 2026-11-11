#include <algorithm>
#include <cmath>

#include "check.hpp"
#include "lua_api.hpp"

namespace {

struct FakeHost : LuaHost {
    Options opt;
    Camera cam;
    int options_changed_calls = 0;
    std::vector<std::string> commands, inputs, messages;

    Options& options() override { return opt; }
    void options_changed() override { ++options_changed_calls; }
    Camera& camera() override { return cam; }
    void camera_reset() override { cam.goal = CameraState{}; }
    CameraState camera_home() override { return CameraState{}; }
    Vec3 cursor_world() override { return Vec3(1, 2, 3); }
    void nvim_command(const std::string& c) override { commands.push_back(c); }
    void nvim_input(const std::string& k) override { inputs.push_back(k); }
    void nvim_exec_lua(const std::string&) override {}
    void message(const std::string& m, bool) override { messages.push_back(m); }
    void request_redraw() override {}
};

}  // namespace

TEST(lua_options) {
    FakeHost host;
    LuaEngine lua(host);
    CHECK(lua.exec("nv3d.set('depth', 0.5)"));
    CHECK_EQ(host.opt.depth, 0.5f);
    CHECK(lua.exec("nv3d.set{ font = 'Fira Code', panels = false, light_dir = {0, 0, 1} }"));
    CHECK_EQ(host.opt.font, std::string("Fira Code"));
    CHECK(!host.opt.panels);
    CHECK_EQ(host.opt.light_dir.z, 1.0f);
    CHECK_EQ(host.options_changed_calls, 2);
    CHECK(lua.exec("assert(nv3d.get('depth') == 0.5)"));
    // Unknown options are reported as errors, not crashes.
    CHECK(!lua.exec("nv3d.set('nope', 1)"));
    CHECK(!host.messages.empty());
}

TEST(lua_camera) {
    FakeHost host;
    LuaEngine lua(host);
    CHECK(lua.exec("nv3d.camera.orbit(90, 0)"));
    CHECK(std::abs(host.cam.goal.yaw - 3.14159265f / 2) < 1e-4f);
    CHECK(lua.exec("nv3d.camera.set{ distance = 42, target = { x = 1 } }"));
    CHECK_EQ(host.cam.goal.distance, 42.0f);
    CHECK_EQ(host.cam.goal.target.x, 1.0f);
    CHECK(lua.exec("local c = nv3d.camera.get(); assert(math.abs(c.yaw - 90) < 1e-3)"));
    CHECK(lua.exec("local x, y, z = nv3d.cursor(); assert(x == 1 and y == 2 and z == 3)"));
}

TEST(lua_keymaps_and_nvim_calls) {
    FakeHost host;
    LuaEngine lua(host);
    CHECK(lua.exec("nv3d.map('<c-s-left>', function() nv3d.command('echo 1') end)"));
    CHECK(lua.handle_key("<C-S-Left>"));
    CHECK(!lua.handle_key("<C-S-Right>"));
    CHECK_EQ(host.commands.size(), size_t(1));
    CHECK(lua.exec("nv3d.unmap('<C-S-Left>')"));
    CHECK(!lua.handle_key("<C-S-Left>"));
    CHECK(lua.exec("nv3d.input('ihi<Esc>')"));
    CHECK_EQ(host.inputs.at(0), std::string("ihi<Esc>"));
}

TEST(lua_prelude_and_call_from_nvim) {
    FakeHost host;
    LuaEngine lua(host);
    CHECK(lua.exec(kLuaPrelude, "=prelude"));
    CHECK(lua.handle_key("<C-S-Right>"));
    // What `require('neovim3d').camera.zoom(2)` sends from inside Nvim.
    float before = host.cam.goal.distance;
    lua.call("camera.zoom", mp::Array{2});
    CHECK_EQ(host.cam.goal.distance, before * 2);
    lua.call("set", mp::Array{"layer_gap", 1.5});
    CHECK_EQ(host.opt.layer_gap, 1.5f);
    lua.call("does.not.exist", mp::Array{});
    CHECK(!host.messages.empty());
}

TEST(lua_frame_hooks) {
    FakeHost host;
    LuaEngine lua(host);
    CHECK(lua.exec("n = 0; id = nv3d.on('frame', function(dt, t) n = n + 1 end)"));
    CHECK(lua.has_frame_hooks());
    lua.run_frame_hooks(0.016, 1.0);
    lua.run_frame_hooks(0.016, 1.016);
    CHECK(lua.exec("assert(n == 2); nv3d.off(id)"));
    CHECK(!lua.has_frame_hooks());
}

namespace {

constexpr float kRad = 3.14159265f / 180.0f;

// Step the frame hooks for `seconds` at 60 fps and return the largest
// per-frame change of the camera goal (yaw/pitch/roll in degrees, distance).
float run_frames(LuaEngine& lua, FakeHost& host, double seconds, double& t) {
    float worst = 0;
    CameraState prev = host.cam.goal;
    for (int i = 0; i < int(seconds * 60); ++i) {
        t += 1.0 / 60;
        lua.run_frame_hooks(1.0 / 60, t);
        const CameraState& g = host.cam.goal;
        worst = std::max({worst, std::abs(g.yaw - prev.yaw) / kRad, std::abs(g.pitch - prev.pitch) / kRad,
                          std::abs(g.roll - prev.roll) / kRad, std::abs(g.distance - prev.distance),
                          length(g.target - prev.target)});
        prev = g;
    }
    return worst;
}

}  // namespace

TEST(cinema_moves_gently_and_stays_bounded) {
    FakeHost host;
    LuaEngine lua(host);
    CHECK(lua.exec("nv3d.cinema.start{ duration = 10, transition = 3 }"));
    CHECK(lua.exec("assert(nv3d.cinema.running() and nv3d.cinema.current() == 'drift')"));
    double t = 0;
    float max_yaw = 0, max_pitch = 0, min_dist = 1e9f, max_dist = 0;
    float worst_step = 0;
    // Long enough to play every preset and cross-fade between all of them.
    for (int s = 0; s < 90; ++s) {
        worst_step = std::max(worst_step, run_frames(lua, host, 1.0, t));
        max_yaw = std::max(max_yaw, std::abs(host.cam.goal.yaw) / kRad);
        max_pitch = std::max(max_pitch, std::abs(host.cam.goal.pitch) / kRad);
        min_dist = std::min(min_dist, host.cam.goal.distance);
        max_dist = std::max(max_dist, host.cam.goal.distance);
    }
    CHECK(max_yaw > 3);         // it actually moves...
    CHECK(max_yaw < 25);        // ...but never so far that text becomes unreadable
    CHECK(max_pitch < 25);
    CHECK(min_dist > 0.7f * 10 && max_dist < 1.05f * 10);
    CHECK(worst_step < 0.25f);  // soothing: no jumps between frames
    CHECK(lua.exec("assert(nv3d.cinema.current() ~= 'drift')"));
}

TEST(cinema_next_stop_and_glide_home) {
    FakeHost host;
    LuaEngine lua(host);
    double t = 0;
    CHECK(lua.exec("nv3d.cinema.start{ transition = 2 }"));
    run_frames(lua, host, 5, t);
    CHECK(lua.exec("nv3d.cinema.next(); assert(nv3d.cinema.current() == 'orbit')"));
    // Interrupt the cross-fade with another cut: still no jump.
    float step = run_frames(lua, host, 1, t);
    CHECK(lua.exec("nv3d.cinema.next(); assert(nv3d.cinema.current() == 'dolly')"));
    step = std::max(step, run_frames(lua, host, 3, t));
    CHECK(step < 0.25f);
    CHECK(lua.exec("nv3d.cinema.stop(); assert(not nv3d.cinema.running())"));
    CHECK(run_frames(lua, host, 3, t) < 0.25f);
    CHECK(!lua.has_frame_hooks());  // hook removed once home
    CHECK(std::abs(host.cam.goal.yaw) < 1e-4f && std::abs(host.cam.goal.pitch) < 1e-4f);
}

TEST(cinema_stops_on_manual_camera_input) {
    FakeHost host;
    LuaEngine lua(host);
    double t = 0;
    CHECK(lua.exec(kLuaPrelude, "=prelude"));
    CHECK(lua.handle_key("<C-S-c>"));  // toggle on
    run_frames(lua, host, 20, t);
    CHECK(lua.exec("assert(nv3d.cinema.running())"));
    float yaw = host.cam.goal.yaw;
    lua.run_camera_input_hooks();  // e.g. a mouse drag
    CHECK(lua.exec("assert(not nv3d.cinema.running())"));
    run_frames(lua, host, 1, t);
    CHECK_EQ(host.cam.goal.yaw, yaw);  // camera left where the user grabbed it
}

TEST(cinema_custom_shots_and_errors) {
    FakeHost host;
    LuaEngine lua(host);
    double t = 0;
    CHECK(lua.exec("nv3d.cinema.add('spin', function(t, a) return { yaw = 10 * a } end)"));
    CHECK(lua.exec("nv3d.cinema.start{ shots = { 'spin' }, transition = 1, intensity = 0.5 }"));
    run_frames(lua, host, 2, t);
    CHECK(std::abs(host.cam.goal.yaw / kRad - 5) < 1e-3f);
    CHECK(!lua.exec("nv3d.cinema.start{ shots = { 'nope' } }"));
    // Follow: the framing drifts towards the cursor (FakeHost cursor is at 1,2).
    CHECK(lua.exec("nv3d.cinema.start{ shots = 'spin', follow = 1, follow_rate = 5, transition = 1 }"));
    run_frames(lua, host, 5, t);
    CHECK(std::abs(host.cam.goal.target.x - 1) < 1e-2f && std::abs(host.cam.goal.target.y - 2) < 1e-2f);
}
