# Kaos Interface

Filesystem and process execution abstraction layer.

## One-Liner

Kaos decouples the agent from the OS by providing a single interface for file I/O and process execution — works locally or over SSH.

## Core Interface

```typescript
interface Kaos {
  // Path operations (sync)
  pathClass(): 'posix' | 'win32';
  normpath(path: string): string;
  gethome(): string;
  getcwd(): string;

  // Directory operations (async)
  chdir(path: string): Promise<void>;
  stat(path: string): Promise<StatResult>;
  iterdir(path: string): Promise<string[]>;
  glob(pattern: string, path?: string): Promise<string[]>;

  // File operations (async)
  readText(path: string): Promise<string>;
  writeText(path: string, data: string): Promise<void>;
  mkdir(path: string, options?: MkdirOptions): Promise<void>;

  // Process execution (async)
  exec(command: string, options?: ExecOptions): Promise<KaosProcess>;
}
```

## Key Concepts

1. **Per-Instance Working Directory**: Each `Kaos` instance maintains its own `_cwd`, not shared global. Multiple instances can have different working directories.

2. **Cycle Detection in Glob**: Tracks visited filesystem entries using `(dev, ino)` pairs to detect and break symlink cycles.

3. **Two-Phase Kill**: `SIGTERM → wait 5s → SIGKILL` for clean process shutdown.

## Implementations

| Implementation | Purpose |
|---------------|---------|
| `LocalKaos` | Local filesystem and process spawning |
| `SSHKaos` | Remote filesystem via SSH/SFTP |

## Async Context

```typescript
// Get current Kaos from async context
const content = await readText('/path/to/file');
// Delegates to getCurrentKaos().readText()

// Run with specific Kaos instance
await runWithKaos(sshKaos, async () => {
  // All operations use sshKaos
});
```

## Dependencies

- Node.js `fs`, `child_process` for `LocalKaos`
- `ssh2` library for `SSHKaos`

## See Also

- [tool-system.md](tool-system.md) — Tools use Kaos for I/O
- [../design-docs/core-beliefs.md](../design-docs/core-beliefs.md) — Unified abstractions principle
