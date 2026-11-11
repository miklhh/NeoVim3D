// End-to-end test: drive a real `nvim --embed` through the RPC layer and check
// that the UI state ends up showing what we typed. Skipped (exit 77) when nvim
// cannot be started.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "nvim.hpp"
#include "ui_state.hpp"

namespace {

std::mutex mu;
std::condition_variable cv;
bool woken = false;

void wake() {
    std::lock_guard lk(mu);
    woken = true;
    cv.notify_one();
}

bool contains(const UiState& ui, const std::string& needle) {
    for (auto& [id, g] : ui.grids)
        for (int r = 0; r < g.height; ++r) {
            std::string row;
            for (int c = 0; c < g.width; ++c) row += g.at(r, c).text;
            if (row.find(needle) != std::string::npos) return true;
        }
    return false;
}

}  // namespace

int main() {
    const char* path = std::getenv("NEOVIM3D_NVIM");
    Nvim nvim;
    if (!nvim.start(path && *path ? path : "nvim", {"--clean", "-n"}, wake)) {
        std::printf("nvim not available, skipping\n");
        return 77;
    }
    UiState ui;
    std::vector<std::string> notifications;
    int64_t chan = -1;
    bool attached = false;

    nvim.request("nvim_get_api_info", {}, [&](const mp::Value&, const mp::Value& r) {
        chan = r[0].as_int();
    });
    nvim.request("nvim_ui_attach",
                 {40, 10, mp::Map{{"rgb", true}, {"ext_linegrid", true}, {"ext_multigrid", true}}},
                 [&](const mp::Value& e, const mp::Value&) { attached = e.is_nil(); });

    auto pump_until = [&](auto pred) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!pred()) {
            if (std::chrono::steady_clock::now() > deadline || nvim.exited()) return false;
            {
                std::unique_lock lk(mu);
                cv.wait_for(lk, std::chrono::milliseconds(50), [] { return woken; });
                woken = false;
            }
            nvim.dispatch(
                [&](const std::string& m, const mp::Value& p) {
                    if (m == "redraw") ui.handle_redraw(p);
                    else notifications.push_back(m + ":" + p[0].as_str());
                },
                [](const std::string&, const mp::Value&, mp::Value&) { return mp::Value(); });
        }
        return true;
    };

    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what);
        if (!ok) ++failures;
    };

    check(pump_until([&] { return attached && chan > 0; }), "attach");
    nvim.notify("nvim_input", {"ihello from neovim3d<Esc>"});
    check(pump_until([&] { return ui.flushed && contains(ui, "hello from neovim3d"); }),
          "typed text reaches the grid");
    check(ui.grids.size() >= 2, "multigrid: window has its own grid");

    nvim.request("nvim_exec_lua",
                 {"vim.rpcnotify(..., 'nv3d_call', 'camera.zoom', {2})", mp::Array{chan}});
    check(pump_until([&] {
              for (auto& n : notifications)
                  if (n == "nv3d_call:camera.zoom") return true;
              return false;
          }),
          "rpcnotify from nvim reaches the frontend");

    nvim.notify("nvim_command", {"qall!"});
    check(pump_until([&] { return nvim.exited(); }), "nvim exits on :qall!");
    return failures == 0 ? 0 : 1;
}
