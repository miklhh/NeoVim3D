#include "scene.hpp"

#include <algorithm>
#include <unordered_map>

namespace {

struct Rgb {
    float r, g, b;
};

Rgb rgb(uint32_t c) {
    return {float((c >> 16) & 0xff) / 255.0f, float((c >> 8) & 0xff) / 255.0f,
            float(c & 0xff) / 255.0f};
}

void push_box(Scene& s, float x, float y, float z, float sx, float sy, float sz, uint32_t color,
              float alpha = 1) {
    Rgb c = rgb(color);
    s.boxes.push_back({x, y, z, sx, sy, sz, c.r, c.g, c.b, alpha});
}

// Thicknesses of the background slabs behind the glyphs.
constexpr float kPanelBack = 0.06f, kPanelFront = 0.01f;
constexpr float kCellBack = 0.055f, kCellFront = 0.005f;
constexpr float kCursorBack = 0.075f, kCursorFront = 0.002f;

}  // namespace

Scene build_scene(const UiState& ui, FontSet& font, const Options& opt, bool cursor_visible) {
    Scene s;
    const float cw = font.cell_width();
    const float ch = 1.0f + opt.linespace * font.em_size() / std::max(1.0f, opt.font_size);
    const float text_dy = (ch - 1.0f) / 2;
    const float depth = font.depth();
    s.cell_w = cw;
    s.cell_h = ch;

    // Order grids back to front and assign their Z layer.
    std::vector<const Grid*> base, floats;
    for (auto& [id, g] : ui.grids) {
        if (!g.visible || g.width <= 0 || g.height <= 0) continue;
        (g.is_float ? floats : base).push_back(&g);
    }
    std::stable_sort(base.begin(), base.end(), [](const Grid* a, const Grid* b) {
        return (a->id == 1) > (b->id == 1);
    });
    std::stable_sort(floats.begin(), floats.end(), [](const Grid* a, const Grid* b) {
        return a->zindex != b->zindex ? a->zindex < b->zindex : a->compindex < b->compindex;
    });
    std::vector<std::pair<const Grid*, float>> ordered;
    for (auto* g : base) ordered.push_back({g, g->id == 1 ? 0.0f : opt.window_gap + 0.002f});
    float top = opt.window_gap;
    for (size_t i = 0; i < floats.size(); ++i) {
        top = opt.window_gap + opt.layer_gap * float(i + 1) + 0.002f * float(i + 1);
        ordered.push_back({floats[i], top});
    }

    // Cursor appearance.
    const ModeInfo* mode = ui.mode_idx >= 0 && size_t(ui.mode_idx) < ui.modes.size()
                               ? &ui.modes[size_t(ui.mode_idx)]
                               : nullptr;
    uint32_t cursor_bg = ui.default_fg, cursor_fg = ui.default_bg;
    if (mode && mode->attr_id > 0) {
        ResolvedColors c = ui.colors(mode->attr_id);
        cursor_bg = c.bg;
        cursor_fg = c.fg;
        // A "Cursor" group without colours means "reverse the cell".
        const HlAttr& a = ui.attr(mode->attr_id);
        if (a.fg < 0 && a.bg < 0 && !a.reverse) cursor_bg = ui.default_fg, cursor_fg = ui.default_bg;
    }
    ModeInfo::Shape shape = mode ? mode->shape : ModeInfo::Block;
    float pct = mode ? float(mode->cell_percentage) / 100.0f : 1.0f;

    std::unordered_map<uint64_t, std::vector<GlyphInstance>> by_mesh;
    std::unordered_map<uint64_t, GlyphMesh> meshes;

    for (auto& [gp, z] : ordered) {
        const Grid& g = *gp;
        const float x0 = float(g.col) * cw, y0 = -float(g.row) * ch;
        s.placements.push_back({g.id, x0, y0, z, g.height, g.width});

        if (opt.panels)
            push_box(s, x0, y0 - float(g.height) * ch, z - depth - kPanelBack, float(g.width) * cw,
                     float(g.height) * ch, kPanelBack - kPanelFront, ui.default_bg);

        bool has_cursor = cursor_visible && ui.cursor_grid == g.id && ui.cursor_row >= 0 &&
                          ui.cursor_row < g.height && ui.cursor_col >= 0 &&
                          ui.cursor_col < g.width;

        for (int r = 0; r < g.height; ++r) {
            const float yt = y0 - float(r) * ch;  // top of the row
            // Background runs.
            int run_start = 0;
            uint32_t run_color = 0;
            bool in_run = false;
            auto flush_run = [&](int end) {
                if (in_run)
                    push_box(s, x0 + float(run_start) * cw, yt - ch, z - depth - kCellBack,
                             float(end - run_start) * cw, ch, kCellBack - kCellFront, run_color);
                in_run = false;
            };
            for (int c = 0; c < g.width; ++c) {
                uint32_t bg = ui.colors(g.at(r, c).hl).bg;
                bool needs = bg != ui.default_bg;
                if (in_run && (!needs || bg != run_color)) flush_run(c);
                if (needs && !in_run) in_run = true, run_start = c, run_color = bg;
            }
            flush_run(g.width);

            // Glyphs and decorations.
            for (int c = 0; c < g.width; ++c) {
                const Cell& cell = g.at(r, c);
                const HlAttr& a = ui.attr(cell.hl);
                ResolvedColors col = ui.colors(cell.hl);
                int cells = (c + 1 < g.width && g.at(r, c + 1).text.empty()) ? 2 : 1;
                const float xl = x0 + float(c) * cw;
                const float base_y = yt - text_dy;

                bool under_cursor = has_cursor && shape == ModeInfo::Block && r == ui.cursor_row &&
                                    c == ui.cursor_col;
                uint32_t fg = under_cursor ? cursor_fg : col.fg;

                if (!cell.text.empty() && cell.text != " ") {
                    const GlyphMesh& m = font.glyph(cell.text, a.bold, a.italic, cells);
                    if (m.index_count > 0) {
                        uint64_t key = (uint64_t(m.first_index) << 32) | uint32_t(m.base_vertex);
                        meshes[key] = m;
                        Rgb f = rgb(fg);
                        by_mesh[key].push_back({xl, base_y, z, f.r, f.g, f.b, 1});
                    }
                }

                // Underline family, drawn as thin bars level with the glyphs.
                const float th = font.underline_thickness();
                const float uy = base_y - font.underline_pos() - th / 2;
                const float span = cw * float(cells);
                if (a.underline || a.underdouble)
                    push_box(s, xl, uy, z - depth, span, th, depth, col.sp);
                if (a.underdouble)
                    push_box(s, xl, uy - th * 2, z - depth, span, th, depth, col.sp);
                if (a.undercurl) {
                    for (int k = 0; k < 4; ++k)
                        push_box(s, xl + span * float(k) / 4, uy + (k % 2 ? th : -th), z - depth,
                                 span / 4, th, depth, col.sp);
                }
                if (a.underdotted || a.underdashed) {
                    int n = a.underdotted ? 4 : 2;
                    for (int k = 0; k < n; ++k)
                        push_box(s, xl + span * float(k) / float(n), uy, z - depth,
                                 span / float(n) * 0.5f, th, depth, col.sp);
                }
                if (a.strikethrough)
                    push_box(s, xl, base_y - font.ascent() * 0.68f, z - depth, span, th, depth,
                             col.fg);
            }
        }

        if (has_cursor) {
            const float xl = x0 + float(ui.cursor_col) * cw;
            const float yt = y0 - float(ui.cursor_row) * ch;
            int cells = (ui.cursor_col + 1 < g.width &&
                         g.at(ui.cursor_row, ui.cursor_col + 1).text.empty())
                            ? 2
                            : 1;
            const float span = cw * float(cells);
            s.cursor_pos = Vec3(xl + span / 2, yt - ch / 2, z);
            switch (shape) {
                case ModeInfo::Block:
                    push_box(s, xl, yt - ch, z - depth - kCursorBack, span, ch,
                             kCursorBack - kCursorFront, cursor_bg);
                    break;
                case ModeInfo::Vertical:
                    push_box(s, xl, yt - ch, z - depth - kCursorBack, std::max(0.04f, cw * pct),
                             ch, kCursorBack + depth + 0.02f, cursor_bg);
                    break;
                case ModeInfo::Horizontal:
                    push_box(s, xl, yt - ch, z - depth - kCursorBack, span,
                             std::max(0.04f, ch * pct), kCursorBack + depth + 0.02f, cursor_bg);
                    break;
            }
        }
    }

    for (auto& [key, inst] : by_mesh) {
        GlyphBatch b;
        b.mesh = meshes[key];
        b.first_instance = uint32_t(s.glyphs.size());
        b.count = uint32_t(inst.size());
        s.glyphs.insert(s.glyphs.end(), inst.begin(), inst.end());
        s.batches.push_back(b);
    }
    return s;
}
