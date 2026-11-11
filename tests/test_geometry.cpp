#include <cmath>
#include <cstdio>

#include "camera.hpp"
#include "check.hpp"
#include "font.hpp"

namespace {

Contour square(float lo, float hi, bool ccw) {
    Contour c;
    if (ccw)
        c.xy = {lo, lo, hi, lo, hi, hi, lo, hi};
    else
        c.xy = {lo, lo, lo, hi, hi, hi, hi, lo};
    return c;
}

void check_square_with_hole(bool outer_ccw) {
    // Outer and hole always have opposite windings.
    std::vector<Contour> cs{square(0, 1, outer_ccw), square(0.25f, 0.75f, !outer_ccw)};
    std::vector<GlyphVertex> v;
    std::vector<uint32_t> idx;
    extrude_contours(cs, outer_ccw, 0.1f, v, idx);
    // 8 cap triangles front + 8 back + 8 wall quads (16 triangles).
    CHECK_EQ(idx.size(), size_t(32 * 3));
    for (uint32_t i : idx) CHECK(i < v.size());

    int walls = 0;
    for (auto& p : v) {
        if (p.nz != 0) continue;
        ++walls;
        float dx = p.px - 0.5f, dy = p.py - 0.5f;
        bool on_outer = std::abs(dx) > 0.4f || std::abs(dy) > 0.4f;
        float d = p.nx * dx + p.ny * dy;
        // Walls face away from the solid: outward on the rim, inward in the hole.
        CHECK(on_outer ? d > 0 : d < 0);
    }
    CHECK_EQ(walls, 32);
    // The front cap sits at z = 0 and the back at -depth.
    bool front = false, back = false;
    for (auto& p : v) {
        if (p.nz > 0.5f) front = front || p.pz == 0.0f;
        if (p.nz < -0.5f) back = back || std::abs(p.pz + 0.1f) < 1e-6f;
    }
    CHECK(front && back);
}

}  // namespace

TEST(geometry_extrude_ccw_outer) { check_square_with_hole(true); }
TEST(geometry_extrude_cw_outer) { check_square_with_hole(false); }

TEST(geometry_flat_when_no_depth) {
    std::vector<Contour> cs{square(0, 1, true)};
    std::vector<GlyphVertex> v;
    std::vector<uint32_t> idx;
    extrude_contours(cs, true, 0.0f, v, idx);
    CHECK_EQ(idx.size(), size_t(6));  // just the front cap
}

TEST(font_loads_and_builds_meshes) {
    FontSet font;
    if (!font.load("monospace")) {
        std::printf("  (no monospace font available, skipping)\n");
        return;
    }
    CHECK(font.cell_width() > 0.2f && font.cell_width() < 1.0f);
    CHECK(font.ascent() > 0.5f && font.ascent() < 1.0f);
    const GlyphMesh& o = font.glyph("o", false, false, 1);
    CHECK(o.index_count > 0);
    CHECK_EQ(font.glyph(" ", false, false, 1).index_count, uint32_t(0));
    // Cached: same mesh returned.
    CHECK_EQ(font.glyph("o", false, false, 1).first_index, o.first_index);
    // All of 'o' lies inside its cell horizontally and below the cell top.
    for (uint32_t i = o.first_index; i < o.first_index + o.index_count; ++i) {
        const GlyphVertex& p = font.vertices[size_t(o.base_vertex) + font.indices[i]];
        CHECK(p.px >= -0.05f && p.px <= font.cell_width() + 0.05f);
        CHECK(p.py <= 0.0f && p.py >= -1.0f);
    }
    // Bold/italic variants are separate meshes.
    CHECK(font.glyph("o", true, true, 1).first_index != o.first_index);
}

TEST(font_missing_family_falls_back_to_monospace) {
    FontSet font;
    if (!font.load("monospace")) return;
    std::string mono = font.primary_path();
    CHECK(font.take_warning().empty());
    CHECK(font.load("No Such Font Family 12345"));
    CHECK(!font.take_warning().empty());
    CHECK_EQ(font.primary_path(), mono);
}

TEST(camera_ray_hits_target) {
    Camera cam;
    cam.goal.target = Vec3(3, -2, 0);
    cam.goal.distance = 10;
    cam.goal.yaw = 0.3f;
    cam.goal.pitch = 0.2f;
    cam.snap();
    Vec3 o, d;
    cam.ray(0, 0, 1.5f, o, d);
    // The centre ray passes through the target.
    float t = (0 - o.z) / d.z;
    Vec3 h = o + d * t;
    CHECK(std::abs(h.x - 3) < 1e-3f);
    CHECK(std::abs(h.y + 2) < 1e-3f);
}

TEST(camera_smoothing_converges) {
    Camera cam;
    cam.goal.yaw = 1.0f;
    for (int i = 0; i < 300 && cam.update(1.0f / 60); ++i) {
    }
    CHECK(!cam.moving());
    CHECK_EQ(cam.current.yaw, 1.0f);
}
