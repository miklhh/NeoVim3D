#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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
    bool record_start(const RecordOptions& o, std::string&) override {
        record_opts = o;
        rec = true;
        return true;
    }
    bool record_stop(std::string&) override {
        rec = false;
        return true;
    }
    bool recording() override { return rec; }
    void quit() override { quit_called = true; }
    RecordOptions record_opts;
    bool rec = false, quit_called = false;
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
    CHECK(lua.exec("nv3d.set('follow_offset', {-2, 3})"));
    CHECK_EQ(host.opt.follow_offset.x, -2.0f);
    CHECK_EQ(host.opt.follow_offset.y, 3.0f);
    CHECK_EQ(host.opt.follow_offset.z, 0.0f);
    CHECK_EQ(host.options_changed_calls, 3);
    CHECK(lua.exec("assert(nv3d.get('depth') == 0.5)"));
    CHECK(lua.exec("assert(nv3d.get('follow_offset')[2] == 3)"));
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

TEST(lua_record_api) {
    FakeHost host;
    LuaEngine lua(host);
    CHECK(lua.exec("nv3d.record.start{ path = 'out.gif', fps = 25, width = 800, colors = 64 }"));
    CHECK_EQ(host.record_opts.colors, 64);
    CHECK(host.rec);
    CHECK_EQ(host.record_opts.path, std::string("out.gif"));
    CHECK_EQ(host.record_opts.fps, 25);
    CHECK_EQ(host.record_opts.width, 800);
    CHECK(lua.exec("nv3d.record.start{ path = 'out.webp', quality = 50, method = 6, sharp_yuv = false }"));
    CHECK_EQ(host.record_opts.quality, 50);
    CHECK_EQ(host.record_opts.method, 6);
    CHECK(!host.record_opts.sharp_yuv);
    CHECK(lua.exec("assert(nv3d.record.active()); nv3d.record.stop(); assert(not nv3d.record.active())"));
    CHECK(lua.exec("nv3d.record.start('plain.mp4')"));
    CHECK_EQ(host.record_opts.fps, 20);
    CHECK(!lua.exec("nv3d.record.start{}"));  // path is required
    CHECK(lua.exec("nv3d.quit()"));
    CHECK(host.quit_called);
}

TEST(recorder_ffmpeg_command) {
    RecordOptions o;
    o.path = "it's a demo.gif";
    o.width = 640;
    std::string gif = Recorder::command(o, 1280, 800);
    CHECK(gif.find("-video_size 1280x800") != std::string::npos);
    CHECK(gif.find("palettegen=stats_mode=diff:max_colors=256") != std::string::npos);
    CHECK(gif.find("scale=640:-2") != std::string::npos);
    CHECK(gif.find("'it'\\''s a demo.gif'") != std::string::npos);  // shell-quoted
    o.path = "demo.mp4";
    o.width = 0;
    std::string mp4 = Recorder::command(o, 1280, 800);
    CHECK(mp4.find("palettegen") == std::string::npos);
    CHECK(mp4.find("yuv420p") != std::string::npos);
}

TEST(recorder_resize_rgb) {
    // 4x2 bottom-up image: bottom row white, top row black. Halving it and
    // flipping gives a 2x1 image that averages both rows.
    std::vector<uint8_t> src(4 * 2 * 3, 0);
    std::fill(src.begin(), src.begin() + 4 * 3, uint8_t(255));
    std::vector<uint8_t> dst(2 * 1 * 3);
    resize_rgb(src.data(), 4, 2, true, dst.data(), 2, 1);
    for (uint8_t v : dst) CHECK(v == 127 || v == 128);
    // Same size + flip just turns it upside down.
    std::vector<uint8_t> same(src.size());
    resize_rgb(src.data(), 4, 2, true, same.data(), 4, 2);
    CHECK_EQ(int(same[0]), 0);
    CHECK_EQ(int(same[4 * 3]), 255);
    // Non-integer downscale keeps a flat colour flat.
    std::vector<uint8_t> grey(10 * 6 * 3, 77), small(4 * 2 * 3);
    resize_rgb(grey.data(), 10, 6, false, small.data(), 4, 2);
    for (uint8_t v : small) CHECK_EQ(int(v), 77);
}

TEST(recorder_writes_animated_webp) {
    if (!Recorder::webp_supported()) {
        std::printf("  (built without libwebp, skipping)\n");
        return;
    }
    const char* path = "recorder_test.webp";
    RecordOptions o;
    o.path = path;
    o.fps = 30;
    o.width = 32;
    o.quality = 50;
    Recorder rec;
    std::string err;
    CHECK(rec.start(o, 64, 48, err));
    int ow, oh;
    Recorder::output_size(o, 64, 48, ow, oh);
    CHECK_EQ(ow, 32);
    CHECK_EQ(oh, 24);
    std::vector<uint8_t> frame(64 * 48 * 3);
    for (int i = 0; i < 12; ++i) {
        for (size_t p = 0; p < frame.size(); p += 3) frame[p] = uint8_t(i * 20);  // changing red
        CHECK(rec.add_frame(frame.data()));
    }
    CHECK(rec.finish(err));
    CHECK(!rec.active());

    FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::vector<uint8_t> d(1 << 20);
    d.resize(std::fread(d.data(), 1, d.size(), f));
    std::fclose(f);
    std::remove(path);
    CHECK(d.size() > 12 && std::memcmp(d.data(), "RIFF", 4) == 0 && std::memcmp(d.data() + 8, "WEBP", 4) == 0);
    // Walk the chunks: one ANIM with loop 0, one ANMF per frame, ~33 ms each.
    int frames = 0, total_ms = 0, loop = -1;
    for (size_t pos = 12; pos + 8 <= d.size();) {
        uint32_t size = d[pos + 4] | d[pos + 5] << 8 | d[pos + 6] << 16 | uint32_t(d[pos + 7]) << 24;
        if (std::memcmp(&d[pos], "ANIM", 4) == 0) loop = d[pos + 12] | d[pos + 13] << 8;
        if (std::memcmp(&d[pos], "ANMF", 4) == 0) {
            ++frames;
            total_ms += d[pos + 20] | d[pos + 21] << 8 | d[pos + 22] << 16;
        }
        pos += 8 + size + (size & 1);
    }
    CHECK_EQ(loop, 0);
    CHECK_EQ(frames, 12);
    CHECK_EQ(total_ms, 400);  // 12 frames at 30 fps
}
