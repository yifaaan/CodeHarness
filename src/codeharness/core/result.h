#pragma once

#include <nonstd/expected.hpp>
#include <string>

#include "codeharness/core/error.h"

namespace codeharness
{

template <typename T>
using Result = nonstd::expected<T, CodeHarnessError>;

template <typename T>
auto fail(ErrorKind kind, std::string message) -> Result<T>
{
    return nonstd::make_unexpected(make_error(kind, std::move(message)));
}

} // namespace codeharness