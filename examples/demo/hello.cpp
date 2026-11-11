#include <iostream>
#include <string>
#include <vector>

// Hello from NeoVim3D!
int main() {
    std::vector<std::string> words{"Neovim", "in", "3D"};
    for (const auto& word : words)
        std::cout << word << ' ';
    std::cout << '\n';
    return 0;
}
