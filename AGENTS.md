# CodeHarness

## Project Description

CodeHarness is a C++20 rewrite of [OpenHarness](https://github.com/HKUDS/OpenHarness) (the `docs/` directory contains detailed design documents). OpenHarness is an agent harness that wraps LLMs into agents capable of working safely — providing model clients, context assembly, a tool system, a permission system, memory, UI, and session management.

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

## Implementation Phases

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

### Phase 6: ohmo and Gateway
- ohmo workspace, MessageBus, Channel adapter
- Gateway runtime pool

## C++ Coding Standards

Follows the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines). Specific requirements:

### Core Principles
- **RAII everywhere**: Resource lifetime is bound to object lifetime
- **Immutability by default**: Prefer `const`/`constexpr`; mutability is the exception
- **Type safety**: Use the type system to prevent errors at compile time
- **Express intent**: Names, types, and concepts should communicate purpose
- **Value semantics**: Prefer value semantics over pointer semantics

### Key Conventions
- `.h` + `.cpp` file structure, `#pragma once`
- `namespace codeharness { ... }`, inner namespaces such as `codeharness::tools`
- `snake_case` naming (functions, variables), `PascalCase` for types
- Class members use `trailing_underscore_`
- `enum class` instead of `enum`, no ALL_CAPS enum values
- Single-argument constructors must be `explicit`
- `nullptr` instead of `0`/`NULL`
- `{}` initialization syntax, avoid narrowing conversions
- `'\n'` instead of `std::endl`
- Base class destructors: `public virtual` or `protected non-virtual`
- Rule of Zero / Rule of Five
- Template parameters constrained by concepts (C++20)
- Locks must use RAII (`scoped_lock`/`lock_guard`)
- Exceptions: throw by value, catch by reference, custom exception types

### Code Style
- `.clang-format` based on Microsoft style, format before committing
- No C-style casts; use `static_cast`/`dynamic_cast`, etc.
- No bare `new`/`delete`; use `make_unique`/`make_shared`
- Functions should be short with a single responsibility

## Design Principles

1. **Avoid reinventing the wheel**: If a third-party library already provides stable functionality (e.g., nlohmann_json for parsing, asio for networking, reproc for process management, spdlog for logging), use it directly rather than building an in-house replacement.
2. **Keep code clear and direct**: Don't pursue over-abstraction. Prefer simple `struct`s + free functions over complex class hierarchies.
3. **Moderate safety**: The permission system must run before tool execution; sensitive paths must not be bypassable by `full_auto`. But avoid overly strict security — don't introduce unnecessary sandboxes, don't over-sanitize input, don't write defensive code beyond runtime type checking. Trust the type system. Trust the chosen third-party libraries.
4. **Unified message model**: All providers convert to the same internal message model; the engine does not depend on any specific provider format.
5. **Event-driven**: The engine emits events rather than directly manipulating the UI. CLI, TUI, JSON output, and tests all consume the same set of events.
6. **Tool failure does not crash**: A tool failure becomes `ToolResultBlock{is_error=true}` returned to the model.
