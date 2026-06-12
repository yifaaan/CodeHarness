#pragma once

#include <string>

namespace codeharness {

struct ProviderConfig {
  std::string type = "echo";
  std::string model;
  std::string api_key;
  std::string base_url;
};

}  // namespace codeharness
