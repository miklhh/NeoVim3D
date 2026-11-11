#include "app.hpp"

#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include "input.hpp"
#include "png.hpp"

namespace {

constexpr float kDeg = 3.14159265358979f / 180.0f;

// Lua run inside Nvim before it sources the user's config. It exposes the
// frontend to Nvim as `require('neovim3d')` and the `:Nv3d {lua}` command.
const char* kNvimSetup = R"lua(
local chan = ...
vim.g.neovim3d = 1
vim.g.neovim3d_channel = chan
local function proxy(path)
  return setmetatable({}, {
    __index = function(_, k) return proxy(path == '' and k or (path .. '.' .. k)) end,
    __call = function(_, ...) vim.rpcnotify(chan, 'nv3d_call', path, { ... }) end,
  })
end
local M = proxy('')
rawset(M, 'exec', function(code) vim.rpcnotify(chan, 'nv3d_lua', code) end)
package.loaded['neovim3d'] = M
vim.api.nvim_create_user_command('Nv3d', function(o) M.exec(o.args) end,
  { nargs = '+', complete = 'lua', desc = 'Run Lua in the NeoVim3D frontend' })
)lua";

App* app_of(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

bool file_exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string default_config_path() {
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x)
        return std::string(x) + "/neovim3d/init.lua";
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/.config/neovim3d/init.lua";
    return "";
}

// Parse a 'guifont' value such as "JetBrains_Mono:h13,Fallback:h12".
bool parse_guifont(const std::string& value, std::string& family, float& points) {
    std::string first = value.substr(0, value.find(','));
    if (first.empty()) return false;
    family.clear();
    points = 0;
    size_t colon = first.find(':');
    std::string name = first.substr(0, colon);
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '\\' && i + 1 < name.size()) {
            family.push_back(name[++i]);
        } else {
            family.push_back(name[i] == '_' ? ' ' : name[i]);
        }
    }
    while (colon != std::string::npos) {
        size_t next = first.find(':', colon + 1);
        std::string opt = first.substr(colon + 1, next == std::string::npos ? std::string::npos
                                                                           : next - colon - 1);
        if (opt.size() > 1 && opt[0] == 'h') points = std::strtof(opt.c_str() + 1, nullptr);
        colon = next;
    }
    return true;
}

}  // namespace

App::App(AppConfig cfg) : cfg_(std::move(cfg)) { lua_ = std::make_unique<LuaEngine>(*this); }

App::~App() {
    std::string err;
    if (recorder_.active() && !record_stop(err)) std::fprintf(stderr, "neovim3d: %s\n", err.c_str());
    nvim_.stop();  // before GLFW goes away: the reader thread wakes the event loop
    if (win_) {
        renderer_.destroy();
        glfwDestroyWindow(win_);
        glfwTerminate();
    }
}

// ---------------------------------------------------------------------------
// LuaHost

void App::message(const std::string& msg, bool error) {
    std::fprintf(stderr, "%s\n", msg.c_str());
    if (!attached_) {
        pending_messages_.push_back({msg, error});
        return;
    }
    nvim_.request("nvim_echo",
                  {mp::Array{mp::Array{msg, error ? "ErrorMsg" : "Normal"}}, true, mp::Map{}});
}

void App::nvim_command(const std::string& cmd) {
    nvim_.request("nvim_command", {cmd}, [this](const mp::Value& err, const mp::Value&) {
        if (!err.is_nil()) message(err[1].as_str(), true);
    });
}

void App::nvim_input(const std::string& keys) {
    nvim_.notify("nvim_input", {keys});
    input_sent_ = true;
}

void App::quit() {
    if (win_) glfwSetWindowShouldClose(win_, GLFW_TRUE);
}

bool App::record_start(const RecordOptions& opts, std::string& error) {
    if (!recorder_.start(opts, fb_w_, fb_h_, error)) return false;
    frame_buf_.resize(size_t(fb_w_) * fb_h_ * 3);
    sim_time_ = glfwGetTime();
    // Frames are captured before the swap, so there is no need to wait for
    // vsync, and compositors throttle hidden or unfocused windows, which
    // would otherwise stall the recording.
    glfwSwapInterval(0);
    std::fprintf(stderr, "neovim3d: recording %dx%d @ %d fps to %s\n", fb_w_, fb_h_, opts.fps,
                 opts.path.c_str());
    return true;
}

