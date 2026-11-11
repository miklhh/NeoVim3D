#include "font.hpp"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <strings.h>

#include "earcut.hpp"

// ---------------------------------------------------------------------------
// Geometry

namespace {

float signed_area(const std::vector<float>& xy) {
    double a = 0;
    size_t n = xy.size() / 2;
    for (size_t i = 0, j = n - 1; i < n; j = i++)
        a += double(xy[2 * j]) * xy[2 * i + 1] - double(xy[2 * i]) * xy[2 * j + 1];
    return float(a / 2);
}

bool point_in_polygon(float x, float y, const std::vector<float>& xy) {
    bool inside = false;
    size_t n = xy.size() / 2;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

void reverse_contour(std::vector<float>& xy) {
    size_t n = xy.size() / 2;
    for (size_t i = 0; i < n / 2; ++i) {
        std::swap(xy[2 * i], xy[2 * (n - 1 - i)]);
        std::swap(xy[2 * i + 1], xy[2 * (n - 1 - i) + 1]);
    }
}

// Side walls of one contour. Contours are oriented with the filled region on
// the left, so the edge's right-hand normal points out of the solid.
void add_walls(const std::vector<float>& xy, float depth, std::vector<GlyphVertex>& verts,
               std::vector<uint32_t>& indices, uint32_t base) {
    size_t n = xy.size() / 2;
    std::vector<std::array<float, 2>> edge_n(n);
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        float dx = xy[2 * j] - xy[2 * i], dy = xy[2 * j + 1] - xy[2 * i + 1];
        float l = std::sqrt(dx * dx + dy * dy);
        edge_n[i] = l > 0 ? std::array<float, 2>{dy / l, -dx / l} : std::array<float, 2>{0, 0};
    }
    // Smooth the normal across a vertex when the outline bends gently.
    const float kSmoothCos = 0.82f;  // ~35 degrees
    auto vertex_normal = [&](size_t v, size_t edge) {
        size_t prev = (v + n - 1) % n;
        auto a = edge_n[prev], b = edge_n[v];
        if (a[0] * b[0] + a[1] * b[1] > kSmoothCos) {
            float x = a[0] + b[0], y = a[1] + b[1];
            float l = std::sqrt(x * x + y * y);
            if (l > 0) return std::array<float, 2>{x / l, y / l};
        }
        return edge_n[edge];
    };
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        auto ni = vertex_normal(i, i), nj = vertex_normal(j, i);
        uint32_t v0 = uint32_t(verts.size()) - base;
        float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
        verts.push_back({xi, yi, 0, ni[0], ni[1], 0});
        verts.push_back({xj, yj, 0, nj[0], nj[1], 0});
        verts.push_back({xj, yj, -depth, nj[0], nj[1], 0});
        verts.push_back({xi, yi, -depth, ni[0], ni[1], 0});
        indices.insert(indices.end(), {v0, v0 + 2, v0 + 1, v0, v0 + 3, v0 + 2});
    }
}

}  // namespace

