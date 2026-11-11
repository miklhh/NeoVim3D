#include "check.hpp"
#include "ui_state.hpp"

namespace {

// Build a redraw batch: [[name, args...]].
mp::Value batch(const std::string& name, std::initializer_list<mp::Value> calls) {
    mp::Array ev{name};
    for (auto& c : calls) ev.push_back(c);
    return mp::Array{mp::Value(ev)};
}

std::string row_text(const Grid& g, int r) {
    std::string s;
    for (int c = 0; c < g.width; ++c) s += g.at(r, c).text;
    return s;
}

}  // namespace

TEST(ui_grid_line_and_repeat) {
    UiState ui;
    ui.handle_redraw(batch("grid_resize", {mp::Array{1, 6, 3}}));
    // cells: ["a", 5], ["b"], [" ", 7, 3]  -> hl carries over to "b"
    ui.handle_redraw(batch(
        "grid_line",
        {mp::Array{1, 1, 0, mp::Array{mp::Array{"a", 5}, mp::Array{"b"}, mp::Array{"-", 7, 3}},
                   false}}));
    const Grid& g = ui.grids.at(1);
    CHECK_EQ(row_text(g, 1), std::string("ab--- "));
    CHECK_EQ(g.at(1, 1).hl, 5);
    CHECK_EQ(g.at(1, 2).hl, 7);
    CHECK_EQ(g.at(1, 4).hl, 7);
}

TEST(ui_grid_scroll) {
    UiState ui;
    ui.handle_redraw(batch("grid_resize", {mp::Array{1, 2, 4}}));
    for (int r = 0; r < 4; ++r)
        ui.handle_redraw(batch(
            "grid_line",
            {mp::Array{1, r, 0, mp::Array{mp::Array{std::string(1, char('0' + r)), 0, 2}}, false}}));
    // Scroll region rows [0,4) up by one.
    ui.handle_redraw(batch("grid_scroll", {mp::Array{1, 0, 4, 0, 2, 1, 0}}));
    const Grid& g = ui.grids.at(1);
    CHECK_EQ(row_text(g, 0), std::string("11"));
    CHECK_EQ(row_text(g, 2), std::string("33"));
    // And back down by two.
    ui.handle_redraw(batch("grid_scroll", {mp::Array{1, 0, 4, 0, 2, -2, 0}}));
    CHECK_EQ(row_text(g, 2), std::string("11"));
    CHECK_EQ(row_text(g, 3), std::string("22"));
}

TEST(ui_highlights_and_reverse) {
    UiState ui;
    ui.handle_redraw(batch("default_colors_set", {mp::Array{0xffffff, 0x000000, 0xff0000, 0, 0}}));
    ui.handle_redraw(batch(
        "hl_attr_define",
        {mp::Array{3, mp::Map{{"foreground", 0x112233}, {"reverse", true}}, mp::Map{}, mp::Array{}}}));
    ResolvedColors c = ui.colors(3);
    CHECK_EQ(c.fg, uint32_t(0x000000));
    CHECK_EQ(c.bg, uint32_t(0x112233));
    CHECK_EQ(ui.colors(0).fg, uint32_t(0xffffff));
}

TEST(ui_multigrid_windows) {
    UiState ui;
    ui.handle_redraw(batch("grid_resize", {mp::Array{1, 80, 24}}));
    ui.handle_redraw(batch("grid_resize", {mp::Array{2, 80, 22}}));
    ui.handle_redraw(batch("win_pos", {mp::Array{2, mp::Ext{1, "\x01"}, 1, 0, 80, 22}}));
    ui.handle_redraw(batch("grid_resize", {mp::Array{4, 10, 3}}));
    ui.handle_redraw(batch(
        "win_float_pos",
        {mp::Array{4, mp::Ext{1, "\x02"}, "NW", 2, 5.0, 7.0, true, 50, 3, 6, 7}}));
    CHECK(ui.grids.at(2).visible);
    CHECK(!ui.grids.at(2).is_float);
    CHECK_EQ(ui.grids.at(2).row, 1.0);
    CHECK(ui.grids.at(4).is_float);
    CHECK_EQ(ui.grids.at(4).row, 6.0);
    CHECK_EQ(ui.grids.at(4).col, 7.0);
    CHECK_EQ(ui.grids.at(4).zindex, 50);
    ui.handle_redraw(batch("win_hide", {mp::Array{4}}));
    CHECK(!ui.grids.at(4).visible);
    ui.handle_redraw(batch("grid_destroy", {mp::Array{4}}));
    CHECK(ui.grids.count(4) == 0);
}

TEST(ui_mode_info_and_flush) {
    UiState ui;
    mp::Map normal{{"cursor_shape", "block"}, {"cell_percentage", 0}, {"name", "normal"}};
    mp::Map insert{{"cursor_shape", "vertical"}, {"cell_percentage", 25}, {"attr_id", 9},
                   {"name", "insert"}};
    ui.handle_redraw(batch("mode_info_set", {mp::Array{true, mp::Array{normal, insert}}}));
    ui.handle_redraw(batch("mode_change", {mp::Array{"insert", 1}}));
    CHECK_EQ(ui.modes.size(), size_t(2));
    CHECK(ui.modes[1].shape == ModeInfo::Vertical);
    CHECK_EQ(ui.modes[0].cell_percentage, 100);
    CHECK_EQ(ui.mode_idx, 1);
    CHECK(!ui.flushed);
    ui.handle_redraw(batch("flush", {mp::Array{}}));
    CHECK(ui.flushed);
}