bool App::record_stop(std::string& error) {
    if (!recorder_.active()) return true;
    int frames = recorder_.frames(), fps = recorder_.fps();
    std::string path = recorder_.path();
    std::fprintf(stderr, "neovim3d: encoding %d frames...\n", frames);
    glfwSwapInterval(1);
    if (!recorder_.finish(error)) return false;
    message("neovim3d: recorded " + std::to_string(frames) + " frames (" +
                std::to_string(frames / std::max(fps, 1)) + " s) to " + path,
            false);
    return true;
}

void App::nvim_exec_lua(const std::string& code) {
    nvim_.request("nvim_exec_lua", {code, mp::Array{}},
                  [this](const mp::Value& err, const mp::Value&) {
                      if (!err.is_nil()) message(err[1].as_str(), true);
                  });
}

void App::camera_reset() {
    cam_.goal = home_pose();
    needs_draw_ = true;
}

void App::options_changed() {
    bool home = win_ && camera_at_home();
    if (win_ && (!font_loaded_ || opt_.font != loaded_font_)) {
        if (font_.load(opt_.font)) {
            font_loaded_ = true;
            loaded_font_ = opt_.font;
        }
        if (std::string w = font_.take_warning(); !w.empty()) message("neovim3d: " + w, true);
        loaded_depth_ = -1;
    }
    if (opt_.depth != loaded_depth_) {
        font_.set_depth(std::max(0.0f, opt_.depth));
        loaded_depth_ = opt_.depth;
    }
    opt_.font_size = std::clamp(opt_.font_size, 4.0f, 200.0f);
    cam_.fov = std::clamp(opt_.fov, 5.0f, 150.0f) * kDeg;
    cam_.smoothing = opt_.smoothing;
    if (win_) {
        update_grid_size();
        if (home) {
            cam_.goal = home_pose();
            cam_.snap();
        }
    }
    needs_scene_ = true;
}

// ---------------------------------------------------------------------------
// Setup

