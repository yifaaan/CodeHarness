#include "HostPath.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <fmt/format.h>

#include <cstdlib>
#include <filesystem>

#include "CurrentHost.h"

namespace codeharness::host
{

	namespace
	{

		absl::StatusOr<Host*> ResolveHost(Host* host)
		{
			if (host)
				return host;
			auto* h = GetCurrentHostNoThrow();
			if (h)
				return h;
			return absl::InternalError("no host available");
		}

	} // namespace

	HostPath::HostPath(std::string_view path, std::string_view pathClass) : path(path), pathClass(pathClass) {}

	HostPath::HostPath(const std::filesystem::path& path, std::string_view pathClass)
		: path(path), pathClass(pathClass) {}

	std::string HostPath::Name() const
	{
		return path.filename().string();
	}

	HostPath HostPath::Parent() const
	{
		return HostPath(path.parent_path(), pathClass);
	}

	std::string HostPath::String() const
	{
		return path.string();
	}

	bool HostPath::IsAbsolute() const
	{
		return path.is_absolute();
	}

	std::string HostPath::Extension() const
	{
		return path.extension().string();
	}

	std::string HostPath::Stem() const
	{
		return path.stem().string();
	}

	HostPath HostPath::operator/(std::string_view other) const
	{
		return HostPath(path / other, pathClass);
	}

	HostPath HostPath::Joinpath(std::string_view other) const
	{
		return *this / other;
	}

	HostPath HostPath::Canonical() const
	{
		std::error_code ec;
		auto p = std::filesystem::canonical(path, ec);
		return HostPath(ec ? path : p, pathClass);
	}

	HostPath HostPath::RelativeTo(const HostPath& other) const
	{
		auto rel = std::filesystem::relative(path, other.path);
		return HostPath(rel, pathClass);
	}

	HostPath HostPath::ExpandUser() const
	{
		if (path.empty() || path.string()[0] != '~')
			return *this;
		const char* Home = std::getenv("HOME");
		if (!Home)
			Home = std::getenv("USERPROFILE");
		if (!Home)
			return *this;
		auto S = path.string();
		if (S.size() == 1)
			return HostPath(std::filesystem::path(Home), pathClass);
		return HostPath(std::filesystem::path(std::string(Home) + S.substr(1)), pathClass);
	}

	HostPath HostPath::Resolve() const
	{
		std::error_code ec;
		auto p = std::filesystem::absolute(path, ec);
		return HostPath(ec ? path : p.lexically_normal(), pathClass);
	}

	absl::StatusOr<StatResult> HostPath::Stat(Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return h.status();
		return (*h)->Stat(path.string());
	}

	absl::StatusOr<bool> HostPath::Exists(Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return std::filesystem::exists(path);
		auto s = (*h)->Stat(path.string());
		if (s.ok())
			return true;
		if (absl::IsNotFound(s.status()))
			return false;
		if (absl::IsPermissionDenied(s.status()))
			return false;
		return false;
	}

	absl::StatusOr<bool> HostPath::IsFile(Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return std::filesystem::is_regular_file(path);
		auto s = (*h)->Stat(path.string());
		if (!s.ok())
			return false;
		return (s->stMode & 0170000) == 0100000;
	}

	absl::StatusOr<bool> HostPath::IsDir(Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return std::filesystem::is_directory(path);
		auto s = (*h)->Stat(path.string());
		if (!s.ok())
			return false;
		return (s->stMode & 0170000) == 0040000;
	}

	absl::StatusOr<std::vector<std::string>> HostPath::Iterdir(Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
		{
			std::vector<std::string> result;
			for (auto& e : std::filesystem::directory_iterator(path))
			{
				result.push_back(e.path().filename().string());
			}
			return result;
		}
		return (*h)->Iterdir(path.string());
	}

	absl::StatusOr<std::vector<std::string>> HostPath::Glob(std::string_view pattern, Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return h.status();
		return (*h)->Glob(pattern, path.string());
	}

	absl::StatusOr<std::string> HostPath::ReadText(Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return h.status();
		return (*h)->ReadText(path.string());
	}

	absl::StatusOr<std::vector<std::string>> HostPath::ReadLines(Host* host, int count) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return h.status();
		return (*h)->ReadLines(path.string(), count);
	}

	absl::Status HostPath::WriteText(std::string_view data, Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return h.status();
		return (*h)->WriteText(path.string(), data);
	}

	absl::Status HostPath::Mkdir(const MkdirOptions& options, Host* host) const
	{
		auto h = ResolveHost(host);
		if (!h.ok())
			return h.status();
		return (*h)->Mkdir(path.string(), options);
	}

} // namespace codeharness::host