#include "Session/Session.h"

#include <chrono>
#include <utility>

#include "Agent/Agent.h"
#include "Agent/AgentTypes.h"
#include "Host/Host.h"
#include "Llm/ChatProvider.h"
#include "Records/AgentRecords.h"
#include "Records/FilePersistence.h"
#include "Session/SessionStore.h"
#include "Tools/ToolManager.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace codeharness::session
{

	namespace
	{

		constexpr std::string_view kMainAgent = "main";

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::system_clock::now().time_since_epoch())
				.count();
		}

	} // namespace

	Session::Session(SessionStore* store, std::string sessionId, std::string sessionPath, SessionMeta meta)
		: store(store), sessionId(std::move(sessionId)), sessionPath(std::move(sessionPath)), meta(std::move(meta))
	{
	}

	Session::~Session()
	{
		if (!closed)
		{
			auto s = Close();
			if (!s.ok())
			{
				spdlog::warn("session: best-effort Close in destructor failed: {}", s.message());
			}
		}
	}

	absl::Status Session::WireMainAgent(SessionConfig cfg, bool replay)
	{
		auto wirePath = fmt::format("{}/agents/{}/wire.jsonl", sessionPath, kMainAgent);

		// Records owns its persistence backend; Session owns Records.
		auto persistence = std::make_unique<records::FilePersistence>(cfg.host, wirePath);
		records = std::make_unique<records::AgentRecords>(std::move(persistence));

		agent = std::make_unique<agent::Agent>(cfg.provider, cfg.host, cfg.toolManager, agent::AgentConfig{});
		// Wire Records as the live event sink for subsequent turns.
		agent->SetRecords(records.get());

		if (replay)
		{
			// Agent::Resume reads the wire stream (via Records) and rebuilds
			// in-memory history. Records' restoring_ guard prevents re-recording
			// the events being replayed.
			auto s = agent->Resume();
			if (!s.ok())
			{
				return absl::InternalError(fmt::format("session: resume replay failed: {}", s.message()));
			}
		}

		return absl::OkStatus();
	}

	absl::StatusOr<std::unique_ptr<Session>> Session::Create(SessionStore* store, SessionConfig cfg)
	{
		if (store == nullptr)
		{
			return absl::InvalidArgumentError("Session::Create requires a SessionStore");
		}
		if (cfg.host == nullptr || cfg.provider == nullptr)
		{
			return absl::InvalidArgumentError("Session::Create requires host and provider");
		}

		auto dir = store->Create(cfg.workdir, cfg.title);
		if (!dir.ok())
		{
			return dir.status();
		}

		auto meta = store->ReadMeta(dir->path);
		if (!meta.ok())
		{
			return meta.status();
		}

		auto session = std::unique_ptr<Session>(new Session(store, dir->sessionId, dir->path, *meta));

		auto s = session->WireMainAgent(cfg, /*replay=*/false);
		if (!s.ok())
		{
			return s;
		}
		return session;
	}

	absl::StatusOr<std::unique_ptr<Session>> Session::Resume(SessionStore* store, SessionConfig cfg, std::string_view sessionId)
	{
		if (store == nullptr)
		{
			return absl::InvalidArgumentError("Session::Resume requires a SessionStore");
		}
		if (cfg.host == nullptr || cfg.provider == nullptr)
		{
			return absl::InvalidArgumentError("Session::Resume requires host and provider");
		}

		// Find resolves a unique prefix to a full SessionDir.
		auto dir = store->Find(sessionId);
		if (!dir.ok())
		{
			return dir.status();
		}

		auto meta = store->ReadMeta(dir->path);
		if (!meta.ok())
		{
			return meta.status();
		}

		auto session = std::unique_ptr<Session>(new Session(store, dir->sessionId, dir->path, *meta));

		auto s = session->WireMainAgent(cfg, /*replay=*/true);
		if (!s.ok())
		{
			return s;
		}
		return session;
	}

	absl::Status Session::Close()
	{
		if (closed)
		{
			return absl::OkStatus();
		}
		closed = true;

		if (records)
		{
			auto s = records->Flush();
			if (!s.ok())
			{
				spdlog::warn("session: records flush on close failed: {}", s.message());
			}
		}

		// Persist updated metadata (updatedAt). Best-effort.
		meta.updatedAt = NowMs();
		if (store)
		{
			auto s = store->WriteMeta(sessionPath, meta);
			if (!s.ok())
			{
				spdlog::warn("session: state.json write on close failed: {}", s.message());
				return s;
			}
		}
		return absl::OkStatus();
	}

} // namespace codeharness::session