bool App::init_window() {
    glfwSetErrorCallback(
        [](int code, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", code, desc); });
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, std::max(0, opt_.msaa));
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    win_ = glfwCreateWindow(cfg_.window_width, cfg_.window_height, "NeoVim3D", nullptr, nullptr);
    if (!win_) return false;
    glfwSetWindowUserPointer(win_, this);
    glfwMakeContextCurrent(win_);
    glfwSwapInterval(1);

    glfwSetKeyCallback(win_, [](GLFWwindow* w, int k, int s, int a, int m) {
        app_of(w)->on_key(k, s, a, m);
    });
    glfwSetCharCallback(win_, [](GLFWwindow* w, unsigned c) { app_of(w)->on_char(c); });
    glfwSetMouseButtonCallback(win_, [](GLFWwindow* w, int b, int a, int m) {
        app_of(w)->on_mouse_button(b, a, m);
    });
    glfwSetCursorPosCallback(win_, [](GLFWwindow* w, double x, double y) {
        app_of(w)->on_cursor_pos(x, y);
    });
    glfwSetScrollCallback(win_, [](GLFWwindow* w, double x, double y) {
        app_of(w)->on_scroll(x, y);
    });
    glfwSetFramebufferSizeCallback(win_, [](GLFWwindow* w, int width, int height) {
        app_of(w)->on_framebuffer_size(width, height);
    });
    glfwSetWindowRefreshCallback(win_, [](GLFWwindow* w) { app_of(w)->request_redraw(); });

    if (!renderer_.init()) return false;
    glfwGetFramebufferSize(win_, &fb_w_, &fb_h_);
    return true;
}

void App::attach() {
    nvim_.request("nvim_get_api_info", {}, [this](const mp::Value& err, const mp::Value& res) {
        if (!err.is_nil()) {
            message("neovim3d: nvim_get_api_info failed", true);
            return;
        }
        int64_t chan = res[0].as_int();
        nvim_.request("nvim_exec_lua", {kNvimSetup, mp::Array{chan}},
                      [this](const mp::Value& e, const mp::Value&) {
                          if (!e.is_nil()) message("neovim3d setup: " + e[1].as_str(), true);
                      });
        mp::Map uiopts{{"rgb", true}, {"ext_linegrid", true}, {"ext_multigrid", true}};
        nvim_.request("nvim_ui_attach", {cols_, rows_, uiopts},
                      [this](const mp::Value& e, const mp::Value&) {
                          if (!e.is_nil()) {
                              std::fprintf(stderr, "neovim3d: ui_attach failed: %s\n",
                                           e[1].as_str().c_str());
                              return;
                          }
                          attached_ = true;
                          for (auto& [m, is_err] : pending_messages_) message(m, is_err);
                          pending_messages_.clear();
                      });
    });
}

int App::run() {
    // The frontend config runs first so it can choose fonts, MSAA etc.
    lua_->exec(kLuaPrelude, "=prelude");
    if (cfg_.load_config) {
        std::string path = cfg_.config_path.empty() ? default_config_path() : cfg_.config_path;
        if (!path.empty() && file_exists(path))
            lua_->exec_file(path);
        else if (!cfg_.config_path.empty())
            message("neovim3d: config file not found: " + path, true);
    }

    if (!init_window()) {
        std::fprintf(stderr, "neovim3d: failed to create an OpenGL 3.3 window\n");
        return 1;
    }
    options_changed();  // loads the font and sizes the grid
    cam_.goal = home_pose();
    cam_.snap();

    ui_.on_title = [this](const std::string& t) {
        glfwSetWindowTitle(win_, t.empty() ? "NeoVim3D" : t.c_str());
    };
    ui_.on_option = [this](const std::string& name, const mp::Value& v) {
        // Nvim reports every option once on attach, before the user's init.lua
        // runs. Those are just Nvim's defaults: let the frontend config win and
        // only follow later changes.
        if (seen_options_.insert(name).second) return;
        if (name == "guifont" && !v.as_str().empty()) {
            std::string fam;
            float pts = 0;
            if (parse_guifont(v.as_str(), fam, pts)) {
                opt_.font = fam;
                if (pts > 0) opt_.font_size = pts * 4.0f / 3.0f;
                options_changed();
            }
        } else if (name == "linespace") {
            opt_.linespace = float(v.as_int());
            options_changed();
        }
    };

    if (!nvim_.start(cfg_.nvim_path, cfg_.nvim_args, [] { glfwPostEmptyEvent(); })) return 1;
    attach();

    double last = glfwGetTime();
    bool blink_visible = true;
    while (!glfwWindowShouldClose(win_) && !nvim_.exited()) {
        double now = glfwGetTime();
        double next_blink = 0;
        bool want_blink = cursor_blink_visible(now, next_blink);
        if (needs_draw_ || needs_scene_ || lua_->has_frame_hooks() || cam_.moving() ||
            recorder_.active()) {
            glfwPollEvents();
        } else if (next_blink > now) {
            glfwWaitEventsTimeout(next_blink - now);
        } else {
            glfwWaitEvents();
        }

        pump_nvim();

        now = glfwGetTime();
        double dt = std::min(now - last, 0.25);
        last = now;
        if (recorder_.active()) {
            // Fixed time steps: the video plays back smoothly however long
            // each frame took to render and encode.
            dt = 1.0 / recorder_.fps();
            sim_time_ += dt;
            now = sim_time_;
        }
        want_blink = cursor_blink_visible(now, next_blink);
        if (want_blink != blink_visible) {
            blink_visible = want_blink;
            needs_scene_ = true;
        }
        if (lua_->has_frame_hooks()) {
            input_sent_ = false;
            lua_->run_frame_hooks(dt, now);
            needs_draw_ = true;
            // While recording, show the result of scripted keys in this frame.
            if (input_sent_ && recorder_.active()) wait_for_redraw(0.5);
        }
        if (needs_scene_) {
            rebuild_scene();
            needs_draw_ = true;
        }
        if (opt_.follow_cursor) cam_.goal.target = scene_.cursor_pos + opt_.follow_offset;
        if (cam_.update(float(dt))) needs_draw_ = true;
        if (!cfg_.screenshot_path.empty() && first_flush_at_ > 0 && now - first_flush_at_ > 1.5 &&
            (!cam_.moving() || lua_->has_frame_hooks()) && !needs_scene_) {
            return save_screenshot(cfg_.screenshot_path) ? 0 : 1;
        }
        if (needs_draw_ || recorder_.active()) draw();
    }
    return 0;
}

void App::pump_nvim() {
    nvim_.dispatch(
        [this](const std::string& m, const mp::Value& p) { handle_notification(m, p); },
        [](const std::string&, const mp::Value&, mp::Value& error) {
            error = mp::Array{0, "neovim3d does not handle requests"};
            return mp::Value();
        });
    if (ui_.flushed) {
        ui_.flushed = false;
        ++flush_count_;
        if (ui_.generation != last_generation_) needs_scene_ = true;
        if (first_flush_at_ == 0) {
            first_flush_at_ = glfwGetTime();
            rebuild_scene();
            lua_->run_ready_hooks();
        }
    }
}

void App::wait_for_redraw(double timeout) {
    uint64_t before = flush_count_;
    double deadline = glfwGetTime() + timeout;
    while (flush_count_ == before && glfwGetTime() < deadline && !nvim_.exited()) {
        glfwWaitEventsTimeout(0.005);
        pump_nvim();
    }
    // Keys can trigger several flushes (e.g. mode change, then text); take a
    // moment to collect the rest.
    double settle = glfwGetTime() + 0.01;
    while (glfwGetTime() < settle) {
        glfwWaitEventsTimeout(0.002);
        pump_nvim();
    }
}

void App::capture_frame() {
    if (fb_w_ != recorder_.width() || fb_h_ != recorder_.height()) return;  // resized: skip
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, fb_w_, fb_h_, GL_RGB, GL_UNSIGNED_BYTE, frame_buf_.data());
    if (!recorder_.add_frame(frame_buf_.data())) {
        std::string err;
        recorder_.finish(err);
        message("neovim3d: recording stopped: ffmpeg is not accepting frames", true);
    }
}

