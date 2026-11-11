// Font loading (fontconfig + FreeType) and conversion of glyph outlines into
// extruded 3D triangle meshes.
//
// Mesh coordinates are in "line units": one unit is the font's natural line
// height (ascender - descender). The origin is the top-left of the cell, X to
// the right, Y up (so glyphs live at negative Y), front face at Z = 0 and the
// back face at Z = -depth.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct FT_LibraryRec_;
struct FT_FaceRec_;

struct GlyphVertex {
    float px, py, pz;
    float nx, ny, nz;
};

struct GlyphMesh {
    uint32_t first_index = 0;
    uint32_t index_count = 0;  // 0 => nothing to draw (space, missing glyph)
    int32_t base_vertex = 0;
};

// Pure geometry: triangulate + extrude a set of closed contours (Y up).
// `outer_is_ccw` gives the winding of filled (outer) contours in the input;
// contours with the opposite winding are holes. Indices are appended relative
// to the size of `verts` on entry.
struct Contour {
    std::vector<float> xy;  // x0,y0,x1,y1,...
};
void extrude_contours(const std::vector<Contour>& contours, bool outer_is_ccw, float depth,
                      std::vector<GlyphVertex>& verts, std::vector<uint32_t>& indices);

class FontSet {
public:
    FontSet();
    ~FontSet();
    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;

    // Load a font family (fontconfig pattern, e.g. "JetBrains Mono").
    // Empty/unavailable families fall back to "monospace".
    bool load(const std::string& family);
    const std::string& family() const { return family_; }
    const std::string& primary_path() const { return primary_path_; }
    // A message about the last load() (e.g. a missing font); cleared on read.
    std::string take_warning() { return std::exchange(warning_, {}); }

    void set_depth(float depth);
    float depth() const { return depth_; }

    // Width of one cell in line units (height is 1).
    float cell_width() const { return cell_width_; }
    // Height of one em in line units (a line is usually ~1.2 em).
    float em_size() const { return primary_upem_ * line_scale_; }
    // Distance from the cell top to the baseline in line units.
    float ascent() const { return ascent_; }
    float underline_pos() const { return underline_pos_; }
    float underline_thickness() const { return underline_thickness_; }

    // Get (building and caching if needed) the mesh for one grid cell's text.
    // `cells` is 1 or 2 for double-width characters.
    const GlyphMesh& glyph(const std::string& text, bool bold, bool italic, int cells);

    // Accumulated geometry for all glyphs; `dirty` is set when it grew.
    std::vector<GlyphVertex> vertices;
    std::vector<uint32_t> indices;
    bool dirty = true;

private:
    struct Face {
        FT_FaceRec_* face = nullptr;
        std::string path;
        bool synthetic_bold = false;
        bool synthetic_italic = false;
    };
    int open_face(const std::string& path, int index, bool want_bold, bool want_italic,
                  int weight, int slant);
    int face_for(uint32_t cp, int style);
    void clear_faces();
    GlyphMesh build(int face_idx, uint32_t glyph_index, int cells);

    FT_LibraryRec_* ft_ = nullptr;
    std::string family_, primary_path_, warning_;
    int style_faces_[4] = {-1, -1, -1, -1};  // regular, bold, italic, bold-italic
    std::vector<Face> faces_;
    std::unordered_map<uint64_t, int> fallback_cache_;  // (cp<<2|style) -> face
    std::unordered_map<std::string, GlyphMesh> cache_;

    float depth_ = 0.12f;
    float line_scale_ = 1;  // font units of primary face -> line units
    float primary_upem_ = 1000;
    float cell_width_ = 0.5f, ascent_ = 0.8f;
    float underline_pos_ = 0.9f, underline_thickness_ = 0.05f;
};

// Decode the first UTF-8 code point of `s`.
uint32_t utf8_first(const std::string& s);
