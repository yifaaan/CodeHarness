#pragma once

#include <filesystem>

#include "codeharness/core/error.h"

namespace codeharness {

// 判断 target 是否位于 base 目录内部。
//
// 举例：
//   base   = D:/code/CodeHarness
//   target = D:/code/CodeHarness/src/main.cpp
//   结果：true
//
//   base   = D:/code/CodeHarness
//   target = D:/code/outside.txt
//   结果：false
//   调用前，base 和 target 已经被规范化。
auto is_under_directory(const std::filesystem::path& base, const std::filesystem::path& target) -> bool;

// 将用户传入的工具路径解析成 cwd 下的安全路径。
//
// 规则：
//   1. path 不能为空
//   2. path 不能是绝对路径
//   3. path 不能通过 ../ 逃逸 cwd
//   4. 返回的是规范化后的路径
//
// 这个函数给这些工具共用：
//   - read_file
//   - write_file
//   - edit_file
//   - glob
//   - grep
auto resolve_workspace_path(const std::filesystem::path& cwd, const std::filesystem::path& requested)
    -> absl::StatusOr<std::filesystem::path>;

}  // namespace codeharness