void extrude_contours(const std::vector<Contour>& input, bool outer_is_ccw, float depth,
                      std::vector<GlyphVertex>& verts, std::vector<uint32_t>& indices) {
    const uint32_t base = uint32_t(verts.size());

    struct C {
        std::vector<float> xy;
        float area;
    };
    std::vector<C> outers, holes;
    for (auto& c : input) {
        if (c.xy.size() < 6) continue;
        float a = signed_area(c.xy);
        if (a == 0) continue;
        bool is_outer = (a > 0) == outer_is_ccw;
        C cc{c.xy, std::abs(a)};
        // Normalise: outers counter-clockwise, holes clockwise.
        if (is_outer && a < 0) reverse_contour(cc.xy);
        if (!is_outer && a > 0) reverse_contour(cc.xy);
        (is_outer ? outers : holes).push_back(std::move(cc));
    }

    std::vector<std::vector<size_t>> holes_of(outers.size());
    for (size_t h = 0; h < holes.size(); ++h) {
        float x = holes[h].xy[0], y = holes[h].xy[1];
        size_t best = SIZE_MAX;
        for (size_t o = 0; o < outers.size(); ++o)
            if (point_in_polygon(x, y, outers[o].xy) &&
                (best == SIZE_MAX || outers[o].area < outers[best].area))
                best = o;
        if (best != SIZE_MAX) holes_of[best].push_back(h);
    }

    using Pt = std::array<float, 2>;
    for (size_t o = 0; o < outers.size(); ++o) {
        std::vector<std::vector<Pt>> poly;
        std::vector<const std::vector<float>*> rings{&outers[o].xy};
        for (size_t h : holes_of[o]) rings.push_back(&holes[h].xy);
        for (auto* r : rings) {
            std::vector<Pt> ring;
            for (size_t i = 0; i + 1 < r->size(); i += 2) ring.push_back({(*r)[i], (*r)[i + 1]});
            poly.push_back(std::move(ring));
        }
        std::vector<uint32_t> tri = mapbox::earcut<uint32_t>(poly);

        // Flattened ring points in earcut's index order.
        std::vector<Pt> pts;
        for (auto& ring : poly) pts.insert(pts.end(), ring.begin(), ring.end());

        uint32_t front = uint32_t(verts.size()) - base;
        for (auto& p : pts) verts.push_back({p[0], p[1], 0, 0, 0, 1});
        for (size_t t = 0; t + 2 < tri.size(); t += 3)
            indices.insert(indices.end(), {front + tri[t], front + tri[t + 1], front + tri[t + 2]});

        if (depth > 0) {
            uint32_t back = uint32_t(verts.size()) - base;
            for (auto& p : pts) verts.push_back({p[0], p[1], -depth, 0, 0, -1});
            for (size_t t = 0; t + 2 < tri.size(); t += 3)
                indices.insert(indices.end(),
                               {back + tri[t], back + tri[t + 2], back + tri[t + 1]});
            for (auto* r : rings) add_walls(*r, depth, verts, indices, base);
        }
    }
}

uint32_t utf8_first(const std::string& s) {
    if (s.empty()) return 0;
    auto u = [&](size_t i) { return i < s.size() ? uint32_t(uint8_t(s[i])) : 0u; };
    uint32_t c = u(0);
    if (c < 0x80) return c;
    if ((c & 0xe0) == 0xc0) return ((c & 0x1f) << 6) | (u(1) & 0x3f);
    if ((c & 0xf0) == 0xe0) return ((c & 0x0f) << 12) | ((u(1) & 0x3f) << 6) | (u(2) & 0x3f);
    if ((c & 0xf8) == 0xf0)
        return ((c & 0x07) << 18) | ((u(1) & 0x3f) << 12) | ((u(2) & 0x3f) << 6) | (u(3) & 0x3f);
    return 0xfffd;
}

// ---------------------------------------------------------------------------
// Outline extraction

namespace {

struct OutlineSink {
    std::vector<Contour> contours;
    float scale;
    float step;  // max segment length (font units) when flattening curves
    float lastx = 0, lasty = 0;

