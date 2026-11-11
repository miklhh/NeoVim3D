// User-tweakable frontend settings (set from the embedded Lua interpreter).
#pragma once

#include <string>

#include "math3d.hpp"

struct Options {
    std::string font;          // fontconfig family; empty = "monospace"
    float font_size = 16.0f;   // em size in pixels at the home camera position
    float linespace = 0.0f;    // extra pixels between lines
    float depth = 0.12f;       // glyph extrusion depth (line units)
    float layer_gap = 0.6f;    // Z distance between stacked floating windows
    float window_gap = 0.0f;   // Z offset of split windows over the base grid
    float fov = 35.0f;         // vertical field of view, degrees
    float smoothing = 12.0f;   // camera smoothing (0 = immediate)
    float ambient = 0.45f;     // ambient light amount
    float specular = 0.25f;    // specular highlight strength
    Vec3 light_dir{-0.35f, 0.55f, 1.0f};
    float background_dim = 0.55f;  // clear colour = default bg * this
    bool panels = true;            // draw a backing slab behind every grid
    bool cursor_blink = true;
    bool follow_cursor = false;    // keep the camera target on the cursor
    int msaa = 4;
};