void App::handle_notification(const std::string& method, const mp::Value& params) {
    if (method == "redraw") {
        ui_.handle_redraw(params);
        int cur[3] = {ui_.cursor_grid, ui_.cursor_row, ui_.cursor_col};
        if (!std::equal(cur, cur + 3, last_cursor_)) {
            std::copy(cur, cur + 3, last_cursor_);
            cursor_moved_at_ = glfwGetTime();
        }
    } else if (method == "nv3d_lua") {
        lua_->exec(params[0].as_str(), "=:Nv3d");
    } else if (method == "nv3d_call") {
        lua_->call(params[0].as_str(), params[1]);
    }
}

bool App::cursor_blink_visible(double now, double& next_change) const {
    next_change = 0;
    if (ui_.busy) return false;
    if (!opt_.cursor_blink || ui_.mode_idx < 0 || size_t(ui_.mode_idx) >= ui_.modes.size())
        return true;
    const ModeInfo& m = ui_.modes[size_t(ui_.mode_idx)];
    if (m.blinkon <= 0 || m.blinkoff <= 0) return true;
    double t = (now - cursor_moved_at_) * 1000.0;
    if (t < m.blinkwait) {
        next_change = cursor_moved_at_ + m.blinkwait / 1000.0;
        return true;
    }
    double period = m.blinkon + m.blinkoff;
    double phase = std::fmod(t - m.blinkwait, period);
    bool on = phase < m.blinkon;
    next_change = now + ((on ? m.blinkon : period) - phase) / 1000.0;
    return on;
}

// ---------------------------------------------------------------------------
// Layout and drawing

float App::pixel_size() const {
    float sx = 1, sy = 1;
    if (win_) glfwGetWindowContentScale(win_, &sx, &sy);
    return opt_.font_size * sy / std::max(0.1f, font_.em_size());
}

void App::update_grid_size() {
    float px = pixel_size();
    float cell_w = font_.cell_width() * px;
    float cell_h = px * (1.0f + opt_.linespace * font_.em_size() / std::max(1.0f, opt_.font_size));
    int cols = std::max(1, int(float(fb_w_) / std::max(1.0f, cell_w)));
    int rows = std::max(1, int(float(fb_h_) / std::max(1.0f, cell_h)));
    if (cols == cols_ && rows == rows_) return;
    cols_ = cols;
    rows_ = rows;
    if (attached_) nvim_.request("nvim_ui_try_resize", {cols_, rows_});
}

