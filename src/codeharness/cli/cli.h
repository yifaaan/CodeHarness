#pragma once

#include "codeharness/core/result.h"
#include "codeharness/memory/memory_store.h"
#include "codeharness/prompts/system_prompt.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace codeharness
{

auto load_relevant_memories_for_prompt(
    const memory::MemoryStore& store, std::string_view prompt, std::size_t max_results = 5)
    -> Result<std::vector<RelevantMemory>>;

auto run_cli(int argc, char** argv) -> Result<int>;

} // namespace codeharness
