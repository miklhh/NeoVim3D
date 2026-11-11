// Records rendered frames to an animated WebP (in-process, with libwebp) or to
// a GIF/video (by piping raw RGB into ffmpeg).
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

struct RecordOptions {
    // .webp is encoded in-process with libwebp; .gif gets a two-pass optimised
    // palette from ffmpeg; anything else (e.g. .mp4) uses ffmpeg's defaults.
    std::string path;
    int fps = 20;
    int width = 0;     // output width in pixels (0 = framebuffer width), height keeps aspect
    std::string dither = "none";  // GIF only: ffmpeg paletteuse dither mode
    int colors = 256;             // GIF only: palette size (fewer = smaller file)
    int quality = 75;             // WebP only: 0-100 (lower = smaller file)
    int method = 4;               // WebP only: 0-6, effort (higher = smaller, slower)
    bool sharp_yuv = true;        // WebP only: keep thin coloured text crisp
};

class Recorder {
public:
    Recorder();
    ~Recorder();

    // Frames will be `width` x `height` RGB, bottom row first (as glReadPixels gives them).
    bool start(const RecordOptions& opts, int width, int height, std::string& error);
    bool add_frame(const uint8_t* rgb);
    // Write out the file (waiting for ffmpeg, or assembling the WebP).
    bool finish(std::string& error);

    bool active() const { return pipe_ != nullptr || webp_ != nullptr; }
    int width() const { return width_; }
    int height() const { return height_; }
    int fps() const { return opts_.fps; }
    int frames() const { return frames_; }
    const std::string& path() const { return opts_.path; }

    // Whether this build can write .webp files.
    static bool webp_supported();
    // The ffmpeg command line for non-WebP formats (exposed for tests).
    static std::string command(const RecordOptions& opts, int width, int height);
    // Output size for a given framebuffer size: `width` wide (if set), even
    // dimensions, aspect ratio kept.
    static void output_size(const RecordOptions& opts, int width, int height, int& out_w,
                            int& out_h);

private:
    struct Webp;

    FILE* pipe_ = nullptr;
    std::unique_ptr<Webp> webp_;
    RecordOptions opts_;
    int width_ = 0, height_ = 0, frames_ = 0;
};

// Area-averaging resize of an RGB image; `flip` turns a bottom-up image
// top-down at the same time.
void resize_rgb(const uint8_t* src, int sw, int sh, bool flip, uint8_t* dst, int dw, int dh);
