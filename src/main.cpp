#include <iostream>
#include <string_view>

#include "codeharness/version.h"

namespace {

bool is_arg(int argc, char** argv, std::string_view expected) {
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == expected) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (is_arg(argc, argv, "--version")) {
        std::cout << codeharness::kProjectName << ' ' << codeharness::kVersion << '\n';
        return 0;
    }

    std::cout << codeharness::kProjectName << " C++20 skeleton initialized with xmake.\n";
    std::cout << "Next step: implement the provider, engine, and tool modules.\n";
    return 0;
}
