// OpenGL 3.3 renderer for a Scene.
#pragma once

#include <cstdint>

#include "camera.hpp"
#include "font.hpp"
#include "options.hpp"
#include "scene.hpp"

class Renderer {
public:
    bool init();
    void destroy();

    // Upload glyph geometry (if it changed) and the scene's instances.
    void upload(FontSet& font, const Scene& scene);
    void draw(const Scene& scene, const Camera& cam, const Options& opt, int fb_width,
              int fb_height, uint32_t clear_color);

private:
    unsigned compile(const char* vs, const char* fs);

    unsigned glyph_prog_ = 0, box_prog_ = 0;
    unsigned glyph_vao_ = 0, glyph_vbo_ = 0, glyph_ebo_ = 0, glyph_inst_ = 0;
    unsigned box_vao_ = 0, box_vbo_ = 0, box_inst_ = 0;
    size_t glyph_vbo_size_ = 0, glyph_ebo_size_ = 0;
};
