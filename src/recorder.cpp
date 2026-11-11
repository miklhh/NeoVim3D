#include "recorder.hpp"

#include <sys/wait.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#ifdef NEOVIM3D_HAVE_WEBP
#include <webp/encode.h>
#include <webp/mux.h>
#endif

namespace {

std::string shell_quote(const std::string& s) {
    std::string q = "'";
    for (char c : s) {
        if (c == '\'')
            q += "'\\''";
        else
            q += c;
    }
    return q + "'";
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// For each destination pixel along one axis: the source pixels it covers and
// how much of each (box filter with fractional edges).
std::vector<std::vector<std::pair<int, float>>> area_weights(int src, int dst) {
    std::vector<std::vector<std::pair<int, float>>> w(static_cast<size_t>(dst));
    const double scale = double(src) / dst;
    for (int i = 0; i < dst; ++i) {
        double lo = i * scale, hi = (i + 1) * scale;
        for (int j = int(std::floor(lo)); j < int(std::ceil(hi)) && j < src; ++j) {
            double overlap = std::min(hi, double(j + 1)) - std::max(lo, double(j));
            if (overlap > 0) w[size_t(i)].push_back({j, float(overlap / scale)});
        }
    }
    return w;
}

}  // namespace

void resize_rgb(const uint8_t* src, int sw, int sh, bool flip, uint8_t* dst, int dw, int dh) {
    auto wx = area_weights(sw, dw), wy = area_weights(sh, dh);
    // Horizontal pass into a float buffer, then vertical pass.
    std::vector<float> tmp(size_t(sh) * dw * 3);
    for (int y = 0; y < sh; ++y) {
        const uint8_t* row = src + size_t(flip ? sh - 1 - y : y) * sw * 3;
        float* out = &tmp[size_t(y) * dw * 3];
        for (int x = 0; x < dw; ++x) {
            float r = 0, g = 0, b = 0;
            for (auto [j, k] : wx[size_t(x)]) {
                r += row[j * 3] * k;
                g += row[j * 3 + 1] * k;
                b += row[j * 3 + 2] * k;
            }
            out[x * 3] = r, out[x * 3 + 1] = g, out[x * 3 + 2] = b;
        }
    }
    for (int y = 0; y < dh; ++y) {
        uint8_t* out = dst + size_t(y) * dw * 3;
        for (int x = 0; x < dw * 3; ++x) {
            float v = 0;
            for (auto [j, k] : wy[size_t(y)]) v += tmp[size_t(j) * dw * 3 + x] * k;
            out[x] = uint8_t(std::clamp(v + 0.5f, 0.0f, 255.0f));
        }
    }
}

#ifdef NEOVIM3D_HAVE_WEBP
struct Recorder::Webp {
    WebPAnimEncoder* enc = nullptr;
    WebPConfig config;
    int out_w = 0, out_h = 0;
    std::vector<uint8_t> scaled;

