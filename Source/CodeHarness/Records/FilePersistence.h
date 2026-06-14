#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "Host/Host.h"
#include "Records/RecordPersistence.h"

namespace codeharness::records
{

	// Host-backed append-only wire.jsonl persistence.
	//
	// Each Append writes one JSON line to `path` via Host::AppendText.
	// Read parses each non-blank line via ParseWireRecord. Lines that fail to
	// parse are returned as errors (no silent skip — preserves replay fidelity).
	class FilePersistence final : public RecordPersistence
	{
	public:
		FilePersistence(host::Host* host, std::string path);

		absl::Status Append(const WireRecord& record) override;
		absl::StatusOr<std::vector<WireRecord>> Read() override;
		absl::Status Flush() override;
		absl::Status Close() override;

		const std::string& Path() const noexcept;

	private:
		host::Host* host_ = nullptr;
		std::string path_;
		bool closed_ = false;
	};

} // namespace codeharness::records
