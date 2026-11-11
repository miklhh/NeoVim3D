#include "renderer.hpp"

#include <epoxy/gl.h>

#include <cstdio>
#include <vector>

namespace {

const char* kGlyphVS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 i_offset;
layout(location = 3) in vec4 i_color;
uniform mat4 u_viewproj;
out vec3 v_world;
out vec3 v_normal;
out vec4 v_color;
void main() {
    vec3 p = a_pos + i_offset;
    v_world = p;
    v_normal = a_normal;
    v_color = i_color;
    gl_Position = u_viewproj * vec4(p, 1.0);
}
)";

const char* kBoxVS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 i_origin;
layout(location = 3) in vec3 i_size;
layout(location = 4) in vec4 i_color;
uniform mat4 u_viewproj;
out vec3 v_world;
out vec3 v_normal;
out vec4 v_color;
void main() {
    vec3 p = i_origin + a_pos * i_size;
    v_world = p;
    v_normal = a_normal;
    v_color = i_color;
    gl_Position = u_viewproj * vec4(p, 1.0);
}
)";

const char* kLitFS = R"(#version 330 core
in vec3 v_world;
in vec3 v_normal;
in vec4 v_color;
uniform vec3 u_light_dir;
uniform vec3 u_eye;
uniform float u_ambient;
uniform float u_specular;
out vec4 frag;
void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(u_light_dir);
    vec3 v = normalize(u_eye - v_world);
    // Faces pointing at the viewer's default direction (+Z) keep their true
    // colour so the head-on view looks like a normal editor.
    float facing = max(n.z, 0.0);
    float diffuse = max(dot(n, l), 0.0);
    float light = mix(u_ambient + (1.0 - u_ambient) * diffuse, 1.0, facing * facing);
    vec3 h = normalize(l + v);
    float spec = u_specular * pow(max(dot(n, h), 0.0), 32.0) * (1.0 - facing);
    frag = vec4(v_color.rgb * light + vec3(spec), v_color.a);
}
)";

// Unit cube [0,1]^3 with per-face normals.
std::vector<float> cube_vertices() {
    const float f[6][4][3] = {
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},  // +Z
        {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},  // -Z
        {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},  // +X
        {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},  // -X
        {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},  // +Y
        {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},  // -Y
    };
    const float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const int tri[6] = {0, 1, 2, 0, 2, 3};
    std::vector<float> v;
    for (int face = 0; face < 6; ++face)
        for (int k : tri) {
            v.insert(v.end(), {f[face][k][0], f[face][k][1], f[face][k][2]});
            v.insert(v.end(), {n[face][0], n[face][1], n[face][2]});
        }
    return v;
}

}  // namespace

unsigned Renderer::compile(const char* vs, const char* fs) {
    auto shader = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            std::fprintf(stderr, "neovim3d: shader error: %s\n", log);
        }
        return s;
    };
    GLuint p = glCreateProgram();
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "neovim3d: link error: %s\n", log);
        return 0;
    }
    return p;
}

bool Renderer::init() {
    glyph_prog_ = compile(kGlyphVS, kLitFS);
    box_prog_ = compile(kBoxVS, kLitFS);
    if (!glyph_prog_ || !box_prog_) return false;

    // Glyph meshes: per-vertex position+normal, per-instance offset+colour.
    glGenVertexArrays(1, &glyph_vao_);
    glGenBuffers(1, &glyph_vbo_);
    glGenBuffers(1, &glyph_ebo_);
    glGenBuffers(1, &glyph_inst_);
    glBindVertexArray(glyph_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, glyph_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex), (void*)(3 * sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glyph_ebo_);
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);

    // Boxes: unit cube, per-instance origin+size+colour.
    glGenVertexArrays(1, &box_vao_);
    glGenBuffers(1, &box_vbo_);
    glGenBuffers(1, &box_inst_);
    glBindVertexArray(box_vao_);
    auto cube = cube_vertices();
    glBindBuffer(GL_ARRAY_BUFFER, box_vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(cube.size() * sizeof(float)), cube.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, box_inst_);
    const GLsizei bs = sizeof(BoxInstance);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, bs, (void*)0);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, bs, (void*)(3 * sizeof(float)));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, bs, (void*)(6 * sizeof(float)));
    glVertexAttribDivisor(4, 1);
    glBindVertexArray(0);
    return true;
}

