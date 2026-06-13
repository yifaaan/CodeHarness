#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace codeharness::host {

struct StatResult {
  uint32_t st_mode = 0;
  uint64_t st_ino = 0;
  uint64_t st_dev = 0;
  uint16_t st_nlink = 0;
  uint32_t st_uid = 0;
  uint32_t st_gid = 0;
  int64_t st_size = 0;
  int64_t st_atime = 0;
  int64_t st_mtime = 0;
  int64_t st_ctime = 0;
};

struct GlobOptions {
  std::filesystem::path cwd;
  bool include_dirs = true;
  int max_depth = -1;
};

struct MkdirOptions {
  bool exist_ok = true;
  bool recursive = false;
};

}  // namespace codeharness::host