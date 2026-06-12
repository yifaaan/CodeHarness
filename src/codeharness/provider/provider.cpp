#include "codeharness/provider/provider.h"

#include <string_view>
#include <utility>

#include "codeharness/core/error.h"

namespace codeharness {

namespace {

auto json_int_value(const nlohmann::json& input, std::string_view key) -> int {
  const auto found = input.find(std::string{key});
  if (found == input.end() || !found->is_number_integer()) {
    return 0;
  }
  return found->get<int>();
}

}  // namespace

auto to_json(nlohmann::json& output, const ProviderUsage& usage) -> void {
  output = nlohmann::json{
      {"input_tokens", usage.input_tokens},
      {"output_tokens", usage.output_tokens},
      {"total_tokens", usage.normalized_total()},
  };
}

auto from_json(const nlohmann::json& input, ProviderUsage& usage) -> void {
  auto input_tokens = json_int_value(input, "input_tokens");
  if (input_tokens == 0) {
    input_tokens = json_int_value(input, "prompt_tokens");
  }

  auto output_tokens = json_int_value(input, "output_tokens");
  if (output_tokens == 0) {
    output_tokens = json_int_value(input, "completion_tokens");
  }

  usage = ProviderUsage{
      .input_tokens = input_tokens,
      .output_tokens = output_tokens,
      .total_tokens = json_int_value(input, "total_tokens"),
  };
  usage.total_tokens = usage.normalized_total();
}

}  // namespace codeharness
