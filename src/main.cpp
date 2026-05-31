#include "codeharness/cli/cli.h"

#include <iostream>

auto main(int argc, char **argv) -> int
{
    auto exit_code = codeharness::run_cli(argc, argv);

    if (!exit_code)
    {
        std::cerr << "error: " << exit_code.error().message << '\n';
        return 1;
    }

    return *exit_code;
}