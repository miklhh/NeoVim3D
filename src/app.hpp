#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "camera.hpp"
#include "font.hpp"
#include "lua_api.hpp"
#include "nvim.hpp"
#include "options.hpp"
#include "recorder.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "ui_state.hpp"

struct GLFWwindow;

struct AppConfig {
    std::string nvim_path = "nvim";
    std::string config_path;  // empty = default location
    bool load_config = true;
    std::string screenshot_path;  // render once, save a PNG and exit
    int window_width = 1280, window_height = 800;
    std::vector<std::string> nvim_args;
};

class App : public LuaHost {
public:
    explicit App(AppConfig cfg);
    ~App() override;

    int run();

    // LuaHost
    Options& options() override { return opt_; }
    void options_changed() override;
    Camera& camera() override { return cam_; }
    void camera_reset() override;
    CameraState camera_home() override { return home_pose(); }
    Vec3 cursor_world() override { return scene_.cursor_pos; }
    void nvim_command(const std::string& cmd) override;
    void nvim_input(const std::string& keys) override;
    void nvim_exec_lua(const std::string& code) override;
    void message(const std::string& msg, bool error) override;
    void request_redraw() override { needs_draw_ = true; }
    bool record_start(const RecordOptions& opts, std::string& error) override;
    bool record_stop(std::string& error) override;
    bool recording() override { return recorder_.active(); }
    void quit() override;

    // GLFW callbacks
    void on_key(int key, int scancode, int action, int mods);
    void on_char(unsigned codepoint);
    void on_mouse_button(int button, int action, int mods);
    void on_cursor_pos(double x, double y);
    void on_scroll(double dx, double dy);
    void on_framebuffer_size(int w, int h);

private:
    bool init_window();
    void attach();
    void handle_notification(const std::string& method, const mp::Value& params);
    void pump_nvim();
    void wait_for_redraw(double timeout);
    void capture_frame();
    void rebuild_scene();
    void draw();
    bool save_screenshot(const std::string& path);
    void update_grid_size();
    CameraState home_pose() const;
    bool camera_at_home() const;
    float pixel_size() const;  // pixels per line unit, including content scale
    bool cursor_blink_visible(double now, double& next_change) const;

    struct Hit {
        int grid = -1, row = 0, col = 0;
    };
    Hit pick(double x, double y) const;
    void send_mouse(const std::string& button, const std::string& action, int mods, const Hit& h);

    AppConfig cfg_;
    Options opt_;
    Camera cam_;
    UiState ui_;
    FontSet font_;
    Renderer renderer_;
    Scene scene_;
    Nvim nvim_;
    std::unique_ptr<LuaEngine> lua_;
    GLFWwindow* win_ = nullptr;

    bool attached_ = false;
    std::vector<std::pair<std::string, bool>> pending_messages_;
    int cols_ = 80, rows_ = 24;
    int fb_w_ = 0, fb_h_ = 0;
    std::string loaded_font_;
    bool font_loaded_ = false;
    std::set<std::string> seen_options_;
    float loaded_depth_ = -1;

    bool needs_scene_ = true;
    bool needs_draw_ = true;
    uint64_t last_generation_ = 0;
    int last_cursor_[3] = {-1, -1, -1};
    double cursor_moved_at_ = 0;
    double first_flush_at_ = 0;
    uint64_t flush_count_ = 0;

    Recorder recorder_;
    bool input_sent_ = false;      // keys were sent to Nvim during this frame
    double sim_time_ = 0;          // fixed-step clock used while recording
    std::vector<uint8_t> frame_buf_;

    int mods_ = 0;
    double mouse_x_ = 0, mouse_y_ = 0;
    int drag_button_ = -1;        // GLFW button currently held
    bool drag_camera_ = false;    // the held button is moving the camera
    double press_x_ = 0, press_y_ = 0;
    int press_mods_ = 0;
    Hit last_hit_;
};
