#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <string>
#include <string_view>

#include "Config/Config.h"

namespace codeharness::host
{
	class Host;
}

namespace codeharness::config
{

	// Loads, validates, and persists `KimiConfig` from TOML. All filesystem
	// access goes through the injected `Host*`, so the manager is fully testable
	// with an in-memory/fake host and never touches the disk directly.
	//
	// Config home resolution order:
	//   1. `$CODEHARNESS_HOME/config.toml` (if the env var is set)
	//   2. `$HOME/.codeharness/config.toml`
	class ConfigManager
	{
	public:
		explicit ConfigManager(host::Host* host);

		// Resolve the config file path that `Load`/`Save` target.
		absl::StatusOr<std::string> ConfigPath() const;

		// Load and validate config from the resolved config path. Returns a
		// default (empty) `KimiConfig` when the file does not exist — first run.
		absl::StatusOr<KimiConfig> Load();

		// Parse config from an in-memory TOML string. Mainly for tests; also
		// used by `Load` after the file is read.
		absl::StatusOr<KimiConfig> LoadFromString(std::string_view toml) const;

		// Validate cross-references (model -> provider, default model exists)
		// and credential presence. Returns a failed-status describing the first
		// blocking problem; non-blocking problems are warnings (logged only).
		absl::Status Validate(const KimiConfig& config) const;

		// Serialize `config` to TOML and write it to the resolved config path,
		// creating parent directories as needed.
		absl::Status Save(const KimiConfig& config) const;

	private:
		host::Host* host;
	};

} // namespace codeharness::config
