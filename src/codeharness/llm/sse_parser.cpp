#include "sse_parser.h"

namespace codeharness::llm {

void SseParser::Feed(std::string_view data) { buffer_.append(data); }

std::optional<std::string> SseParser::NextEvent() {
  if (done_) return std::nullopt;

  std::string line;
  while (TryExtractLine(line)) {
    if (line.empty()) {
      if (current_event_has_data_) {
        if (current_event_data_ == "[DONE]") {
          done_ = true;
          current_event_data_.clear();
          current_event_has_data_ = false;
          return std::nullopt;
        }
        std::string result = std::move(current_event_data_);
        current_event_data_.clear();
        current_event_has_data_ = false;
        return result;
      }
      continue;
    }

    if (line[0] == ':') continue;

    if (line.starts_with("data:")) {
      std::string_view value(line.data() + 5, line.size() - 5);
      if (!value.empty() && value.front() == ' ') value = value.substr(1);

      if (current_event_has_data_) current_event_data_ += '\n';
      current_event_data_.append(value);
      current_event_has_data_ = true;
    }
  }

  return std::nullopt;
}

bool SseParser::Done() const { return done_; }

void SseParser::Reset() {
  buffer_.clear();
  current_event_data_.clear();
  current_event_has_data_ = false;
  done_ = false;
}

bool SseParser::TryExtractLine(std::string& line) {
  if (buffer_.empty()) return false;

  size_t end_pos = std::string::npos;
  size_t term_len = 0;

  for (size_t i = 0; i < buffer_.size(); ++i) {
    if (buffer_[i] == '\n') {
      end_pos = i;
      term_len = 1;
      break;
    }
    if (buffer_[i] == '\r') {
      end_pos = i;
      term_len = (i + 1 < buffer_.size() && buffer_[i + 1] == '\n') ? 2 : 1;
      break;
    }
  }

  if (end_pos == std::string::npos) return false;

  line.assign(buffer_, 0, end_pos);
  buffer_.erase(0, end_pos + term_len);
  return true;
}

}  // namespace codeharness::llm