CameraState App::home_pose() const {
    // Frame the screen so one line unit maps to `pixel_size()` pixels and the
    // top-left of the base grid sits in the top-left window corner.
    float px = std::max(1.0f, pixel_size());
    float w = float(std::max(fb_w_, 1)) / px, h = float(std::max(fb_h_, 1)) / px;
    CameraState s;
    s.target = Vec3(w / 2, -h / 2, 0);
    s.distance = (h / 2) / std::tan(cam_.fov / 2);
    return s;
}

bool App::camera_at_home() const {
    CameraState h = home_pose();
    const CameraState& g = cam_.goal;
    return std::abs(g.yaw - h.yaw) < 1e-3f && std::abs(g.pitch - h.pitch) < 1e-3f &&
           std::abs(g.roll - h.roll) < 1e-3f && std::abs(g.distance - h.distance) < 1e-2f &&
           length(g.target - h.target) < 1e-2f;
}

void App::on_framebuffer_size(int w, int h) {
    bool home = camera_at_home();
    fb_w_ = w;
    fb_h_ = h;
    update_grid_size();
    if (home) {
        cam_.goal = home_pose();
        cam_.snap();
    }
    needs_draw_ = true;
}

void App::rebuild_scene() {
    double dummy;
    bool cursor_on = cursor_blink_visible(glfwGetTime(), dummy);
    scene_ = build_scene(ui_, font_, opt_, cursor_on);
    renderer_.upload(font_, scene_);
    last_generation_ = ui_.generation;
    needs_scene_ = false;
}

void App::draw() {
    renderer_.draw(scene_, cam_, opt_, fb_w_, fb_h_, ui_.default_bg);
    if (recorder_.active()) capture_frame();
    glfwSwapBuffers(win_);
    needs_draw_ = false;
}

