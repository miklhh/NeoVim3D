#include "ui_state.hpp"

#include <algorithm>

void Grid::resize(int w, int h) {
    std::vector<Cell> nc(size_t(w) * h);
    for (int r = 0; r < std::min(h, height); ++r)
        for (int c = 0; c < std::min(w, width); ++c) nc[size_t(r) * w + c] = at(r, c);
    cells.swap(nc);
    width = w;
    height = h;
}

void Grid::clear() { std::fill(cells.begin(), cells.end(), Cell{}); }

Grid& UiState::grid(int id) {
    auto& g = grids[id];
    g.id = id;
    if (id == 1) g.visible = true;
    return g;
}

const HlAttr& UiState::attr(int hl_id) const {
    static const HlAttr kDefault;
    auto it = hl_.find(hl_id);
    return it == hl_.end() ? kDefault : it->second;
}

ResolvedColors UiState::colors(int hl_id) const {
    const HlAttr& a = attr(hl_id);
    uint32_t fg = a.fg >= 0 ? uint32_t(a.fg) : default_fg;
    uint32_t bg = a.bg >= 0 ? uint32_t(a.bg) : default_bg;
    uint32_t sp = a.sp >= 0 ? uint32_t(a.sp) : (a.fg >= 0 ? uint32_t(a.fg) : default_sp);
    if (a.reverse) std::swap(fg, bg);
    return {fg, bg, sp};
}

void UiState::handle_redraw(const mp::Value& params) {
    for (auto& batch : params.as_array()) {
        auto& items = batch.as_array();
        if (items.empty()) continue;
        const std::string& name = items[0].as_str();
        for (size_t i = 1; i < items.size(); ++i) event(name, items[i]);
    }
}

