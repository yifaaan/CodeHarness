#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "codeharness/core/error.h"

namespace codeharness {

// 读取完整文本文件。
//
// 调用方应先使用resolve_workspace_path。
auto read_text_file(const std::filesystem::path& path) -> absl::StatusOr<std::string>;

// 原子写入文本文件。
auto atomic_write_text_file(const std::filesystem::path& target_path, std::string_view content) -> absl::Status;

}  // namespace codeharness
