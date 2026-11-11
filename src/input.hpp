// Translation of GLFW keyboard events into Neovim key notation ("<C-a>").
#pragma once

#include <cstdint>
#include <string>

// Modifier prefix such as "C-S-" for the given GLFW modifier bits.
std::string mod_prefix(int mods, bool include_shift);

// For a GLFW key press. `key_name` is glfwGetKeyName() for the key (may be
// null). Returns the Neovim keys to send, or "" when the key should instead be
// delivered through the character callback.
std::string translate_key(int key, int mods, const char* key_name);

// For a GLFW character event. Returns "" when the character was already
// handled by translate_key (because Ctrl/Alt/Super was held).
std::string translate_char(uint32_t codepoint, int mods);

// Encode a code point as UTF-8.
std::string utf8_encode(uint32_t cp);

// Normalise a user-written key ("<c-a>", "<A-Left>") into the canonical form
// produced by translate_key/translate_char so they can be compared.
std::string normalize_keys(const std::string& keys);
