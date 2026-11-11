#include "input.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>

std::string utf8_encode(uint32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s.push_back(char(cp));
    } else if (cp < 0x800) {
        s.push_back(char(0xc0 | (cp >> 6)));
        s.push_back(char(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        s.push_back(char(0xe0 | (cp >> 12)));
        s.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
        s.push_back(char(0x80 | (cp & 0x3f)));
    } else {
        s.push_back(char(0xf0 | (cp >> 18)));
        s.push_back(char(0x80 | ((cp >> 12) & 0x3f)));
        s.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
        s.push_back(char(0x80 | (cp & 0x3f)));
    }
    return s;
}

std::string mod_prefix(int mods, bool include_shift) {
    std::string p;
    if (mods & GLFW_MOD_CONTROL) p += "C-";
    if (include_shift && (mods & GLFW_MOD_SHIFT)) p += "S-";
    if (mods & GLFW_MOD_ALT) p += "A-";
    if (mods & GLFW_MOD_SUPER) p += "D-";
    return p;
}

namespace {

const char* special_name(int key) {
    switch (key) {
        case GLFW_KEY_ESCAPE: return "Esc";
        case GLFW_KEY_ENTER: return "CR";
        case GLFW_KEY_TAB: return "Tab";
        case GLFW_KEY_BACKSPACE: return "BS";
        case GLFW_KEY_INSERT: return "Insert";
        case GLFW_KEY_DELETE: return "Del";
        case GLFW_KEY_RIGHT: return "Right";
        case GLFW_KEY_LEFT: return "Left";
        case GLFW_KEY_DOWN: return "Down";
        case GLFW_KEY_UP: return "Up";
        case GLFW_KEY_PAGE_UP: return "PageUp";
        case GLFW_KEY_PAGE_DOWN: return "PageDown";
        case GLFW_KEY_HOME: return "Home";
        case GLFW_KEY_END: return "End";
        case GLFW_KEY_KP_ENTER: return "kEnter";
        case GLFW_KEY_KP_ADD: return "kPlus";
        case GLFW_KEY_KP_SUBTRACT: return "kMinus";
        case GLFW_KEY_KP_MULTIPLY: return "kMultiply";
        case GLFW_KEY_KP_DIVIDE: return "kDivide";
        case GLFW_KEY_KP_DECIMAL: return "kPoint";
        default: return nullptr;
    }
}

bool is_modifier_key(int key) {
    switch (key) {
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT:
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL:
        case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT:
        case GLFW_KEY_LEFT_SUPER:
        case GLFW_KEY_RIGHT_SUPER:
        case GLFW_KEY_CAPS_LOCK:
        case GLFW_KEY_NUM_LOCK:
        case GLFW_KEY_SCROLL_LOCK:
        case GLFW_KEY_MENU:
            return true;
        default:
            return false;
    }
}

// Characters that need a name inside <...>.
std::string char_name(const std::string& ch) {
    if (ch == "<") return "lt";
    if (ch == "\\") return "Bslash";
    if (ch == "|") return "Bar";
    if (ch == " ") return "Space";
    return ch;
}

}  // namespace

std::string translate_key(int key, int mods, const char* key_name) {
    if (is_modifier_key(key)) return "";

    if (const char* name = special_name(key)) return "<" + mod_prefix(mods, true) + name + ">";
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25)
        return "<" + mod_prefix(mods, true) + "F" + std::to_string(key - GLFW_KEY_F1 + 1) + ">";
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9 && (mods & GLFW_MOD_NUM_LOCK) == 0)
        return "<" + mod_prefix(mods, true) + "k" + std::to_string(key - GLFW_KEY_KP_0) + ">";

    // Printable keys only come through here when a non-shift modifier is held;
    // otherwise the character callback (which knows the layout) handles them.
    const int hard_mods = GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER;
    if ((mods & hard_mods) == 0) return "";

    std::string ch;
    if (key == GLFW_KEY_SPACE) {
        ch = " ";
    } else if (key_name && *key_name) {
        ch = key_name;
    } else if (key >= 32 && key < 127) {
        ch = std::string(1, char(std::tolower(key)));
    } else {
        return "";
    }
    bool shift = mods & GLFW_MOD_SHIFT;
    if (shift && ch.size() == 1 && std::isalpha(uint8_t(ch[0]))) {
        // <C-S-a> is what Nvim expects for Ctrl+Shift+A; keep the letter lowercase
        // for Ctrl, uppercase for pure Alt/Super combos (<A-A>).
        if (!(mods & GLFW_MOD_CONTROL)) {
            ch[0] = char(std::toupper(uint8_t(ch[0])));
            shift = false;
        }
    }
    int m = mods;
    if (!shift) m &= ~GLFW_MOD_SHIFT;
    return "<" + mod_prefix(m, true) + char_name(ch) + ">";
}

std::string translate_char(uint32_t cp, int mods) {
    const int hard_mods = GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER;
    if (mods & hard_mods) return "";
    if (cp == '<') return "<lt>";
    return utf8_encode(cp);
}

std::string normalize_keys(const std::string& keys) {
    // Split "<...>" groups, canonicalise modifier order/case and key names.
    std::string out;
    size_t i = 0;
    while (i < keys.size()) {
        if (keys[i] != '<') {
            out.push_back(keys[i++]);
            continue;
        }
        size_t end = keys.find('>', i + 1);
        if (end == std::string::npos) {
            out += keys.substr(i);
            break;
        }
        std::string inner = keys.substr(i + 1, end - i - 1);
        i = end + 1;
        bool c = false, s = false, a = false, d = false;
        for (;;) {
            if (inner.size() > 2 && inner[1] == '-') {
                char m = char(std::toupper(uint8_t(inner[0])));
                if (m == 'C') c = true;
                else if (m == 'S') s = true;
                else if (m == 'A' || m == 'M') a = true;
                else if (m == 'D' || m == 'T') d = true;
                else break;
                inner = inner.substr(2);
            } else {
                break;
            }
        }
        std::string lower = inner;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return char(std::tolower(ch)); });
        static const char* names[] = {"Esc",    "CR",       "Tab",  "BS",     "Insert", "Del",
                                      "Right",  "Left",     "Down", "Up",     "PageUp", "PageDown",
                                      "Home",   "End",      "Space", "lt",    "Bslash", "Bar",
                                      "kEnter", "kPlus",    "kMinus", "kMultiply", "kDivide",
                                      "kPoint"};
        std::string key = inner;
        for (const char* n : names) {
            std::string nl = n;
            std::transform(nl.begin(), nl.end(), nl.begin(),
                           [](unsigned char ch) { return char(std::tolower(ch)); });
            if (nl == lower) key = n;
        }
        if (lower == "return" || lower == "enter") key = "CR";
        if (lower == "escape") key = "Esc";
        if (lower == "backspace") key = "BS";
        if (lower == "delete") key = "Del";
        if (lower.size() >= 2 && lower[0] == 'f' && std::isdigit(uint8_t(lower[1])))
            key = "F" + lower.substr(1);
        if (key.size() == 1 && std::isalpha(uint8_t(key[0])) && c)
            key[0] = char(std::tolower(uint8_t(key[0])));
        bool special = key.size() > 1;
        if (!c && !s && !a && !d && key == "Space") {
            out += " ";
            continue;
        }
        if (!c && !s && !a && !d && !special) {
            out += key;
            continue;
        }
        std::string p;
        if (c) p += "C-";
        if (s) p += "S-";
        if (a) p += "A-";
        if (d) p += "D-";
        out += "<" + p + key + ">";
    }
    return out;
}
