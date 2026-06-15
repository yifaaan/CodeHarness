#include "Records/AgentRecords.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "Records/RecordPersistence.h"

namespace codeharness::records
{

	namespace
	{

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::system_clock::now().time_since_epoch())
				.count();
		}

	} // namespace

	AgentRecords::AgentRecords(std::unique_ptr<RecordPersistence> persistence)
		: persistence(std::move(persistence))
	{
		if (persistence == nullptr)
		{
			spdlog::warn("AgentRecords constructed with null persistence; Log() will fail");
		}
	}

	AgentRecords::~AgentRecords()
	{
		if (persistence != nullptr)
		{
			(void)persistence->Close();
		}
	}

	absl::Status AgentRecords::Log(AgentRecord record)
	{
		if (restoring)
		{
			return absl::OkStatus(); // replay short-circuit
		}
		if (persistence == nullptr)
		{
			return absl::FailedPreconditionError("AgentRecords has no persistence");
		}

		WireRecord wire;
		wire.meta.ts = NowMs();
		wire.record = std::move(record);
		return persistence->Append(wire);
	}

	absl::StatusOr<std::vector<WireRecord>> AgentRecords::ReadAll()
	{
		if (persistence == nullptr)
		{
			return absl::FailedPreconditionError("AgentRecords has no persistence");
		}
		return persistence->Read();
	}

	bool AgentRecords::IsRestoring() const noexcept
	{
		return restoring;
	}

	absl::Status AgentRecords::Flush()
	{
		if (persistence == nullptr)
			return absl::OkStatus();
		return persistence->Flush();
	}

	void AgentRecords::Close()
	{
		if (persistence != nullptr)
		{
			(void)persistence->Close();
		}
	}

	absl::Status AgentRecords::Replay(const std::function<absl::Status(const AgentRecord&)>& apply)
	{
		if (persistence == nullptr)
		{
			return absl::FailedPreconditionError("AgentRecords has no persistence");
		}

		auto readResult = persistence->Read();
		if (!readResult.ok())
		{
			return readResult.status();
		}

		restoring = true;
		absl::Status lastStatus = absl::OkStatus();
		for (auto& wire : *readResult)
		{
			auto s = apply(wire.record);
			if (!s.ok())
			{
				lastStatus = s;
				break;
			}
		}
		restoring = false;
		return lastStatus;
	}

} // namespace codeharness::records
