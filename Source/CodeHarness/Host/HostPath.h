#pragma once

#include <absl/status/statusor.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "HostTypes.h"

namespace codeharness::host
{

	class Host;

	class HostPath
	{
	public:
		HostPath(std::string_view path, std::string_view pathClass);
		explicit HostPath(const std::filesystem::path& path, std::string_view pathClass = "posix");

		std::string Name() const;
		HostPath Parent() const;
		std::string String() const;
		bool IsAbsolute() const;
		std::string Extension() const;
		std::string Stem() const;

		HostPath operator/(std::string_view other) const;
		HostPath Joinpath(std::string_view other) const;
		HostPath Canonical() const;
		HostPath RelativeTo(const HostPath& other) const;
		HostPath ExpandUser() const;
		HostPath Resolve() const;

		absl::StatusOr<StatResult> Stat(Host* host = nullptr) const;
		absl::StatusOr<bool> Exists(Host* host = nullptr) const;
		absl::StatusOr<bool> IsFile(Host* host = nullptr) const;
		absl::StatusOr<bool> IsDir(Host* host = nullptr) const;
		absl::StatusOr<std::vector<std::string>> Iterdir(Host* host = nullptr) const;
		absl::StatusOr<std::vector<std::string>> Glob(std::string_view pattern, Host* host = nullptr) const;
		absl::StatusOr<std::string> ReadText(Host* host = nullptr) const;
		absl::StatusOr<std::vector<std::string>> ReadLines(Host* host = nullptr, int count = 0) const;
		absl::Status WriteText(std::string_view data, Host* host = nullptr) const;
		absl::Status Mkdir(const MkdirOptions& options = {}, Host* host = nullptr) const;

	private:
		std::filesystem::path path;
		std::string pathClass;
	};

} // namespace codeharness::host