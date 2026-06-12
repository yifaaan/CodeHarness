# Kaos — Execution Environment Abstraction Layer

**Source**: `packages/kaos/src/`

## Purpose

Kaos ("Kimi Agent Operating System") is the **filesystem and process execution abstraction** that decouples the agent from the underlying operating system. It provides a single interface for reading/writing files, executing shell commands, and manipulating paths — whether the target is the local machine or a remote SSH host.

Every tool in the agent that touches the filesystem or runs a process does so through the Kaos interface. This makes the entire tool system testable (by injecting a mock Kaos) and portable (by implementing Kaos for different environments).

## Core Interface

### Kaos Interface

```
packages/kaos/src/kaos.ts
```

```typescript
interface Kaos {
  // --- Path operations (synchronous) ---
  pathClass(): 'posix' | 'win32';
  normpath(path: string): string;
  gethome(): string;
  getcwd(): string;

  // --- Directory operations (async) ---
  chdir(path: string): Promise<void>;
  stat(path: string, followSymlinks?: boolean): Promise<StatResult>;
  iterdir(path: string): Promise<string[]>;        // list directory entries
  glob(pattern: string, path?: string, options?: GlobOptions): Promise<string[]>;

  // --- File operations (async) ---
  readBytes(path: string): Promise<Uint8Array>;
  readText(path: string): Promise<string>;
  readLines(path: string, count?: number): Promise<string[]>;
  writeBytes(path: string, data: Uint8Array): Promise<void>;
  writeText(path: string, data: string): Promise<void>;
  mkdir(path: string, options?: MkdirOptions): Promise<void>;

  // --- Process execution (async) ---
  exec(command: string, options?: ExecOptions): Promise<KaosProcess>;
  execWithEnv(args: string[], env?: Record<string, string>): Promise<KaosProcess>;
}
```

### StatResult

Mirrors Python's `os.stat_result`:

```typescript
interface StatResult {
  stMode: number;    // File mode (permissions)
  stIno: number;     // Inode number
  stDev: number;     // Device number
  stNlink: number;   // Number of hard links
  stUid: number;     // User ID of owner
  stGid: number;     // Group ID of owner
  stSize: number;    // Size in bytes
  stAtime: number;   // Access time
  stMtime: number;   // Modify time
  stCtime: number;   // Change time (metadata)
}
```

### KaosProcess

```typescript
interface KaosProcess {
  stdin: WritableStream<Uint8Array>;
  stdout: ReadableStream<Uint8Array>;
  stderr: ReadableStream<Uint8Array>;
  pid: number | null;
  exitCode: number | null;     // null while running
  wait(): Promise<number>;     // resolves to exit code
  kill(signal?: string): void; // default SIGTERM
}
```

### GlobOptions

```typescript
interface GlobOptions {
  cwd?: string;
  includeDirs?: boolean;      // default true
  maxDepth?: number;
  signal?: AbortSignal;
}
```

### MkdirOptions

```typescript
interface MkdirOptions {
  existOk?: boolean;          // default true (don't error if exists)
  recursive?: boolean;        // default false (like mkdir -p)
}
```

## Implementations

### LocalKaos

**Source**: `packages/kaos/src/local.ts`

The primary implementation that directly interfaces with the local filesystem and process spawner.

**Key Architectural Decisions**:

1. **Per-instance working directory**: Each `LocalKaos` instance maintains its own `_cwd` field rather than mutating `process.cwd()`. This allows multiple instances with different working directories to coexist. The `_cwd` is initialized from `process.cwd()` but subsequent `chdir()` calls only affect that instance.

2. **Glob with cycle detection**: The `glob()` implementation performs a recursive directory walk. It tracks visited filesystem entries using `(dev, ino)` pairs (device number + inode number) to detect and break symlink cycles:

    ```
    function glob(pattern, rootPath):
      visited = Set<(dev, ino)>  — cycle detection
      results = []
      
      def walk(dir):
        for entry in readdir(dir):
          stat = lstat(entry)
          if isSymlink(stat):
            key = (stat.dev, stat.ino)
            if key in visited → skip (cycle!)
            visited.add(key)
            target = readlink(entry)
            stat = stat(target)  — follow symlink
          if matches(entry.name, pattern):
            results.append(entry.path)
          if isDirectory(stat):
            walk(entry.path)
      
      walk(rootPath)
      return results.sort(by mtime descending)
    ```

3. **Process spawning**: Uses Node.js `child_process.spawn()` with:
   - POSIX: `detached: true`, `setsid: true` → process group leader, so `kill(-pid)` kills the entire tree
   - Windows: special handling via `taskkill /T /F` for tree kill
   - `stdio: ['pipe', 'pipe', 'pipe']` for stdin/stdout/stderr capture

4. **Two-phase kill**:
   ```
   kill(signal) → SIGTERM → wait 5s → SIGKILL
   ```
   The initial kill sends SIGTERM (or the requested signal). After a 5-second grace period, if the process hasn't exited, SIGKILL is sent. This gives processes a chance to clean up.

5. **Exec environment**: Sets up the default shell:
   - Unix: `/bin/sh -c <command>`
   - Windows: `git-bash` (configured Kaos path) or `cmd.exe /c <command>`
   - `execWithEnv()`: allows passing custom environment variables (merged with inherited)

### SSH Implementation

**Source**: `packages/kaos/src/ssh.ts`

A secondary implementation that wraps the `ssh2` library. It communicates with a remote machine via SSH:

```typescript
interface SSHKaosOptions {
  host: string;
  port?: number;         // default 22
  username?: string;
  privateKey?: string;
  password?: string;
}
```

