#include "codeharness/cli/cli.h"

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef _WIN32
namespace
{

auto wide_to_utf8(const wchar_t* wide) -> std::string
{
    if (wide == nullptr)
    {
        return {};
    }

    const auto wide_len = static_cast<int>(std::wcslen(wide));
    if (wide_len == 0)
    {
        return {};
    }

    const auto needed = WideCharToMultiByte(
        CP_UTF8, 0, wide, wide_len, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
    {
        return {};
    }

    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide, wide_len, out.data(), needed, nullptr, nullptr);
    return out;
}

auto build_utf8_argv() -> std::pair<std::vector<std::string>, std::vector<char*>>
{
    std::pair<std::vector<std::string>, std::vector<char*>> result;
    auto& [storage, pointers] = result;

    int wide_argc = 0;
    LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv == nullptr || wide_argc <= 0)
    {
        if (wide_argv != nullptr)
        {
            LocalFree(wide_argv);
        }
        return result;
    }

    storage.reserve(static_cast<std::size_t>(wide_argc));
    for (int i = 0; i < wide_argc; ++i)
    {
        storage.push_back(wide_to_utf8(wide_argv[i]));
    }
    LocalFree(wide_argv);

    pointers.reserve(storage.size());
    for (auto& arg : storage)
    {
        pointers.push_back(arg.data());
    }
    return result;
}

} // namespace
#endif

auto main(int argc, char** argv) -> int
{
#ifdef _WIN32
    // The C runtime's argv is encoded in the system ANSI code page, which
    // turns non-ANSI characters (Chinese, Japanese, emoji, ...) into '?'.
    // Re-derive argv from GetCommandLineW so prompts, paths, and tool input
    // reach the harness as UTF-8.
    auto [utf8_storage, utf8_pointers] = build_utf8_argv();
    if (!utf8_pointers.empty())
    {
        argc = static_cast<int>(utf8_pointers.size());
        argv = utf8_pointers.data();
    }

    // Streamed deltas and errors are UTF-8; the console defaults to the OEM
    // code page on Windows, so mojibake would appear without this.
    SetConsoleOutputCP(CP_UTF8);
#endif

    auto exit_code = codeharness::run_cli(argc, argv);

    if (!exit_code)
    {
        std::cerr << "error: " << exit_code.error().message << '\n';
        return 1;
    }

    return *exit_code;
}
