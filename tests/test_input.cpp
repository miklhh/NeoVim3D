#include <GLFW/glfw3.h>

#include "check.hpp"
#include "input.hpp"

TEST(input_special_keys) {
    CHECK_EQ(translate_key(GLFW_KEY_ESCAPE, 0, nullptr), std::string("<Esc>"));
    CHECK_EQ(translate_key(GLFW_KEY_ENTER, 0, nullptr), std::string("<CR>"));
    CHECK_EQ(translate_key(GLFW_KEY_LEFT, GLFW_MOD_SHIFT, nullptr), std::string("<S-Left>"));
    CHECK_EQ(translate_key(GLFW_KEY_F5, GLFW_MOD_CONTROL, nullptr), std::string("<C-F5>"));
    CHECK_EQ(translate_key(GLFW_KEY_PAGE_UP, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT, nullptr),
             std::string("<C-S-PageUp>"));
    CHECK_EQ(translate_key(GLFW_KEY_LEFT_SHIFT, GLFW_MOD_SHIFT, nullptr), std::string(""));
}

TEST(input_printable_keys) {
    // Plain printable keys are left to the character callback.
    CHECK_EQ(translate_key(GLFW_KEY_A, 0, "a"), std::string(""));
    CHECK_EQ(translate_key(GLFW_KEY_A, GLFW_MOD_SHIFT, "a"), std::string(""));
    CHECK_EQ(translate_key(GLFW_KEY_A, GLFW_MOD_CONTROL, "a"), std::string("<C-a>"));
    CHECK_EQ(translate_key(GLFW_KEY_A, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT, "a"),
             std::string("<C-S-a>"));
    CHECK_EQ(translate_key(GLFW_KEY_X, GLFW_MOD_ALT | GLFW_MOD_SHIFT, "x"), std::string("<A-X>"));
    CHECK_EQ(translate_key(GLFW_KEY_SPACE, GLFW_MOD_CONTROL, nullptr), std::string("<C-Space>"));
    CHECK_EQ(translate_key(GLFW_KEY_COMMA, GLFW_MOD_CONTROL, ","), std::string("<C-,>"));
    CHECK_EQ(translate_key(GLFW_KEY_BACKSLASH, GLFW_MOD_ALT, "\\"), std::string("<A-Bslash>"));
}

TEST(input_chars) {
    CHECK_EQ(translate_char('a', 0), std::string("a"));
    CHECK_EQ(translate_char('<', GLFW_MOD_SHIFT), std::string("<lt>"));
    CHECK_EQ(translate_char(0xe5, 0), std::string("\xc3\xa5"));  // å
    CHECK_EQ(translate_char('a', GLFW_MOD_CONTROL), std::string(""));
}

TEST(input_normalize) {
    CHECK_EQ(normalize_keys("<c-s-left>"), std::string("<C-S-Left>"));
    CHECK_EQ(normalize_keys("<S-C-pageup>"), std::string("<C-S-PageUp>"));
    CHECK_EQ(normalize_keys("<C-A>"), std::string("<C-a>"));
    CHECK_EQ(normalize_keys("<M-x>"), std::string("<A-x>"));
    CHECK_EQ(normalize_keys("<f12>"), std::string("<F12>"));
    CHECK_EQ(normalize_keys("<Space>"), std::string(" "));
    CHECK_EQ(normalize_keys("x"), std::string("x"));
}
