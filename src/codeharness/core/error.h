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
    Internal,
    AlreadyExists, // 资源已存在（例如重复创建团队）
    NotFound,      // 资源不存在（例如查找不存在的团队或成员）
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
