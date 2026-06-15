#pragma once

#include <string>
#include <string_view>

namespace codeharness::session
{

	// Encodes an absolute working-directory path into a filesystem-safe key:
	//   <sanitized-prefix>-<fnv1a-64-hex>
	//
	// The sanitized prefix keeps directories debuggable when listing the
	// sessions root (e.g. "D__code_CodeHarness"); the FNV-1a-64 hash suffix
	// disambiguates paths that sanitize identically and avoids filesystem
	// length/illegal-character issues. No external dependency.
	//
	// Example: "D:\\code\\CodeHarness" -> "D__code_CodeHarness-a3f1b2c4d5e6f7a8"
	std::string EncodeWorkdirKey(std::string_view absoluteWorkdir);

} // namespace codeharness::session
