# Coding Conventions

## C++ Standard

- C++20 with no extensions (`-std=c++20`)
- Core Guidelines: [https://isocpp.github.io/CppCoreGuidelines/](https://isocpp.github.io/CppCoreGuidelines/)

## Naming

| Kind | Convention | Example |
|------|-----------|---------|
| Namespaces | `snake_case` | `codeharness::host` |
| Classes/Types | `PascalCase` | `LocalHost`, `StatResult` |
| Functions/Methods | `PascalCase` | `ReadText()`, `GetCwd()` |
| Variables | `snake_case` | `shell_path_`, `tmp_dir` |
| Member variables | `trailing_underscore` | `cwd_`, `path_` |
| Files | `snake_case` | `local_host.h`, `host_types.h` |

## Error Handling

Use `absl::Status` and `absl::StatusOr<T>` for all fallible operations.
No custom exception classes. Callers check status with `.ok()` or `ValueOrDie()`.

```cpp
absl::StatusOr<std::string> result = host.ReadText("file.txt");
if (!result.ok()) {
  // handle error via result.status()
}
```

## Resource Management

RAII everywhere. No raw `new`/`delete`. Use `std::unique_ptr` for ownership,
`std::shared_ptr` only when shared ownership is required.

## Headers

- `#pragma once` for all headers
- Include in order: own header → standard → third-party → project
- Minimize includes; forward-declare where possible

## Formatting

Based on Google style with 120-column limit, left-aligned pointers/references.

## Logging

Use `spdlog`:
- `spdlog::debug(...)` — development/diagnostic details
- `spdlog::info(...)` — significant state transitions
- `spdlog::warn(...)` — unexpected but recoverable conditions
- `spdlog::error(...)` — failures

## Testing

Doctest is the unit test framework. Test files mirror the source layout.