#include "capability.h"

#include <regex>
#include <string>

namespace codeharness::llm {

ModelCapability GetCapability(std::string_view model_name) {
  std::string name(model_name);
  std::transform(name.begin(), name.end(), name.begin(), ::tolower);

  ModelCapability cap;

  if (std::regex_search(name, std::regex(R"(^gpt-4o)"))) {
    cap.image_in = true;
    cap.tool_use = true;
    cap.max_context_tokens = 128000;
  } else if (std::regex_search(name, std::regex(R"(^gpt-4\.1)"))) {
    cap.image_in = true;
    cap.tool_use = true;
    cap.max_context_tokens = 1047576;
  } else if (std::regex_search(name, std::regex(R"(^gpt-4\.5)"))) {
    cap.image_in = true;
    cap.tool_use = true;
    cap.max_context_tokens = 128000;
  } else if (std::regex_search(name, std::regex(R"(^gpt-4-turbo)"))) {
    cap.image_in = true;
    cap.tool_use = true;
    cap.max_context_tokens = 128000;
  } else if (std::regex_search(name, std::regex(R"(^gpt-3\.5)"))) {
    cap.tool_use = true;
    cap.max_context_tokens = 16385;
  } else if (std::regex_search(name, std::regex(R"(^o\d)"))) {
    cap.thinking = true;
    cap.tool_use = true;
    cap.max_context_tokens = 200000;
  } else if (std::regex_search(name, std::regex(R"(claude.*4)"))) {
    cap.image_in = true;
    cap.thinking = true;
    cap.tool_use = true;
    cap.max_context_tokens = 200000;
  } else if (std::regex_search(name, std::regex(R"(claude.*3)"))) {
    cap.image_in = true;
    cap.tool_use = true;
    cap.max_context_tokens = 200000;
  } else if (std::regex_search(name, std::regex(R"(gemini-2\.5)"))) {
    cap.image_in = true;
    cap.video_in = true;
    cap.audio_in = true;
    cap.thinking = true;
    cap.tool_use = true;
    cap.max_context_tokens = 1048576;
  } else if (std::regex_search(name, std::regex(R"(gemini-2\.0)"))) {
    cap.image_in = true;
    cap.video_in = true;
    cap.audio_in = true;
    cap.tool_use = true;
    cap.max_context_tokens = 1048576;
  } else if (std::regex_search(name, std::regex(R"(gemini-1\.5)"))) {
    cap.image_in = true;
    cap.video_in = true;
    cap.audio_in = true;
    cap.tool_use = true;
    cap.max_context_tokens = 1048576;
  } else {
    cap = kUnknownCapability;
  }

  return cap;
}

}  // namespace codeharness::llm
