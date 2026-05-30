#pragma once

#include <string>

namespace codeharness
{

enum class ErrorKind
{
    InvalidArgument,
    Config,
    Io,
    Network,
    Provider,
    Tool,
    Internal
};

struct CodeHarnessError
{
    ErrorKind kind = ErrorKind::Internal;
    std::string message;
};

inline auto make_error(ErrorKind kind, std::string message) -> CodeHarnessError
{
    return CodeHarnessError{kind, std::move(message)};
}

} // namespace codeharness