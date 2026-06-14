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
		: persistence_(std::move(persistence))
	{
		if (persistence_ == nullptr)
		{
			spdlog::warn("AgentRecords constructed with null persistence; Log() will fail");
		}
	}

	AgentRecords::~AgentRecords()
	{
		if (persistence_ != nullptr)
		{
			(void)persistence_->Close();
		}
	}

	absl::Status AgentRecords::Log(AgentRecord record)
	{
		if (restoring_)
		{
			return absl::OkStatus(); // replay short-circuit
		}
		if (persistence_ == nullptr)
		{
			return absl::FailedPreconditionError("AgentRecords has no persistence");
		}

		WireRecord wire;
		wire.meta.ts = NowMs();
		wire.record = std::move(record);
		return persistence_->Append(wire);
	}

	absl::StatusOr<std::vector<WireRecord>> AgentRecords::ReadAll()
	{
		if (persistence_ == nullptr)
		{
			return absl::FailedPreconditionError("AgentRecords has no persistence");
		}
		return persistence_->Read();
	}

	bool AgentRecords::IsRestoring() const noexcept
	{
		return restoring_;
	}

	absl::Status AgentRecords::Flush()
	{
		if (persistence_ == nullptr)
			return absl::OkStatus();
		return persistence_->Flush();
	}

	void AgentRecords::Close()
	{
		if (persistence_ != nullptr)
		{
			(void)persistence_->Close();
		}
	}

	absl::Status AgentRecords::Replay(const std::function<absl::Status(const AgentRecord&)>& apply)
	{
		if (persistence_ == nullptr)
		{
			return absl::FailedPreconditionError("AgentRecords has no persistence");
		}

		auto readResult = persistence_->Read();
		if (!readResult.ok())
		{
			return readResult.status();
		}

		restoring_ = true;
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
		restoring_ = false;
		return lastStatus;
	}

} // namespace codeharness::records
