// Mirror of Neovim's UI state, built from `redraw` notifications
// (ext_linegrid + ext_multigrid).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "msgpack.hpp"

struct HlAttr {
    int64_t fg = -1, bg = -1, sp = -1;  // -1 = use default colour
    bool reverse = false, bold = false, italic = false, strikethrough = false;
    bool underline = false, undercurl = false, underdouble = false, underdotted = false,
         underdashed = false;
    int blend = 0;
};

struct Cell {
    std::string text = " ";
    int hl = 0;
};

struct Grid {
    int id = 0;
    int width = 0, height = 0;
    std::vector<Cell> cells;

    // Placement on the outer screen, in cells.
    bool visible = false;
    bool is_float = false;
    bool is_msg = false;
    double row = 0, col = 0;
    int zindex = 0;
    int compindex = 0;

    Cell& at(int r, int c) { return cells[size_t(r) * width + c]; }
    const Cell& at(int r, int c) const { return cells[size_t(r) * width + c]; }
    void resize(int w, int h);
    void clear();
};

struct ModeInfo {
    enum Shape { Block, Horizontal, Vertical } shape = Block;
    int cell_percentage = 100;
    int attr_id = 0;
    int blinkwait = 0, blinkon = 0, blinkoff = 0;
    std::string name;
};

struct ResolvedColors {
    uint32_t fg, bg, sp;
};

class UiState {
public:
    // Apply the args of one "redraw" notification.
    void handle_redraw(const mp::Value& params);

    // Resolve the effective colours of a highlight id (handles reverse/defaults).
    ResolvedColors colors(int hl_id) const;
    const HlAttr& attr(int hl_id) const;

    std::map<int, Grid> grids;
    uint32_t default_fg = 0xd0d0d0, default_bg = 0x1e1e1e, default_sp = 0xff0000;

    int cursor_grid = 1, cursor_row = 0, cursor_col = 0;
    std::vector<ModeInfo> modes;
    int mode_idx = 0;
    std::string mode_name;
    bool cursor_style_enabled = true;
    bool busy = false;

    // Set when a "flush" event arrives; the frontend clears it after redrawing.
    bool flushed = false;
    // Bumped on every structural/contents change.
    uint64_t generation = 0;

    std::function<void(const std::string&)> on_title;
    std::function<void(const std::string&, const mp::Value&)> on_option;
    std::function<void()> on_bell;

private:
    void event(const std::string& name, const mp::Value& args);
    Grid& grid(int id);

    std::unordered_map<int, HlAttr> hl_;
    int next_compindex_ = 0;
};
