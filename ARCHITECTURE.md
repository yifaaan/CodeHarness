# Architecture

CodeHarness is now a Go rewrite of a Kimi Code-style coding agent. The current repository is a fresh Go baseline focused on the first foundation module: **Host**.

## Current Status

| Area | Status | Location |
|------|--------|----------|
| CLI entrypoint | Baseline | `cmd/codeharness` |
| Host interface | Implemented | `internal/host` |
| Local host | Implemented | `internal/host/local` |
| Current host binding | Implemented | `internal/host/current` |
| Config, providers, loop, tools, records, sessions, permissions, hooks, skills, MCP, TUI | Planned | `docs/plan/re-build` |

## Host Layer

Host is the bottom layer. Tools and future agent services must use it for filesystem and process work.

```text
future tools / agent services
        |
        v
internal/host
  - Host interface and shared types
  - Process interface
  - Stat and option types
        |
        +--> internal/host/current
        |      context.Context binding helpers
        |
        +--> internal/host/local
               local filesystem, glob, env probe, process spawn
```

Important invariants:

- A `LocalHost` owns its cwd; it never calls `os.Chdir`.
- `Exec` and `ExecWithEnv` spawn direct argv commands, not shell strings.
- `Glob` supports `**`, dotfiles, character classes, optional case-insensitive matching, and symlink cycle protection where stable file identity is available.
- Process output is buffered while the command runs, so callers may read stdout/stderr after `Wait`.
- On Windows, environment detection requires Git Bash, using `KIMI_SHELL_PATH`, `git --exec-path`, or known Git install paths.

## Planned Layering

```text
CLI/TUI
  -> SDK/session
    -> agent core
      -> loop/context/records
        -> tools/permissions/hooks/skills/MCP
          -> Host
          -> provider abstraction
```

The provider abstraction and agent loop are intentionally deferred until the Host layer is stable.

## Commands

```bash
go test ./...
go build ./cmd/codeharness
go run ./cmd/codeharness
```

## References

- `D:\code\kimi-code-upstream` at `1c65cbf`
- `docs/plan/re-build/00-INDEX.md`
- `docs/plan/re-build/references/host-interface.md`
