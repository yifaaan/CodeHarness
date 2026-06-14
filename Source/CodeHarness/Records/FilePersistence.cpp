#include "Records/FilePersistence.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <string>

#include "Records/RecordJson.h"

namespace codeharness::records
{

	FilePersistence::FilePersistence(host::Host* host, std::string path)
		: host_(host), path_(std::move(path))
	{
	}

	absl::Status FilePersistence::Append(const WireRecord& record)
	{
		if (closed_)
		{
			return absl::FailedPreconditionError("FilePersistence is closed");
		}
		if (host_ == nullptr)
		{
			return absl::FailedPreconditionError("FilePersistence has no host");
		}

		auto json = WireRecordToJson(record);
		std::string line = json.dump();
		line.push_back('\n');

		auto status = host_->AppendText(path_, line);
		if (!status.ok())
		{
			spdlog::warn("FilePersistence::Append failed: {}", status.message());
		}
		return status;
	}

	absl::StatusOr<std::vector<WireRecord>> FilePersistence::Read()
	{
		if (closed_)
		{
			return absl::FailedPreconditionError("FilePersistence is closed");
		}
		if (host_ == nullptr)
		{
			return absl::FailedPreconditionError("FilePersistence has no host");
		}

		auto linesResult = host_->ReadLines(path_);
		if (!linesResult.ok())
		{
			// Missing wire.jsonl is treated as empty history (fresh session).
			if (absl::IsNotFound(linesResult.status()))
			{
				return std::vector<WireRecord>{};
			}
			return linesResult.status();
		}

		std::vector<WireRecord> records;
		records.reserve(linesResult->size());

		for (std::size_t i = 0; i < linesResult->size(); ++i)
		{
			const auto& raw = (*linesResult)[i];
			if (raw.empty())
				continue;

			auto parsed = ParseWireRecord(raw);
			if (!parsed.ok())
			{
				return absl::InvalidArgumentError(
					fmt::format("wire.jsonl line {}: {}", i + 1, parsed.status().message()));
			}
			records.push_back(*std::move(parsed));
		}

		return records;
	}

	absl::Status FilePersistence::Flush()
	{
		// Current AppendText implementation is synchronous (no buffering).
		// No-op for now; reserve hook for future buffered writers.
		return absl::OkStatus();
	}

	absl::Status FilePersistence::Close()
	{
		closed_ = true;
		return absl::OkStatus();
	}

	const std::string& FilePersistence::Path() const noexcept
	{
		return path_;
	}

} // namespace codeharness::records
