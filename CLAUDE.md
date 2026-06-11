# CodeHarness

## Project Description

CodeHarness is a C++20 rewrite of [OpenHarness](https://github.com/HKUDS/OpenHarness) (the `docs/` directory contains detailed design documents). OpenHarness is an agent harness that wraps LLMs into agents capable of working safely -- providing model clients, context assembly, a tool system, a permission system, memory, UI, and session management.

Core execution path:

```
User input
  -> CLI / TUI
  -> RuntimeBundle
  -> QueryEngine.submit_message
  -> run_query agent loop
  -> API client stream_message
  -> Model returns text delta or tool_use
  -> ToolRegistry looks up the tool
  -> Hooks + PermissionChecker
  -> tool.execute
  -> ToolResultBlock is backfilled as a user message
  -> Next model turn continues
```

## Working Principles

- Think from first principles. Start from real requirements, code facts, and verification results; if the goal is unclear, discuss it with the user first.
- Treat code, not documentation, as the source of truth. Unless the user explicitly says otherwise, do not read ordinary Markdown just to understand the implementation.
- Before making code changes, read the relevant code and the most recent constraints, and follow the nearest `AGENTS.md` in the directory tree.
- Keep changes focused. Do not slip in unrelated refactors along the way.
- When committing, do not add any co-author attribution, and do not reveal the identity of the agent in commit messages, PR descriptions, or any explanatory text.

### Source Layout

`src/codeharness/` modules and their CMake dependency order (top depends on bottom):

| Layer | Modules | CMake targets |
|-------|---------|---------------|
| CLI entry | `cli/` | `codeharness_cli` |
| Engine | `engine/` | `codeharness_engine` |
| Extensions | `skills/`, `plugins/`, `tools/` | `codeharness_extensions`, `codeharness_tools` |
| Context | `prompts/`, `memory/`, `commands/` | `codeharness_prompts`, `codeharness_memory`, `codeharness_commands` |
| Multi-agent | `tasks/`, `mailbox/` | `codeharness_tasks`, `codeharness_mailbox` |
| Infrastructure | `provider/`, `mcp/`, `permissions/`, `hooks/` | respective `codeharness_*` |
| Foundation | `core/` | `codeharness_foundation` |

`codeharness_core` is an umbrella `INTERFACE` library linking all modules.

## Implementation Phases

Phases 1-5 have substantial implementations in `src/`. Phase 6 is planned.

### Phase 1: Agent Loop
- CLI: `codeharness -p "prompt"`
- Provider: OpenAI-compatible / Anthropic streaming
- Messages: `TextBlock`, `ToolUseBlock`, `ToolResultBlock`
- Tools: `read_file`, `glob`, `grep`
- Engine: tool call backfill and max turns
- Session: JSON snapshot

### Phase 2: Safe Tool Execution
- `bash`, `write_file`, `edit_file`
- `PermissionChecker`, sensitive path hard deny, default mode confirmation
- Tool output truncation and artifact saving

### Phase 3: Context System
- System prompt builder, AGENTS.md discovery
- Skills loader, memory store, slash commands

### Phase 4: MCP and Plugins
- MCP stdio/http transport, JSON-RPC
- Plugin manifest, skills, commands, MCP config

### Phase 5: Interaction and Multi-Agent
- JSON Lines backend-only protocol, TUI
- TaskManager, subprocess subagent
- Mailbox and team lifecycle

### Phase 6: Coding Agent Refinement
- Runtime assembly for CLI and TUI entrypoints
- Session resume, permission UX, and coding-agent workflow polish

## Build & Test

Requires [vcpkg](https://vcpkg.io/) with `VCPKG_ROOT` set. All dependencies are declared in `vcpkg.json`.

```bash
# Windows (MSVC -- current platform)
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

# Linux
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Test framework: [doctest](https://github.com/doctest/doctest). Tests live in `tests/` as `*_tests.cpp`. The test binary is `codeharness_tests`.

## Coding Conventions

### General Principles
- If a narrower `Project.md` exists for the task, obey it first; otherwise this file is the project-level guide.
- Debug the compiled binary when it crashes, or after one failed speculative fix. Prefer `cmake --preset linux-debug`, `cmake --build --preset linux-debug`, and `ctest --preset linux-debug`, or a focused test/binary run before continuing to guess.
- Prefer crash-early behavior for violated internal invariants. Do not hide impossible states with defensive defaults or silent error tolerance.
- Treat user input, provider failures, file-system failures, permissions, and tool execution errors as expected runtime failures. They must flow through `Result<T>`, events, or `ToolResponse{is_error=true}` as appropriate so the agent loop can continue.
- DRY is about not repeating project knowledge, protocols, parsing rules, permission rules, or data conversions. It is not a mandate to extract tiny helpers that only restate a constructor or a single obvious operation.
- Before implementing a feature, search `src/`, `tests/`, and the relevant docs for an existing implementation. If the existing logic is not shareable, prefer a narrow refactor over duplicating the same knowledge in another place.
- Avoid helper functions used only once unless they remove real complexity or name a meaningful concept. Do not create a helper solely for one destructor.

### Project Scope
- Primary editable code lives in `src/` and `tests/`. Project documentation lives in `docs/`.
- Treat `docs/OpenHarness/` as upstream reference material. Do not modify it unless the task explicitly targets the imported reference project.
- Do not edit `build/`, `.cache/`, generated files, or third-party package sources.
- CodeHarness is C++20 and should remain cross-platform. If OS-specific behavior is necessary, keep the shared interface cross-platform and put the platform-specific implementation in small `*.Windows.cpp` and `*.Linux.cpp` files.

### Core C++ Standards
- Follow the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).
- **RAII everywhere**: resource lifetime is bound to object lifetime.
- **Immutability by default**: prefer `const` and `constexpr`; mutability is the exception.
- **Type safety**: use the type system to prevent errors at compile time.
- **Express intent**: names, types, and concepts should communicate purpose.
- **Value semantics**: prefer value semantics over pointer semantics.

### Header and Source Rules
- Use `.h` + `.cpp` file structure.
- Keep declarations in `namespace codeharness { ... }`, with inner namespaces such as `codeharness::tools` only when they clarify ownership.
- Do not use `using namespace` in headers. Prefer fully qualified names for public declarations and cross-module types.
- Source files may use local `using` declarations or `using namespace` when it removes noisy repetition without obscuring ownership.
- In class, struct, or union declarations, align member names in the same column within the same access section when doing so stays consistent with `.clang-format`.
- Keep style consistent with nearby files when touching existing code.

### Naming and Types
- Use `snake_case` for functions and variables, `PascalCase` for types, and `trailing_underscore_` for class members.
- Use `enum class` instead of unscoped `enum`; enum values are not ALL_CAPS.
- Single-argument constructors must be `explicit`.
- Use `nullptr` instead of `0` or `NULL`.
- Use `{}` initialization and avoid narrowing conversions.
- Use `'\n'` instead of `std::endl`.
- Use `auto` when the initializer makes the type obvious and the exact type is not part of the reader-facing contract.
- Use `auto&&` for range loops over large values or collections when copying is not intended.
- Prefer `std::string_view` for non-owning string inputs, `std::filesystem::path` for paths, `std::optional<T>` for optional values, and the project `Result<T>` alias for recoverable failures.
- Do not use `std::optional<T*>`, nested optionals, or a separate `bool` flag for availability unless null is itself a valid value.

### Resource and Error Handling
- No C-style casts; use `static_cast`, `dynamic_cast`, `const_cast`, or `reinterpret_cast` only when the intent is correct and explicit.
- No bare `new` or `delete`; use scope objects, `std::make_unique`, or `std::make_shared`.
- Base class destructors must be public virtual or protected non-virtual.
- Prefer Rule of Zero. If a type must manually manage a resource, follow Rule of Five.
- Throw exceptions by value and catch by reference when exceptions are the right abstraction. For normal project failures, prefer `Result<T>` with `CodeHarnessError`.
- Permission checks must happen before tool execution. Sensitive path hard-deny behavior must not be bypassable by `full_auto`.
- Tool failure must not crash the harness. Convert it into a model-visible tool result with `is_error=true` through the engine's normal path.

### Library and Parser Usage
- Avoid reinventing stable functionality already provided by project dependencies. Current runtime dependencies include `cli11`, `date`, `expected-lite`, `fmt`, `p-ranav-glob`, `nlohmann-json`, `re2`, `reproc`, `spdlog`, `yaml-cpp`, `libgit2`, and `llhttp`. Test dependency: `doctest`.
- Use existing project helpers before adding another implementation, especially JSON field helpers in `codeharness/core/json_parse.*`, workspace path helpers in `codeharness/tools/workspace_path.*`, tool abstractions, permission targets, and event types.
- Use structured APIs for structured data. For JSON, use `nlohmann/json` and the project helper functions; do not parse JSON with ad hoc string handling or regular expressions.
- Use `re2` for regular expressions when a regex is actually the right tool. Prefer parsers, path APIs, or structured matching when available.
- Avoid namespace-scope globals with non-trivial constructors or destructors. Prefer `constexpr`, `std::string_view`, dependency injection, or explicit initialization/finalization where shared state is unavoidable.

### Templates, Concurrency, and Formatting
- Constrain template parameters with C++20 concepts when the constraint is meaningful.
- Prefer variadic templates over hard-coded arity solutions when it keeps the code simpler.
- Most code does not need thread safety. Do not add locks or atomics unless data is actually shared across threads.
- Locks must use RAII (`std::scoped_lock`, `std::lock_guard`, etc.) and keep critical sections short.
- Use `std::atomic<T>` precisely when required; avoid unnecessary atomics.
- `.clang-format` is the formatting source of truth. Format touched C++ files before committing and avoid unrelated whitespace churn.

## Design Principles

1. **Avoid reinventing the wheel**: If a third-party library already provides stable functionality (e.g., nlohmann/json for parsing, asio for networking, reproc for process management, spdlog for logging), use it directly rather than building an in-house replacement.
2. **Keep code clear and direct**: Don't pursue over-abstraction. Prefer simple `struct`s + free functions over complex class hierarchies.
3. **Moderate safety**: The permission system must run before tool execution; sensitive paths must not be bypassable by `full_auto`. But avoid overly strict security -- don't introduce unnecessary sandboxes, don't over-sanitize input, don't write defensive code beyond runtime type checking. Trust the type system. Trust the chosen third-party libraries.
4. **Unified message model**: All providers convert to the same internal message model; the engine does not depend on any specific provider format.
5. **Event-driven**: The engine emits events rather than directly manipulating the UI. CLI, TUI, JSON output, and tests all consume the same set of events.
6. **Tool failure does not crash**: A tool failure becomes `ToolResultBlock{is_error=true}` returned to the model.