void Renderer::destroy() {
    GLuint bufs[] = {glyph_vbo_, glyph_ebo_, glyph_inst_, box_vbo_, box_inst_};
    glDeleteBuffers(5, bufs);
    GLuint vaos[] = {glyph_vao_, box_vao_};
    glDeleteVertexArrays(2, vaos);
    glDeleteProgram(glyph_prog_);
    glDeleteProgram(box_prog_);
}

void Renderer::upload(FontSet& font, const Scene& scene) {
    if (font.dirty) {
        glBindBuffer(GL_ARRAY_BUFFER, glyph_vbo_);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(font.vertices.size() * sizeof(GlyphVertex)),
                     font.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glyph_ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(font.indices.size() * sizeof(uint32_t)),
                     font.indices.data(), GL_STATIC_DRAW);
        font.dirty = false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, glyph_inst_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(scene.glyphs.size() * sizeof(GlyphInstance)),
                 scene.glyphs.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, box_inst_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(scene.boxes.size() * sizeof(BoxInstance)),
                 scene.boxes.data(), GL_DYNAMIC_DRAW);
}

void Renderer::draw(const Scene& scene, const Camera& cam, const Options& opt, int fbw, int fbh,
                    uint32_t clear) {
    glViewport(0, 0, fbw, fbh);
    float dim = opt.background_dim;
    glClearColor(float((clear >> 16) & 0xff) / 255.0f * dim, float((clear >> 8) & 0xff) / 255.0f * dim,
                 float(clear & 0xff) / 255.0f * dim, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    if (opt.msaa > 0) glEnable(GL_MULTISAMPLE);

    float aspect = fbh > 0 ? float(fbw) / float(fbh) : 1.0f;
    Mat4 vp = cam.projection(aspect) * cam.view();
    Vec3 eye = cam.eye();

    auto set_common = [&](GLuint prog) {
        glUseProgram(prog);
        glUniformMatrix4fv(glGetUniformLocation(prog, "u_viewproj"), 1, GL_FALSE, vp.m);
        glUniform3f(glGetUniformLocation(prog, "u_light_dir"), opt.light_dir.x, opt.light_dir.y,
                    opt.light_dir.z);
        glUniform3f(glGetUniformLocation(prog, "u_eye"), eye.x, eye.y, eye.z);
        glUniform1f(glGetUniformLocation(prog, "u_ambient"), opt.ambient);
        glUniform1f(glGetUniformLocation(prog, "u_specular"), opt.specular);
    };

    if (!scene.boxes.empty()) {
        set_common(box_prog_);
        glBindVertexArray(box_vao_);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, GLsizei(scene.boxes.size()));
    }

    if (!scene.batches.empty()) {
        set_common(glyph_prog_);
        glBindVertexArray(glyph_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, glyph_inst_);
        const GLsizei gs = sizeof(GlyphInstance);
        for (const GlyphBatch& b : scene.batches) {
            const size_t off = size_t(b.first_instance) * sizeof(GlyphInstance);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, gs, (void*)off);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, gs, (void*)(off + 3 * sizeof(float)));
            glDrawElementsInstancedBaseVertex(
                GL_TRIANGLES, GLsizei(b.mesh.index_count), GL_UNSIGNED_INT,
                (void*)(size_t(b.mesh.first_index) * sizeof(uint32_t)), GLsizei(b.count),
                b.mesh.base_vertex);
        }
    }
    glBindVertexArray(0);
}