`SSHKaos` implements the same `Kaos` interface but:
- `exec()` runs commands remotely via SSH
- File operations use SFTP
- Path operations use the remote machine's path conventions

## Async Context Management

**Source**: `packages/kaos/src/current.ts`

Uses Node.js `AsyncLocalStorage` to provide a "current Kaos" instance without explicit parameter passing:

```typescript
function getCurrentKaos(): Kaos;     // returns current context or default LocalKaos
function runWithKaos<T>(kaos: Kaos, fn: () => T): Promise<T>;  // scoped
function setCurrentKaos(kaos: Kaos): void;
function resetCurrentKaos(): void;
```

This is inspired by Python's `contextvars` pattern. Module-level convenience functions delegate to the current Kaos:

```typescript
// In packages/kaos/src/index.ts:
async function readText(path: string): Promise<string> {
  return getCurrentKaos().readText(path);
}

async function exec(command: string, options?: ExecOptions): Promise<KaosProcess> {
  return getCurrentKaos().exec(command, options);
}
// ... etc for writeText, readLines, glob, mkdir, stat, etc.
```

**Pattern**: This allows any code that imports `kaos` to call `readText()` without explicitly passing a Kaos instance — it inherits the instance from the async call context. When a specific instance is needed (e.g., SSH), wrap the operation in `runWithKaos(sshKaos, op)`.

## KaosPath Utility

**Source**: `packages/kaos/src/path.ts`

A Python pathlib-like class for path manipulation:

```typescript
class KaosPath {
  constructor(path: string, pathClass: 'posix' | 'win32');
  
  // Properties
  get name(): string;           // basename
  get parent(): KaosPath;       // parent directory
  
  // Operations
  isAbsolute(): boolean;
  joinpath(...parts: string[]): KaosPath;
  div(other: string): KaosPath;            // path / "subdir"
  canonical(): KaosPath;                   // resolved absolute path
  relativeTo(other: KaosPath): KaosPath;
  expanduser(): KaosPath;
  
  // Delegation (receive Kaos instance or use current)
  stat(kaos?: Kaos): Promise<StatResult>;
  exists(kaos?: Kaos): Promise<boolean>;
  isFile(kaos?: Kaos): Promise<boolean>;
  isDir(kaos?: Kaos): Promise<boolean>;
  iterdir(kaos?: Kaos): Promise<string[]>;
  glob(pattern: string, kaos?: Kaos): Promise<string[]>;
  readBytes(kaos?: Kaos): Promise<Uint8Array>;
  readText(kaos?: Kaos): Promise<string>;
  readLines(kaos?: Kaos): Promise<string[]>;
  writeBytes(data: Uint8Array, kaos?: Kaos): Promise<void>;
  writeText(data: string, kaos?: Kaos): Promise<void>;
  appendText(data: string, kaos?: Kaos): Promise<void>;
  mkdir(options?: MkdirOptions, kaos?: Kaos): Promise<void>;
}
```

## Error Handling

```typescript
class KaosFileExistsError extends Error {
  constructor(path: string);
  path: string;
}
```

Thrown when `mkdir` is called with `existOk: false` and the directory already exists.

## Data Flow

```
┌─────────────────────────────────────────────────────────┐
│  Tool implementation (e.g., ReadTool, BashTool)         │
│                                                          │
│  import { readText, exec } from '@moonshot-ai/kaos'     │
│                                                          │
│  async function execute() {                              │
│    const content = await readText('/path/to/file');      │
│    const proc = await exec('npm test');                  │
│    // ...                                                │
│  }                                                       │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│  getCurrentKaos() resolves to:                          │
│                                                          │
│  If inside runWithKaos(sshKaos, ...) ──> SSHKaos        │
│  Otherwise ──────────────────────────> LocalKaos         │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│  LocalKaos implementation:                              │
│                                                          │
│  readText(path) → fs.promises.readFile(path, 'utf-8')  │
│  exec(cmd) → child_process.spawn(shell, ['-c', cmd])   │
│  glob(pat, cwd) → recursive walk + (dev,ino) tracking  │
└─────────────────────────────────────────────────────────┘
```

## Design Patterns

| Pattern | Usage |
|---------|-------|
| **Adapter** | LocalKaos and SSHKaos both implement the Kaos interface, adapting platform-specific APIs |
| **Context Manager** | AsyncLocalStorage-based "current instance" pattern (like Python's contextvars) |
| **Proxy** | Module-level convenience functions delegate to the current Kaos instance transparently |

## Re-implementation Notes

1. **Port priority**: Implement the `Kaos` interface with `LocalKaos` first. It only needs:
   - File read/write (your language's `fs` module)
   - Process spawn (`subprocess` / `Command`)
   - Glob (recursive directory walk)
   - Path operations

2. **The `(dev, ino)` cycle detection** in glob is crucial. It prevents infinite loops from symlink cycles. Without it, `find . -type f` patterns can hang. Your glob implementation must:
   - Use `lstat` (not `stat`) to detect symlinks
   - Track the device + inode of each visited symlink target
   - Skip already-visited symlink targets

3. **Process group management**: When spawning processes, use the platform's mechanism to create a process group. On Unix: `setid` / `detached` + `kill(-pid)`. On Windows: `taskkill /T`. The two-phase kill (SIGTERM → 5s → SIGKILL) is important for clean shutdown.

4. **Multiple Kaos instances**: The per-instance `_cwd` (not shared global `process.cwd()`) design must be preserved. Different operations can run in different directories simultaneously.

5. **SSH is optional**: For an initial reimplementation, `LocalKaos` is sufficient. Add SSHKaos later as a transport layer over SSH/SFTP.

6. **File encoding**: All text operations assume UTF-8. Binary operations use byte arrays (`Uint8Array` equivalent).
