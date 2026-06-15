#include "Session/SessionStore.h"

#include <chrono>
#include <cstdlib>
#include <random>
#include <string>

#include "Host/Host.h"
#include "Host/HostTypes.h"
#include "Session/WorkdirKey.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace codeharness::session
{

	namespace
	{

		constexpr std::string_view kStateFile = "state.json";
		constexpr std::string_view kStateTmp = "state.json.tmp";
		constexpr std::string_view kIndexFile = "session_index.jsonl";

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::system_clock::now().time_since_epoch())
				.count();
		}

		nlohmann::json MetaToJson(const SessionMeta& m)
		{
			return {
				{"title", m.title},
				{"createdAt", m.createdAt},
				{"updatedAt", m.updatedAt},
				{"workdir", m.workdir},
			};
		}

		absl::StatusOr<SessionMeta> MetaFromJson(const nlohmann::json& j)
		{
			if (!j.is_object())
			{
				return absl::InvalidArgumentError("state.json is not a JSON object");
			}
			SessionMeta m;
			m.title = j.value("title", "");
			m.createdAt = j.value("createdAt", std::int64_t{0});
			m.updatedAt = j.value("updatedAt", std::int64_t{0});
			m.workdir = j.value("workdir", "");
			return m;
		}

		// Treat any failed Stat/Iterdir as "path absent" for read-only session
		// queries. Host::Stat returns NotFound for errno 2 but may surface other
		// codes (InternalError) when an intermediate directory is missing on
		// some platforms; for session lookup that distinction is not useful.
		bool PathMissing(const absl::Status& s)
		{
			return !s.ok();
		}

	} // namespace

	std::string SessionDir::AgentWirePath(std::string_view agentId) const
	{
		return fmt::format("{}/agents/{}/wire.jsonl", path, agentId);
	}

	SessionStore::SessionStore(host::Host* host, std::string sessionsRoot)
		: host_(host), root_(std::move(sessionsRoot))
	{
	}

	absl::StatusOr<std::string> SessionStore::ResolveSessionsRoot(host::Host* host)
	{
		// Mirror ConfigManager::ConfigPath() exactly.
		if (const char* home = std::getenv("CODEHARNESS_HOME"))
		{
			if (home[0] != '\0')
			{
				return fmt::format("{}/sessions", home);
			}
		}
		auto homeStatus = host->GetHome();
		if (!homeStatus.ok())
		{
			return homeStatus.status();
		}
		return fmt::format("{}/.codeharness/sessions", *homeStatus);
	}

	std::string SessionStore::NewSessionId()
	{
		static thread_local std::mt19937_64 rng{std::random_device{}()};
		auto high = static_cast<std::uint32_t>(rng());
		return fmt::format("sess_{:x}_{:06x}", NowMs(), high & 0xffffffU);
	}

	std::string SessionStore::WorkdirRoot(std::string_view workdir) const
	{
		return fmt::format("{}/{}", root_, EncodeWorkdirKey(workdir));
	}

	std::string SessionStore::SessionPath(std::string_view workdir, std::string_view sessionId) const
	{
		return fmt::format("{}/{}", WorkdirRoot(workdir), sessionId);
	}

	absl::StatusOr<SessionDir> SessionStore::Create(std::string_view workdir, std::string_view title)
	{
		auto workdirRoot = WorkdirRoot(workdir);
		auto sessionId = NewSessionId();
		auto sessionPath = fmt::format("{}/{}", workdirRoot, sessionId);

		host::MkdirOptions mkrec{.existOk = true, .recursive = true};
		auto s = host_->Mkdir(workdirRoot, mkrec);
		if (!s.ok())
			return s;

		// Pre-create the main agent dir so wire.jsonl can be appended without a
		// missing-parent error when Records constructs FilePersistence.
		s = host_->Mkdir(fmt::format("{}/agents/main", sessionPath), mkrec);
		if (!s.ok())
			return s;

		SessionMeta meta{
			.title = std::string(title),
			.createdAt = NowMs(),
			.updatedAt = NowMs(),
			.workdir = std::string(workdir),
		};
		s = WriteMeta(sessionPath, meta);
		if (!s.ok())
			return s;

		SessionInfo info{
			.sessionId = sessionId,
			.title = meta.title,
			.workdir = meta.workdir,
			.createdAt = meta.createdAt,
			.updatedAt = meta.updatedAt,
			.agentCount = 1,
		};
		// Index is best-effort: a missing/failed append must not fail Create.
		auto is = AppendIndex(info);
		if (!is.ok())
			spdlog::warn("session: failed to append session_index.jsonl: {}", is.message());

		return SessionDir{.sessionId = std::move(sessionId), .path = std::move(sessionPath)};
	}

	absl::StatusOr<SessionDir> SessionStore::Get(std::string_view workdir, std::string_view sessionId) const
	{
		auto sessionPath = SessionPath(workdir, sessionId);
		auto st = host_->Stat(sessionPath);
		if (!st.ok())
		{
			// Normalize any lookup failure to NotFound: a session either exists
			// on disk or it doesn't, and the caller doesn't care whether the
			// workdir-key parent is missing vs. the session leaf.
			return absl::NotFoundError(fmt::format("session not found: {}/{}", workdir, sessionId));
		}
		return SessionDir{.sessionId = std::string(sessionId), .path = std::move(sessionPath)};
	}

	absl::StatusOr<std::vector<SessionInfo>> SessionStore::List(std::string_view workdir) const
	{
		auto workdirRoot = WorkdirRoot(workdir);
		auto entries = host_->Iterdir(workdirRoot);
		if (!entries.ok())
		{
			// Missing workdir-key dir (no sessions created for this workdir yet)
			// or unreadable dir — both mean "no sessions" for list purposes.
			if (PathMissing(entries.status()))
			{
				return std::vector<SessionInfo>{};
			}
			return entries.status();
		}

		std::vector<SessionInfo> out;
		for (const auto& name : *entries)
		{
			auto sessionPath = fmt::format("{}/{}", workdirRoot, name);
			auto meta = ReadMeta(sessionPath);
			if (!meta.ok())
			{
				spdlog::warn("session: skipping unreadable state.json in {}: {}", sessionPath, meta.status().message());
				continue;
			}
			out.push_back(SessionInfo{
				.sessionId = name,
				.title = meta->title,
				.workdir = meta->workdir,
				.createdAt = meta->createdAt,
				.updatedAt = meta->updatedAt,
				.agentCount = 1,
			});
		}
		return out;
	}

	absl::StatusOr<SessionDir> SessionStore::Find(std::string_view sessionIdPrefix) const
	{
		auto workdirs = host_->Iterdir(root_);
		if (!workdirs.ok())
		{
			// Root not created yet → no sessions exist at all.
			return absl::NotFoundError(fmt::format("no session matches prefix '{}'", sessionIdPrefix));
		}

		std::vector<SessionDir> matches;
		for (const auto& workdirKey : *workdirs)
		{
			auto sessions = host_->Iterdir(fmt::format("{}/{}", root_, workdirKey));
			if (!sessions.ok())
				continue;
			for (const auto& name : *sessions)
			{
				if (name.rfind(sessionIdPrefix, 0) == 0)
				{
					matches.push_back(SessionDir{
						.sessionId = name,
						.path = fmt::format("{}/{}/{}", root_, workdirKey, name),
					});
				}
			}
		}

		if (matches.empty())
		{
			return absl::NotFoundError(fmt::format("no session matches prefix '{}'", sessionIdPrefix));
		}
		if (matches.size() > 1)
		{
			return absl::AlreadyExistsError(fmt::format("session prefix '{}' is ambiguous ({} matches)", sessionIdPrefix, matches.size()));
		}
		return std::move(matches.front());
	}

	absl::Status SessionStore::Remove(std::string_view workdir, std::string_view sessionId)
	{
		auto sessionPath = SessionPath(workdir, sessionId);
		return host_->Remove(sessionPath, {.recursive = true, .existOk = false});
	}

	absl::Status SessionStore::RenameTitle(std::string_view workdir, std::string_view sessionId, std::string_view title)
	{
		auto sessionPath = SessionPath(workdir, sessionId);
		auto meta = ReadMeta(sessionPath);
		if (!meta.ok())
			return meta.status();
		meta->title = std::string(title);
		meta->updatedAt = NowMs();
		return WriteMeta(sessionPath, *meta);
	}

	absl::StatusOr<SessionMeta> SessionStore::ReadMeta(std::string_view sessionPath) const
	{
		auto statePath = fmt::format("{}/{}", sessionPath, kStateFile);
		auto text = host_->ReadText(statePath);
		if (!text.ok())
			return text.status();

		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(*text);
		}
		catch (const nlohmann::json::parse_error& e)
		{
			return absl::InvalidArgumentError(fmt::format("state.json parse error: {}", e.what()));
		}
		return MetaFromJson(j);
	}

	absl::Status SessionStore::WriteMeta(std::string_view sessionPath, const SessionMeta& meta)
	{
		auto statePath = fmt::format("{}/{}", sessionPath, kStateFile);
		auto tmpPath = fmt::format("{}/{}", sessionPath, kStateTmp);
		auto payload = MetaToJson(meta).dump();

		auto s = host_->WriteText(tmpPath, payload);
		if (!s.ok())
			return s;
		// Atomic replace: tmp -> state.json.
		s = host_->Rename(tmpPath, statePath);
		if (!s.ok())
		{
			// Best-effort cleanup of the leftover temp file.
			host_->Remove(tmpPath, {.recursive = false, .existOk = true}).IgnoreError();
		}
		return s;
	}

	absl::Status SessionStore::AppendIndex(const SessionInfo& info)
	{
		auto indexPath = fmt::format("{}/{}", root_, kIndexFile);
		nlohmann::json line = {
			{"sessionId", info.sessionId},
			{"workdir", info.workdir},
			{"title", info.title},
			{"createdAt", info.createdAt},
		};
		auto payload = line.dump() + "\n";
		return host_->AppendText(indexPath, payload);
	}

} // namespace codeharness::session
