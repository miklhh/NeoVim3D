// Minimal PNG writer (uncompressed deflate), used for --screenshot-3d.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// `rgb` holds width*height*3 bytes, top row first.
bool write_png(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb);
