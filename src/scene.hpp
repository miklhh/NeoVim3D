// Turns the UI state into renderable instances laid out in 3D space.
//
// World layout: one cell is `cell_w` x `cell_h` units, rows grow towards -Y,
// columns towards +X. The base grid (grid 1) sits at Z = 0 with its top-left
// corner at the origin; floating windows are stacked towards the viewer.
#pragma once

#include <cstdint>
#include <vector>

#include "font.hpp"
#include "options.hpp"
#include "ui_state.hpp"

struct BoxInstance {
    float x, y, z;     // minimum corner
    float sx, sy, sz;  // size
    float r, g, b, a;
};

struct GlyphInstance {
    float x, y, z;
    float r, g, b, a;
};

struct GlyphBatch {
    GlyphMesh mesh;
    uint32_t first_instance = 0, count = 0;
};

struct GridPlacement {
    int grid;
    float x, y, z;  // world position of the top-left corner of the front face
    int rows, cols;
};

struct Scene {
    std::vector<BoxInstance> boxes;
    std::vector<GlyphInstance> glyphs;
    std::vector<GlyphBatch> batches;
    std::vector<GridPlacement> placements;  // back to front
    float cell_w = 0.5f, cell_h = 1.0f;
    Vec3 cursor_pos;  // world position of the cursor cell's centre
};

Scene build_scene(const UiState& ui, FontSet& font, const Options& opt, bool cursor_visible);