bool App::save_screenshot(const std::string& path) {
    renderer_.draw(scene_, cam_, opt_, fb_w_, fb_h_, ui_.default_bg);
    glFinish();
    std::vector<uint8_t> pixels(size_t(fb_w_) * fb_h_ * 3), flipped(pixels.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, fb_w_, fb_h_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const size_t stride = size_t(fb_w_) * 3;
    for (int y = 0; y < fb_h_; ++y)
        std::copy_n(pixels.begin() + ptrdiff_t(size_t(fb_h_ - 1 - y) * stride), stride,
                    flipped.begin() + ptrdiff_t(size_t(y) * stride));
    if (!write_png(path, fb_w_, fb_h_, flipped)) {
        std::fprintf(stderr, "neovim3d: could not write %s\n", path.c_str());
        return false;
    }
    std::fprintf(stderr, "neovim3d: saved screenshot to %s\n", path.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Input

void App::on_key(int key, int scancode, int action, int mods) {
    mods_ = mods;
    if (action == GLFW_RELEASE) return;
    std::string keys = translate_key(key, mods, glfwGetKeyName(key, scancode));
    if (keys.empty()) return;
    if (lua_->handle_key(keys)) {
        needs_draw_ = true;
        return;
    }
    nvim_input(keys);
}

void App::on_char(unsigned codepoint) {
    std::string keys = translate_char(codepoint, mods_);
    if (keys.empty()) return;
    if (lua_->handle_key(keys)) {
        needs_draw_ = true;
        return;
    }
    nvim_input(keys);
}

App::Hit App::pick(double x, double y) const {
    Hit best;
    int ww = 1, wh = 1;
    glfwGetWindowSize(win_, &ww, &wh);
    float ndc_x = float(2.0 * x / std::max(ww, 1) - 1.0);
    float ndc_y = float(1.0 - 2.0 * y / std::max(wh, 1));
    float aspect = fb_h_ > 0 ? float(fb_w_) / float(fb_h_) : 1.0f;
    Vec3 o, d;
    cam_.ray(ndc_x, ndc_y, aspect, o, d);
    float best_t = 1e30f;
    for (const GridPlacement& p : scene_.placements) {
        if (std::abs(d.z) < 1e-6f) continue;
        float t = (p.z - o.z) / d.z;
        if (t <= 0 || t >= best_t) continue;
        Vec3 h = o + d * t;
        int col = int(std::floor((h.x - p.x) / scene_.cell_w));
        int row = int(std::floor((p.y - h.y) / scene_.cell_h));
        if (row < 0 || col < 0 || row >= p.rows || col >= p.cols) continue;
        best_t = t;
        best = {p.grid, row, col};
    }
    return best;
}

void App::send_mouse(const std::string& button, const std::string& action, int mods, const Hit& h) {
    if (!attached_ || h.grid < 0) return;
    nvim_.notify("nvim_input_mouse",
                 {button, action, mod_prefix(mods, true), h.grid, h.row, h.col});
}

namespace {
const char* button_name(int b) {
    switch (b) {
        case GLFW_MOUSE_BUTTON_LEFT: return "left";
        case GLFW_MOUSE_BUTTON_RIGHT: return "right";
        case GLFW_MOUSE_BUTTON_MIDDLE: return "middle";
        default: return nullptr;
    }
}
}  // namespace

// Mouse: left button goes to Nvim (Alt+left orbits), right-drag orbits,
// middle-drag pans, Ctrl+wheel zooms. A right/middle click without dragging
// is passed on to Nvim.
void App::on_mouse_button(int button, int action, int mods) {
    mods_ = mods;
    if (!button_name(button)) return;
    if (action == GLFW_PRESS) {
        if (drag_button_ >= 0) return;
        drag_button_ = button;
        drag_camera_ = false;
        press_x_ = mouse_x_;
        press_y_ = mouse_y_;
        press_mods_ = mods;
        if (button == GLFW_MOUSE_BUTTON_LEFT && !(mods & GLFW_MOD_ALT)) {
            last_hit_ = pick(mouse_x_, mouse_y_);
            send_mouse("left", "press", mods, last_hit_);
        }
        return;
    }
    if (action != GLFW_RELEASE || button != drag_button_) return;
    bool to_nvim_left = button == GLFW_MOUSE_BUTTON_LEFT && !(press_mods_ & GLFW_MOD_ALT);
    if (to_nvim_left) {
        send_mouse("left", "release", mods, last_hit_);
    } else if (!drag_camera_) {
        Hit h = pick(press_x_, press_y_);
        send_mouse(button_name(button), "press", press_mods_ & ~GLFW_MOD_ALT, h);
        send_mouse(button_name(button), "release", press_mods_ & ~GLFW_MOD_ALT, h);
    }
    drag_button_ = -1;
    drag_camera_ = false;
}

void App::on_cursor_pos(double x, double y) {
    double dx = x - mouse_x_, dy = y - mouse_y_;
    mouse_x_ = x;
    mouse_y_ = y;
    if (drag_button_ < 0) return;

    bool left_nvim = drag_button_ == GLFW_MOUSE_BUTTON_LEFT && !(press_mods_ & GLFW_MOD_ALT);
    if (left_nvim) {
        Hit h = pick(x, y);
        if (h.grid == last_hit_.grid && (h.row != last_hit_.row || h.col != last_hit_.col)) {
            last_hit_ = h;
            send_mouse("left", "drag", mods_, h);
        }
        return;
    }
    if (!drag_camera_ && std::hypot(x - press_x_, y - press_y_) > 4) {
        drag_camera_ = true;
        lua_->run_camera_input_hooks();
    }
    if (!drag_camera_) return;

    if (drag_button_ == GLFW_MOUSE_BUTTON_MIDDLE) {
        int ww = 1, wh = 1;
        glfwGetWindowSize(win_, &ww, &wh);
        float k = 2 * cam_.goal.distance * std::tan(cam_.fov / 2) / float(std::max(wh, 1));
        cam_.pan(float(-dx) * k, float(dy) * k);
    } else {
        cam_.orbit(float(-dx) * 0.006f, float(dy) * 0.006f);
    }
    needs_draw_ = true;
}

void App::on_scroll(double dx, double dy) {
    if (mods_ & GLFW_MOD_CONTROL) {
        lua_->run_camera_input_hooks();
        cam_.zoom(float(std::pow(0.88, dy)));
        needs_draw_ = true;
        return;
    }
    static double acc_x = 0, acc_y = 0;
    acc_x += dx;
    acc_y += dy;
    Hit h = pick(mouse_x_, mouse_y_);
    while (acc_y >= 1) send_mouse("wheel", "up", mods_, h), acc_y -= 1;
    while (acc_y <= -1) send_mouse("wheel", "down", mods_, h), acc_y += 1;
    while (acc_x >= 1) send_mouse("wheel", "left", mods_, h), acc_x -= 1;
    while (acc_x <= -1) send_mouse("wheel", "right", mods_, h), acc_x += 1;
}
