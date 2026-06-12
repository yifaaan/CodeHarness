#pragma once

#include <chrono>
#include <cstdint>

namespace codeharness::host {

struct StatResult {
  bool is_directory = false;
  bool is_regular_file = false;
  bool is_symlink = false;
  std::uintmax_t size = 0;
  std::chrono::system_clock::time_point modified_time{};
  std::uint64_t device_id = 0;
  std::uint64_t inode_number = 0;
};

}  // namespace codeharness::host