void UiState::event(const std::string& name, const mp::Value& a) {
    if (name == "grid_line") {
        Grid& g = grid(int(a[0].as_int()));
        int row = int(a[1].as_int());
        int col = int(a[2].as_int());
        if (row < 0 || row >= g.height) return;
        int hl = 0;
        for (auto& cell : a[3].as_array()) {
            const std::string& text = cell[0].as_str();
            if (cell.size() > 1) hl = int(cell[1].as_int());
            int repeat = cell.size() > 2 ? int(cell[2].as_int()) : 1;
            for (int k = 0; k < repeat && col < g.width; ++k, ++col) {
                Cell& c = g.at(row, col);
                c.text = text;
                c.hl = hl;
            }
        }
        ++generation;
    } else if (name == "grid_scroll") {
        Grid& g = grid(int(a[0].as_int()));
        int top = int(a[1].as_int()), bot = int(a[2].as_int());
        int left = int(a[3].as_int()), right = int(a[4].as_int());
        int rows = int(a[5].as_int());
        bot = std::min(bot, g.height);
        right = std::min(right, g.width);
        if (rows > 0) {
            for (int r = top; r < bot - rows; ++r)
                for (int c = left; c < right; ++c) g.at(r, c) = g.at(r + rows, c);
        } else if (rows < 0) {
            for (int r = bot - 1; r >= top - rows; --r)
                for (int c = left; c < right; ++c) g.at(r, c) = g.at(r + rows, c);
        }
        ++generation;
    } else if (name == "grid_clear") {
        grid(int(a[0].as_int())).clear();
        ++generation;
    } else if (name == "grid_resize") {
        Grid& g = grid(int(a[0].as_int()));
        g.resize(int(a[1].as_int()), int(a[2].as_int()));
        ++generation;
    } else if (name == "grid_destroy") {
        grids.erase(int(a[0].as_int()));
        ++generation;
    } else if (name == "grid_cursor_goto") {
        cursor_grid = int(a[0].as_int());
        cursor_row = int(a[1].as_int());
        cursor_col = int(a[2].as_int());
        ++generation;
    } else if (name == "win_pos") {
        Grid& g = grid(int(a[0].as_int()));
        g.row = double(a[2].as_int());
        g.col = double(a[3].as_int());
        g.visible = true;
        g.is_float = false;
        g.zindex = 0;
        ++generation;
    } else if (name == "win_float_pos") {
        Grid& g = grid(int(a[0].as_int()));
        g.is_float = true;
        g.visible = true;
        g.zindex = int(a[7].as_int(50));
        g.compindex = a.size() > 8 ? int(a[8].as_int()) : ++next_compindex_;
        if (a.size() > 10) {
            // Nvim >= 0.10 tells us where it put the float on the screen.
            g.row = double(a[9].as_int());
            g.col = double(a[10].as_int());
        } else {
            const std::string& anchor = a[2].as_str();
            int anchor_grid = int(a[3].as_int());
            double r = a[4].as_double(), c = a[5].as_double();
            if (auto it = grids.find(anchor_grid); it != grids.end() && anchor_grid != 1) {
                r += it->second.row;
                c += it->second.col;
            }
            if (anchor.size() == 2) {
                if (anchor[0] == 'S') r -= g.height;
                if (anchor[1] == 'E') c -= g.width;
            }
            g.row = std::max(0.0, r);
            g.col = std::max(0.0, c);
        }
        ++generation;
    } else if (name == "win_external_pos") {
        Grid& g = grid(int(a[0].as_int()));
        g.visible = true;
        g.is_float = true;
        g.zindex = 100;
        ++generation;
    } else if (name == "win_hide" || name == "win_close") {
        if (auto it = grids.find(int(a[0].as_int())); it != grids.end()) it->second.visible = false;
        ++generation;
    } else if (name == "msg_set_pos") {
        Grid& g = grid(int(a[0].as_int()));
        g.row = double(a[1].as_int());
        g.col = 0;
        g.visible = true;
        g.is_msg = true;
        g.is_float = true;
        g.zindex = a.size() > 4 ? int(a[4].as_int(200)) : 200;
        g.compindex = a.size() > 5 ? int(a[5].as_int()) : ++next_compindex_;
        ++generation;
    } else if (name == "hl_attr_define") {
        int id = int(a[0].as_int());
        const mp::Value& r = a[1];
        HlAttr h;
        h.fg = r["foreground"].is_nil() ? -1 : r["foreground"].as_int();
        h.bg = r["background"].is_nil() ? -1 : r["background"].as_int();
        h.sp = r["special"].is_nil() ? -1 : r["special"].as_int();
        h.reverse = r["reverse"].as_bool();
        h.bold = r["bold"].as_bool();
        h.italic = r["italic"].as_bool();
        h.strikethrough = r["strikethrough"].as_bool();
        h.underline = r["underline"].as_bool();
        h.undercurl = r["undercurl"].as_bool();
        h.underdouble = r["underdouble"].as_bool();
        h.underdotted = r["underdotted"].as_bool();
        h.underdashed = r["underdashed"].as_bool();
        h.blend = int(r["blend"].as_int());
        hl_[id] = h;
        ++generation;
    } else if (name == "default_colors_set") {
        int64_t fg = a[0].as_int(-1), bg = a[1].as_int(-1), sp = a[2].as_int(-1);
        if (fg >= 0) default_fg = uint32_t(fg);
        if (bg >= 0) default_bg = uint32_t(bg);
        if (sp >= 0) default_sp = uint32_t(sp);
        ++generation;
    } else if (name == "mode_info_set") {
        cursor_style_enabled = a[0].as_bool(true);
        modes.clear();
        for (auto& m : a[1].as_array()) {
            ModeInfo mi;
            const std::string& shape = m["cursor_shape"].as_str();
            if (shape == "horizontal") mi.shape = ModeInfo::Horizontal;
            else if (shape == "vertical") mi.shape = ModeInfo::Vertical;
            mi.cell_percentage = int(m["cell_percentage"].as_int(100));
            if (mi.cell_percentage <= 0) mi.cell_percentage = 100;
            mi.attr_id = int(m["attr_id"].as_int());
            mi.blinkwait = int(m["blinkwait"].as_int());
            mi.blinkon = int(m["blinkon"].as_int());
            mi.blinkoff = int(m["blinkoff"].as_int());
            mi.name = m["name"].as_str();
            modes.push_back(mi);
        }
        ++generation;
    } else if (name == "mode_change") {
        mode_name = a[0].as_str();
        mode_idx = int(a[1].as_int());
        ++generation;
    } else if (name == "busy_start") {
        busy = true;
        ++generation;
    } else if (name == "busy_stop") {
        busy = false;
        ++generation;
    } else if (name == "flush") {
        flushed = true;
    } else if (name == "set_title") {
        if (on_title) on_title(a[0].as_str());
    } else if (name == "option_set") {
        if (on_option) on_option(a[0].as_str(), a[1]);
    } else if (name == "bell" || name == "visual_bell") {
        if (on_bell) on_bell();
    }
    // Ignored: set_icon, mouse_on/off, suspend, update_menu, win_viewport,
    // win_extmark, win_viewport_margins, hl_group_set, chdir.
}
