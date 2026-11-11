#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app.hpp"

#ifndef NEOVIM3D_VERSION
#define NEOVIM3D_VERSION "dev"
#endif

namespace {

void usage() {
    std::printf(
        "Usage: neovim3d [options] [--] [nvim arguments / files...]\n"
        "\n"
        "Neovim, rendered in 3D. All arguments not listed below are passed to nvim.\n"
        "\n"
        "Options:\n"
        "  --nvim <path>     Neovim executable to run (default: nvim, or $NEOVIM3D_NVIM)\n"
        "  --config <file>   Frontend Lua config (default: ~/.config/neovim3d/init.lua)\n"
        "  --clean-3d        Do not load the frontend Lua config\n"
        "  --screenshot-3d <file.png>  Render, save a screenshot and exit\n"
        "  --size-3d <WxH>   Initial window size (default 1280x800)\n"
        "  --help-3d         Show this help\n"
        "  --version-3d      Show the NeoVim3D version\n"
        "\n"
        "Mouse: right-drag orbits, middle-drag pans, Ctrl+wheel zooms, Alt+left-drag orbits.\n"
        "Keys:  Ctrl+Shift+arrows orbit, Ctrl+Shift+PageUp/PageDown zoom,\n"
        "       Ctrl+Shift+Home resets the camera, Ctrl+Shift+End toggles cursor follow,\n"
        "       Ctrl+Shift+C toggles cinema mode, Ctrl+Shift+N skips to the next shot.\n");
}

}  // namespace

int main(int argc, char** argv) {
    AppConfig cfg;
    if (const char* env = std::getenv("NEOVIM3D_NVIM"); env && *env) cfg.nvim_path = env;
    bool passthrough = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (passthrough) {
            cfg.nvim_args.push_back(a);
        } else if (a == "--") {
            passthrough = true;
            cfg.nvim_args.push_back(a);
        } else if (a == "--nvim" && i + 1 < argc) {
            cfg.nvim_path = argv[++i];
        } else if (a == "--config" && i + 1 < argc) {
            cfg.config_path = argv[++i];
        } else if (a == "--screenshot-3d" && i + 1 < argc) {
            cfg.screenshot_path = argv[++i];
        } else if (a == "--size-3d" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &cfg.window_width, &cfg.window_height) != 2 ||
                cfg.window_width < 100 || cfg.window_height < 100) {
                std::fprintf(stderr, "neovim3d: --size-3d expects WIDTHxHEIGHT, e.g. 1280x720\n");
                return 2;
            }
        } else if (a == "--clean-3d") {
            cfg.load_config = false;
        } else if (a == "--help-3d" || a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a == "--version-3d") {
            std::printf("NeoVim3D %s\n", NEOVIM3D_VERSION);
            return 0;
        } else {
            cfg.nvim_args.push_back(a);
        }
    }
    App app(std::move(cfg));
    return app.run();
}