    void point(float x, float y) {
        auto& xy = contours.back().xy;
        float sx = x * scale, sy = y * scale;
        size_t n = xy.size();
        if (n >= 2 && xy[n - 2] == sx && xy[n - 1] == sy) return;
        xy.push_back(sx);
        xy.push_back(sy);
    }
    int steps(float len) const { return std::clamp(int(len / step), 2, 24); }
};

int move_to(const FT_Vector* to, void* user) {
    auto* s = static_cast<OutlineSink*>(user);
    s->contours.emplace_back();
    s->point(float(to->x), float(to->y));
    s->lastx = float(to->x), s->lasty = float(to->y);
    return 0;
}

int line_to(const FT_Vector* to, void* user) {
    auto* s = static_cast<OutlineSink*>(user);
    s->point(float(to->x), float(to->y));
    s->lastx = float(to->x), s->lasty = float(to->y);
    return 0;
}

int conic_to(const FT_Vector* c, const FT_Vector* to, void* user) {
    auto* s = static_cast<OutlineSink*>(user);
    float x0 = s->lastx, y0 = s->lasty, x1 = float(c->x), y1 = float(c->y), x2 = float(to->x),
          y2 = float(to->y);
    int n = s->steps(std::hypot(x1 - x0, y1 - y0) + std::hypot(x2 - x1, y2 - y1));
    for (int i = 1; i <= n; ++i) {
        float t = float(i) / n, mt = 1 - t;
        s->point(mt * mt * x0 + 2 * mt * t * x1 + t * t * x2,
                 mt * mt * y0 + 2 * mt * t * y1 + t * t * y2);
    }
    s->lastx = x2, s->lasty = y2;
    return 0;
}

int cubic_to(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) {
    auto* s = static_cast<OutlineSink*>(user);
    float x0 = s->lastx, y0 = s->lasty;
    float x1 = float(c1->x), y1 = float(c1->y), x2 = float(c2->x), y2 = float(c2->y);
    float x3 = float(to->x), y3 = float(to->y);
    int n = s->steps(std::hypot(x1 - x0, y1 - y0) + std::hypot(x2 - x1, y2 - y1) +
                     std::hypot(x3 - x2, y3 - y2));
    for (int i = 1; i <= n; ++i) {
        float t = float(i) / n, mt = 1 - t;
        float a = mt * mt * mt, b = 3 * mt * mt * t, cc = 3 * mt * t * t, d = t * t * t;
        s->point(a * x0 + b * x1 + cc * x2 + d * x3, a * y0 + b * y1 + cc * y2 + d * y3);
    }
    s->lastx = x3, s->lasty = y3;
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// FontSet

FontSet::FontSet() {
    FT_Init_FreeType(&ft_);
    FcInit();
}

FontSet::~FontSet() {
    clear_faces();
    if (ft_) FT_Done_FreeType(ft_);
}

void FontSet::clear_faces() {
    for (auto& f : faces_)
        if (f.face) FT_Done_Face(f.face);
    faces_.clear();
    fallback_cache_.clear();
    cache_.clear();
    vertices.clear();
    indices.clear();
    dirty = true;
    for (int& s : style_faces_) s = -1;
}

int FontSet::open_face(const std::string& path, int index, bool want_bold, bool want_italic,
                       int weight, int slant) {
    for (size_t i = 0; i < faces_.size(); ++i)
        if (faces_[i].path == path + "#" + std::to_string(index) &&
            faces_[i].synthetic_bold == (want_bold && weight < FC_WEIGHT_DEMIBOLD) &&
            faces_[i].synthetic_italic == (want_italic && slant == FC_SLANT_ROMAN))
            return int(i);
    FT_Face face;
    if (FT_New_Face(ft_, path.c_str(), index, &face) != 0) return -1;
    if (!FT_IS_SCALABLE(face)) {
        FT_Done_Face(face);
        return -1;
    }
    Face f;
    f.face = face;
    f.path = path + "#" + std::to_string(index);
    f.synthetic_bold = want_bold && weight < FC_WEIGHT_DEMIBOLD;
    f.synthetic_italic = want_italic && slant == FC_SLANT_ROMAN;
    faces_.push_back(f);
    return int(faces_.size() - 1);
}

namespace {
struct FcMatchResult {
    std::string path;
    int index = 0, weight = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN;
};

bool is_generic_family(std::string f) {
    for (auto& ch : f) ch = char(std::tolower(uint8_t(ch)));
    return f == "monospace" || f == "mono" || f == "sans" || f == "sans-serif" || f == "serif";
}

bool same_family(const char* a, const char* b) { return strcasecmp(a, b) == 0; }

// Resolve a font with fontconfig. With `cp`, the font must contain that code
// point. With `exact`, the match must actually be of the requested family
// (fontconfig otherwise happily substitutes an unrelated default font).
bool fc_match(const std::string& family, bool bold, bool italic, uint32_t cp, FcMatchResult& out,
              bool exact = false) {
    FcPattern* pat = FcNameParse(reinterpret_cast<const FcChar8*>(family.c_str()));
    if (!pat) return false;
    std::string wanted;
    if (FcChar8* fam = nullptr; FcPatternGetString(pat, FC_FAMILY, 0, &fam) == FcResultMatch)
        wanted = reinterpret_cast<char*>(fam);
    if (wanted.empty() || is_generic_family(wanted)) exact = false;
    FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    if (cp) {
        FcCharSet* cs = FcCharSetCreate();
        FcCharSetAddChar(cs, cp);
        FcPatternAddCharSet(pat, FC_CHARSET, cs);
        FcCharSetDestroy(cs);
    }
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    bool ok = false;
    if (cp) {
        // Walk the sorted list and take the first font that has the glyph.
        FcFontSet* set = FcFontSort(nullptr, pat, FcTrue, nullptr, &res);
        if (set) {
            for (int i = 0; i < set->nfont && !ok; ++i) {
                FcCharSet* cs = nullptr;
                if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &cs) != FcResultMatch ||
                    !FcCharSetHasChar(cs, cp))
                    continue;
                FcPattern* m = FcFontRenderPrepare(nullptr, pat, set->fonts[i]);
                FcChar8* file = nullptr;
                if (m && FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch) {
                    out.path = reinterpret_cast<char*>(file);
                    FcPatternGetInteger(m, FC_INDEX, 0, &out.index);
                    FcPatternGetInteger(m, FC_WEIGHT, 0, &out.weight);
                    FcPatternGetInteger(m, FC_SLANT, 0, &out.slant);
                    ok = true;
                }
                if (m) FcPatternDestroy(m);
            }
            FcFontSetDestroy(set);
        }
    } else {
        FcPattern* m = FcFontMatch(nullptr, pat, &res);
        FcChar8* file = nullptr;
        bool family_ok = !exact;
        FcChar8* fam = nullptr;
        for (int i = 0; m && !family_ok && FcPatternGetString(m, FC_FAMILY, i, &fam) == FcResultMatch;
             ++i)
            family_ok = same_family(reinterpret_cast<char*>(fam), wanted.c_str());
        if (m && family_ok && FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch) {
            out.path = reinterpret_cast<char*>(file);
            FcPatternGetInteger(m, FC_INDEX, 0, &out.index);
            FcPatternGetInteger(m, FC_WEIGHT, 0, &out.weight);
            FcPatternGetInteger(m, FC_SLANT, 0, &out.slant);
            ok = true;
        }
        if (m) FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    return ok;
}
}  // namespace

bool FontSet::load(const std::string& family_in) {
    std::string family = family_in.empty() ? "monospace" : family_in;
    FcMatchResult probe;
    if (!fc_match(family, false, false, 0, probe, true)) {
        warning_ = "font '" + family + "' not found, using monospace";
        family = "monospace";
    }
    clear_faces();
    for (int style = 0; style < 4; ++style) {
        bool bold = style & 1, italic = style & 2;
        FcMatchResult m;
        if (!fc_match(family, bold, italic, 0, m) &&
            !fc_match("monospace", bold, italic, 0, m))
            continue;
        style_faces_[style] = open_face(m.path, m.index, bold, italic, m.weight, m.slant);
    }
    if (style_faces_[0] < 0) {
        std::fprintf(stderr, "neovim3d: could not load any font for '%s'\n", family.c_str());
        return false;
    }
    for (int& s : style_faces_)
        if (s < 0) s = style_faces_[0];
    family_ = family;
    primary_path_ = faces_[style_faces_[0]].path;

    FT_Face f = faces_[style_faces_[0]].face;
    primary_upem_ = float(f->units_per_EM);
    float asc = float(f->ascender), desc = float(f->descender);
    float line_h = asc - desc;
    if (line_h <= 0) line_h = primary_upem_ * 1.2f, asc = primary_upem_ * 0.95f;
    line_scale_ = 1.0f / line_h;
    ascent_ = asc * line_scale_;

    float adv = primary_upem_ * 0.6f;
    FT_UInt gi = FT_Get_Char_Index(f, 'M');
    if (gi && FT_Load_Glyph(f, gi, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) == 0)
        adv = float(f->glyph->advance.x);
    cell_width_ = adv * line_scale_;

    if (f->underline_thickness > 0) {
        underline_thickness_ = std::max(0.03f, float(f->underline_thickness) * line_scale_);
        underline_pos_ = ascent_ - float(f->underline_position) * line_scale_;
    } else {
        underline_thickness_ = 0.05f;
        underline_pos_ = ascent_ + 0.08f;
    }
    return true;
}

void FontSet::set_depth(float depth) {
    if (depth == depth_) return;
    depth_ = depth;
    cache_.clear();
    vertices.clear();
    indices.clear();
    dirty = true;
}

int FontSet::face_for(uint32_t cp, int style) {
    int primary = style_faces_[style];
    if (primary < 0) return -1;
    if (FT_Get_Char_Index(faces_[primary].face, cp)) return primary;
    uint64_t key = (uint64_t(cp) << 2) | uint64_t(style);
    if (auto it = fallback_cache_.find(key); it != fallback_cache_.end()) return it->second;
    int result = -1;
    FcMatchResult m;
    if (fc_match(family_, style & 1, style & 2, cp, m)) {
        int idx = open_face(m.path, m.index, style & 1, style & 2, m.weight, m.slant);
        if (idx >= 0 && FT_Get_Char_Index(faces_[idx].face, cp)) result = idx;
    }
    fallback_cache_[key] = result;
    return result;
}

const GlyphMesh& FontSet::glyph(const std::string& text, bool bold, bool italic, int cells) {
    std::string key = text;
    key.push_back(char('0' + (bold ? 1 : 0) + (italic ? 2 : 0) + (cells > 1 ? 4 : 0)));
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;

    GlyphMesh mesh;
    uint32_t cp = utf8_first(text);
    if (cp > ' ' && style_faces_[0] >= 0) {
        int style = (bold ? 1 : 0) | (italic ? 2 : 0);
        int fi = face_for(cp, style);
        if (fi < 0) fi = face_for(0xfffd, style);
        if (fi >= 0) mesh = build(fi, FT_Get_Char_Index(faces_[fi].face, cp ? cp : 0xfffd), cells);
    }
    return cache_.emplace(std::move(key), mesh).first->second;
}

GlyphMesh FontSet::build(int face_idx, uint32_t glyph_index, int cells) {
    GlyphMesh mesh;
    Face& fc = faces_[face_idx];
    FT_Face f = fc.face;
    if (FT_Load_Glyph(f, glyph_index, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) !=
            0 ||
        f->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return mesh;

    FT_Outline* outline = &f->glyph->outline;
    float upem = float(f->units_per_EM);
    if (fc.synthetic_bold) FT_Outline_EmboldenXY(outline, FT_Pos(upem / 28), FT_Pos(upem / 40));
    if (fc.synthetic_italic) {
        FT_Matrix shear{0x10000, 0x0366A, 0, 0x10000};  // ~12 degree slant
        FT_Outline_Transform(outline, &shear);
    }

    // Font units of this face -> line units (relative to the primary face).
    float scale = line_scale_ * primary_upem_ / upem;
    float advance = float(f->glyph->advance.x) * scale;
    float span = cell_width_ * float(cells);
    float fit = 1.0f;
    if (advance > span * 1.05f && advance > 0) fit = span / advance;

    OutlineSink sink;
    sink.scale = scale * fit;
    sink.step = upem / 24.0f;
    FT_Outline_Funcs funcs{move_to, line_to, conic_to, cubic_to, 0, 0};
    if (FT_Outline_Decompose(outline, &funcs, &sink) != 0) return mesh;

    // Drop the closing duplicate point of each contour.
    for (auto& c : sink.contours) {
        size_t n = c.xy.size();
        if (n >= 4 && c.xy[0] == c.xy[n - 2] && c.xy[1] == c.xy[n - 1]) c.xy.resize(n - 2);
    }

    // Place the glyph: baseline at -ascent, centred horizontally in its span.
    float xoff = (span - advance * fit) / 2;
    for (auto& c : sink.contours)
        for (size_t i = 0; i + 1 < c.xy.size(); i += 2) {
            c.xy[i] += xoff;
            c.xy[i + 1] -= ascent_;
        }

    bool outer_ccw = FT_Outline_Get_Orientation(outline) == FT_ORIENTATION_POSTSCRIPT;

    mesh.base_vertex = int32_t(vertices.size());
    mesh.first_index = uint32_t(indices.size());
    extrude_contours(sink.contours, outer_ccw, depth_, vertices, indices);
    mesh.index_count = uint32_t(indices.size()) - mesh.first_index;
    if (mesh.index_count > 0) dirty = true;
    return mesh;
}
