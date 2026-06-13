#pragma once

#include <exception>
#include <filesystem>
#include <string>

namespace codeharness::host {

class HostError : public std::exception {
 public:
  explicit HostError(std::string message) : message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }

  const std::string& message() const { return message_; }

 private:
  std::string message_;
};

class HostValueError : public HostError {
 public:
  explicit HostValueError(std::string message) : HostError(std::move(message)) {}
};

class HostFileExistsError : public HostError {
 public:
  explicit HostFileExistsError(std::filesystem::path path)
      : HostError("file already exists: " + path.string()), path_(std::move(path)) {}

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class HostShellNotFoundError : public HostError {
 public:
  explicit HostShellNotFoundError(std::string message) : HostError(std::move(message)) {}
};

class HostFileNotFoundError : public HostError {
 public:
  explicit HostFileNotFoundError(std::filesystem::path path)
      : HostError("file not found: " + path.string()), path_(std::move(path)) {}

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class HostPermissionError : public HostError {
 public:
  explicit HostPermissionError(std::filesystem::path path)
      : HostError("permission denied: " + path.string()), path_(std::move(path)) {}

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace codeharness::host