    ~Webp() {
        if (enc) WebPAnimEncoderDelete(enc);
    }
};
bool Recorder::webp_supported() { return true; }
#else
struct Recorder::Webp {};
bool Recorder::webp_supported() { return false; }
#endif

Recorder::Recorder() = default;

Recorder::~Recorder() {
    std::string ignored;
    finish(ignored);
}

void Recorder::output_size(const RecordOptions& o, int w, int h, int& out_w, int& out_h) {
    out_w = o.width > 0 ? o.width : w;
    out_h = int(std::lround(double(h) * out_w / w));
    out_w -= out_w % 2;
    out_h -= out_h % 2;
    out_w = std::max(out_w, 2);
    out_h = std::max(out_h, 2);
}

std::string Recorder::command(const RecordOptions& o, int w, int h) {
    std::string filters = "vflip";
    if (o.width > 0) filters += ",scale=" + std::to_string(o.width) + ":-2:flags=lanczos";

    std::string cmd = "ffmpeg -y -hide_banner -loglevel error -f rawvideo -pixel_format rgb24";
    cmd += " -video_size " + std::to_string(w) + "x" + std::to_string(h);
    cmd += " -framerate " + std::to_string(o.fps) + " -i -";
    if (ends_with(o.path, ".gif")) {
        // Two passes over the frames: build one palette from what actually
        // changes, then map every frame to it, only re-encoding changed areas.
        filters += ",split[a][b];[a]palettegen=stats_mode=diff:max_colors=" +
                   std::to_string(std::clamp(o.colors, 2, 256)) + "[p];[b][p]paletteuse=dither=" +
                   o.dither + ":diff_mode=rectangle";
        cmd += " -filter_complex " + shell_quote(filters);
    } else {
        filters += ",scale=trunc(iw/2)*2:trunc(ih/2)*2";
        cmd += " -vf " + shell_quote(filters) + " -pix_fmt yuv420p";
    }
    cmd += " " + shell_quote(o.path);
    return cmd;
}

bool Recorder::start(const RecordOptions& opts, int w, int h, std::string& error) {
    if (active()) {
        error = "already recording to " + opts_.path;
        return false;
    }
    if (opts.path.empty() || opts.fps <= 0 || w <= 0 || h <= 0) {
        error = "invalid recording options";
        return false;
    }
    opts_ = opts;
    width_ = w;
    height_ = h;
    frames_ = 0;

    if (ends_with(opts.path, ".webp")) {
#ifdef NEOVIM3D_HAVE_WEBP
        // Encoded with libwebp directly rather than through ffmpeg: ffmpeg's
        // libwebp_anim never emits keyframes, so lossy errors pile up into
        // streaks on long, constantly moving recordings.
        auto webp = std::make_unique<Webp>();
        output_size(opts, w, h, webp->out_w, webp->out_h);
        webp->scaled.resize(size_t(webp->out_w) * webp->out_h * 3);
        if (!WebPConfigPreset(&webp->config, WEBP_PRESET_DEFAULT,
                              float(std::clamp(opts.quality, 0, 100))) ||
            !WebPValidateConfig(&webp->config)) {
            error = "invalid WebP settings";
            return false;
        }
        webp->config.method = std::clamp(opts.method, 0, 6);
        webp->config.use_sharp_yuv = opts.sharp_yuv ? 1 : 0;
        webp->config.thread_level = 1;
        WebPAnimEncoderOptions ao;
        WebPAnimEncoderOptionsInit(&ao);
        ao.anim_params.loop_count = 0;  // loop forever
        ao.kmin = 3;                    // a fresh keyframe every 3-5 frames
        ao.kmax = 5;
        webp->enc = WebPAnimEncoderNew(webp->out_w, webp->out_h, &ao);
        if (!webp->enc) {
            error = "could not create the WebP encoder";
            return false;
        }
        webp_ = std::move(webp);
        return true;
#else
        error = "this build has no WebP support (install libwebp-dev and rebuild)";
        return false;
#endif
    }

    if (std::system("command -v ffmpeg >/dev/null 2>&1") != 0) {
        error = "recording to " + opts.path + " needs ffmpeg on PATH";
        return false;
    }
    pipe_ = popen(command(opts, w, h).c_str(), "w");
    if (!pipe_) {
        error = "could not start ffmpeg";
        return false;
    }
    return true;
}

#ifdef NEOVIM3D_HAVE_WEBP
namespace {
int timestamp_ms(int frame, int fps) { return int(std::lround(frame * 1000.0 / fps)); }
}  // namespace
#endif

bool Recorder::add_frame(const uint8_t* rgb) {
    if (pipe_) {
        size_t n = size_t(width_) * height_ * 3;
        if (std::fwrite(rgb, 1, n, pipe_) != n) return false;
        ++frames_;
        return true;
    }
#ifdef NEOVIM3D_HAVE_WEBP
    if (webp_) {
        Webp& wp = *webp_;
        resize_rgb(rgb, width_, height_, true, wp.scaled.data(), wp.out_w, wp.out_h);
        WebPPicture pic;
        if (!WebPPictureInit(&pic)) return false;
        pic.use_argb = 1;
        pic.width = wp.out_w;
        pic.height = wp.out_h;
        bool ok = WebPPictureImportRGB(&pic, wp.scaled.data(), wp.out_w * 3) &&
                  WebPAnimEncoderAdd(wp.enc, &pic, timestamp_ms(frames_, opts_.fps), &wp.config);
        WebPPictureFree(&pic);
        if (ok) ++frames_;
        return ok;
    }
#endif
    return false;
}

bool Recorder::finish(std::string& error) {
    if (pipe_) {
        int status = pclose(pipe_);
        pipe_ = nullptr;
        if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            error = "ffmpeg failed while writing " + opts_.path;
            return false;
        }
        return true;
    }
#ifdef NEOVIM3D_HAVE_WEBP
    if (webp_) {
        std::unique_ptr<Webp> wp = std::move(webp_);
        WebPData data;
        WebPDataInit(&data);
        // A final null frame sets the duration of the last real one.
        bool ok = WebPAnimEncoderAdd(wp->enc, nullptr, timestamp_ms(frames_, opts_.fps), nullptr) &&
                  WebPAnimEncoderAssemble(wp->enc, &data);
        if (!ok) {
            const char* msg = WebPAnimEncoderGetError(wp->enc);
            error = std::string("WebP encoding failed: ") + (msg && *msg ? msg : "unknown error");
            WebPDataClear(&data);
            return false;
        }
        FILE* f = std::fopen(opts_.path.c_str(), "wb");
        ok = f && std::fwrite(data.bytes, 1, data.size, f) == data.size;
        if (f && std::fclose(f) != 0) ok = false;
        WebPDataClear(&data);
        if (!ok) error = "could not write " + opts_.path;
        return ok;
    }
#endif
    return true;
}
