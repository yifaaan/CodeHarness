# 第2章：Host 层深度剖析

> Host 是最底层，所有文件系统和进程操作都通过它抽象。

## 1. 为什么需要 Host 抽象？

### 1.1 问题场景

想象你正在实现一个 Agent，它需要：
- 读取文件：`ReadFile("src/main.cpp")`
- 执行命令：`Bash("npm test")`
- 搜索文件：`Glob("**/*.cpp")`

**直接调用操作系统 API**：
```cpp
// 读取文件
std::ifstream file("src/main.cpp");
std::string content((std::istreambuf_iterator<char>(file)), ...);

// 执行命令
system("npm test");  // 或 popen、CreateProcess...

// 搜索文件
// 需要自己递归遍历目录...
```

**问题**：
1. **测试困难**：测试时需要真实文件、真实进程，无法 Mock
2. **平台差异**：Windows vs Linux 的文件路径、进程 API 不同
3. **状态耦合**：直接调用 `chdir()` 会改变进程状态，影响其他代码
4. **扩展困难**：想支持 SSH 远程执行？需要改动大量代码

### 1.2 Host 抽象的价值

**抽象层的作用**：
```
Tool 层（ReadFile, Bash, Glob）
        ↓
    Host 接口（抽象）
        ↓
┌───────────────┬───────────────┐
│   LocalHost   │   SSHHost     │  ← 不同实现
│  (本地执行)    │  (远程执行)    │
└───────────────┴───────────────┘
        ↓               ↓
   本地文件系统      远程 SSH 服务器
```

**好处**：
1. **可测试**：注入 MockHost，无需真实文件
2. **平台统一**：LocalHost 内部处理平台差异
3. **状态隔离**：每个 LocalHost 有自己的 cwd，不改变进程状态
4. **可扩展**：新增 SSHHost，Tool 层代码不变

## 2. Host 接口详解

### 2.1 接口定义

**位置**：`Source/CodeHarness/Host/Host.h`

```cpp
class Host
{
public:
    virtual ~Host() = default;

    // ==================== 路径操作 ====================
    
    // 获取路径工具类名（如 "HostPath"）
    virtual std::string PathClass() const = 0;
    
    // 规范化路径（去除 ./、../ 等）
    virtual absl::StatusOr<std::string> Normpath(std::string_view path) const = 0;
    
    // 获取用户主目录
    virtual absl::StatusOr<std::string> GetHome() const = 0;
    
    // 获取当前工作目录
    virtual absl::StatusOr<std::string> GetCwd() const = 0;
    
    // 切换工作目录（注意：只改变 Host 内部状态，不改变进程 cwd）
    virtual absl::Status Chdir(std::string_view path) = 0;

    // ==================== 文件信息 ====================
    
    // 获取文件状态（大小、权限、时间等）
    // followSymlinks: 是否跟随符号链接
    virtual absl::StatusOr<StatResult> Stat(std::string_view path, bool followSymlinks = true) = 0;
    
    // 列出目录内容（返回文件名列表）
    virtual absl::StatusOr<std::vector<std::string>> Iterdir(std::string_view path) = 0;
    
    // 文件搜索（支持通配符 **、*、? 等）
    virtual absl::StatusOr<std::vector<std::string>> Glob(std::string_view pattern, std::string_view path = "", const GlobOptions& options = {}) = 0;

    // ==================== 文件读写 ====================
    
    // 读取二进制内容
    virtual absl::StatusOr<std::vector<uint8_t>> ReadBytes(std::string_view path) = 0;
    
    // 读取文本内容
    virtual absl::StatusOr<std::string> ReadText(std::string_view path) = 0;
    
    // 按行读取（count=0 表示全部，>0 表示最多 count 行）
    virtual absl::StatusOr<std::vector<std::string>> ReadLines(std::string_view path, int count = 0) = 0;
    
    // 写入二进制内容
    virtual absl::Status WriteBytes(std::string_view path, std::span<const uint8_t> data) = 0;
    
    // 写入文本内容
    virtual absl::Status WriteText(std::string_view path, std::string_view data) = 0;
    
    // 追加文本内容（用于事件溯源的 wire.jsonl）
    virtual absl::Status AppendText(std::string_view path, std::string_view data) = 0;
    
    // 创建目录
    virtual absl::Status Mkdir(std::string_view path, const MkdirOptions& options = {}) = 0;
    
    // 删除文件/目录
    virtual absl::Status Remove(std::string_view path, const RemoveOptions& options = {}) = 0;
    
    // 重命名/移动文件
    virtual absl::Status Rename(std::string_view from, std::string_view to) = 0;

    // ==================== 进程操作 ====================
    
    // 执行命令（shell 命令字符串）
    virtual absl::StatusOr<std::unique_ptr<HostProcess>> Exec(std::string_view command, std::string_view cwd = "") = 0;
    
    // 执行命令（参数列表 + 环境变量）
    virtual absl::StatusOr<std::unique_ptr<HostProcess>> ExecWithEnv(
        std::vector<std::string> args,      // 命令参数（第一个是程序路径）
        std::string_view cwd = "",          // 工作目录
        const std::vector<std::pair<std::string, std::string>>& env = {}  // 环境变量
    ) = 0;
};
```

### 2.2 关键设计点

#### 2.2.1 工作目录隔离

**重要**：`LocalHost` 有自己的 cwd，**不调用进程的 `chdir()`**

```cpp
// ❌ 错误理解
Chdir("/tmp")  // 这会改变整个进程的 cwd！

// ✅ 正确理解
LocalHost host("/home/user/project");  // host 内部记录 cwd
host.Chdir("/tmp");  // 只改变 host 内部状态，进程 cwd 不变
host.GetCwd();  // 返回 "/tmp"，但其他代码不受影响
```

**为什么重要**：
- 多个 Host 实例可以有不同 cwd
- 测试时不会污染全局状态
- 并发安全

#### 2.2.2 命令执行方式

**两种方式**：

```cpp
// 方式1：Shell 命令字符串
Exec("ls -la | grep test")  // 通过 shell 执行，支持管道等

// 方式2：直接参数列表（推荐）
ExecWithEnv({"ls", "-la"}, "/home/user", {"PATH=/usr/bin"})
// 直接执行程序，不通过 shell，更安全
```

**推荐使用 ExecWithEnv**：
- 避免 shell 注入风险
- 更高效（不启动 shell）
- 环境变量可控

#### 2.2.3 文件搜索 Glob

```cpp
// 支持 ** 递归
Glob("**/*.cpp")  // 搜索所有 cpp 文件

// 支持 *、?、[] 通配符
Glob("src/*.cpp")
Glob("test/test_?.cpp")
Glob("src/[abc]*.cpp")

// 选项
GlobOptions options{
    .cwd = "/home/user/project",  // 搜索起始目录
    .includeDirs = true,          // 是否包含目录
    .maxDepth = 10                // 最大递归深度
};
```

## 3. HostProcess 接口详解

### 3.1 接口定义

**位置**：`Source/CodeHarness/Host/HostProcess.h`

```cpp
// 进程输出 Drain 结果
struct DrainResult
{
    std::string out;      // stdout 内容
    std::string err;      // stderr 内容
    int exitCode = -1;    // 退出码（-1 表示未结束）
    bool finished = false;  // 进程是否已结束
    bool timedOut = false;  // 是否超时
};

class HostProcess
{
public:
    virtual ~HostProcess() = default;

    // ==================== 输入 ====================
    
    // 写入 stdin
    virtual absl::Status WriteStdin(std::string_view data) = 0;
    
    // 关闭 stdin（告诉进程输入结束）
    virtual absl::Status CloseStdin() = 0;

    // ==================== 输出 ====================
    
    // 读取 stdout（阻塞，直到有数据）
    virtual absl::StatusOr<std::string> ReadStdout() = 0;
    
    // 读取 stderr
    virtual absl::StatusOr<std::string> ReadStderr() = 0;

    // ==================== 进程控制 ====================
    
    // 获取进程 ID
    virtual absl::StatusOr<int> Pid() const = 0;
    
    // 获取退出码（如果进程已结束）
    virtual absl::StatusOr<int> ExitCode() const = 0;
    
    // 等待进程结束（阻塞）
    virtual absl::StatusOr<int> Wait() = 0;
    
    // 发送信号终止进程
    virtual absl::Status Kill(const std::string& signal = "SIGTERM") = 0;

    // ==================== 综合操作 ====================
    
    // Drain: 同时读取 stdout/stderr + 等待结束
    // 解决管道缓冲区死锁问题
    // 
    // 参数：
    //   timeoutMs: 超时时间（<=0 表示无限）
    //   stopToken: 取消令牌
    // 
    // 返回：
    //   DrainResult: 输出内容 + 进程状态
    virtual absl::StatusOr<DrainResult> Drain(int timeoutMs, std::stop_token stopToken) = 0;
};
```

### 3.2 管道缓冲区死锁问题

**问题背景**：

```
进程 stdout → 管道（缓冲区有限，通常 64KB）→ 读取端

如果：
  1. 进程输出大量数据（超过 64KB）
  2. 读取端没有及时读取
  3. 管道缓冲区满了
  4. 进程阻塞在 write()
  5. 进程永远无法结束
  6. 读取端等待进程结束
  7. 死锁！
```

**解决方案**：`Drain()` 同时读取 stdout 和 stderr

```cpp
// BashTool 中使用 Drain（Tools/Bash.cpp）
auto drainResult = process->Drain(timeoutMs, ctx.stopToken);

if (!drainResult.ok()) {
    return {.content = "drain failed", .isError = true};
}

auto& result = *drainResult;
if (!result.finished) {
    // 超时或取消，杀死进程
    process->Kill("SIGTERM");
    // 等待 5 秒后 SIGKILL（两阶段终止）
}

// 返回输出（截断后）
return {.content = TruncateOutput(result.out + result.err), .isError = result.exitCode != 0};
```

### 3.3 两阶段终止

```cpp
// 当进程需要终止时
process->Kill("SIGTERM");  // 先发送 SIGTERM（友好终止）

// 等待 5 秒
sleep(5000ms);

if (process 还在运行) {
    process->Kill("SIGKILL");  // 强制终止
}
```

**为什么两阶段**：
- SIGTERM：进程可以优雅退出（保存状态、关闭连接）
- SIGKILL：强制终止，进程无法拒绝
- 先友好，后强制，兼顾效率和优雅

## 4. LocalHost 实现分析

### 4.1 类定义

**位置**：`Source/CodeHarness/Host/LocalHost.h`

```cpp
class LocalHost : public Host
{
public:
    // 构造：指定初始 cwd
    explicit LocalHost(std::string_view cwd = "");
    
private:
    // 内部工作目录（不改变进程 cwd）
    std::filesystem::path cwd;
    
    // Shell 路径（Windows 上需要 Git Bash）
    std::string shellPath;
    std::string shellName;
    
    // 路径解析：相对于 cwd 解析
    std::filesystem::path ResolvePath(std::string_view path) const;
};
```

### 4.2 关键实现细节

#### 4.2.1 路径解析

```cpp
// ResolvePath 实现
std::filesystem::path LocalHost::ResolvePath(std::string_view path) const
{
    std::filesystem::path p(path);
    
    if (p.is_relative()) {
        // 相对路径：相对于 cwd 解析
        return cwd / p;
    } else {
        // 绝对路径：直接返回
        return p;
    }
}
```

#### 4.2.2 Windows Shell 检测

**问题**：Windows 没有 Bash，需要找到 Git Bash

```cpp
// 检测环境（Host/Environment.h）
EnvironmentResult DetectEnvironment();

// 检测顺序：
// 1. $KIMI_SHELL_PATH 环境变量
// 2. git --exec-path 获取 Git 安装路径
// 3. 已知的 Git 安装目录（Program Files）
// 4. PATH 中查找 bash
```

#### 4.2.3 进程启动（reproc++）

```cpp
// 使用 reproc++ 库启动进程
reproc::process process;
reproc::options options;

// 设置工作目录
options.working_directory = cwd.c_str();

// 设置环境变量
options.env = env_pairs;

// 启动
auto error = process.start(args, options);
```

## 5. 类型定义

### 5.1 StatResult

```cpp
struct StatResult
{
    uint32_t stMode = 0;    // 文件类型 + 权限
                            // 可以判断：是否目录、是否文件、权限位
    uint64_t stIno = 0;     // inode 号（唯一标识）
    uint64_t stDev = 0;     // 设备号
    uint16_t stNlink = 0;   // 硬链接数
    uint32_t stUid = 0;     // 用户 ID
    uint32_t stGid = 0;     // 组 ID
    int64_t stSize = 0;     // 文件大小（字节）
    int64_t stAtime = 0;    // 访问时间
    int64_t stMtime = 0;    // 修改时间
    int64_t stCtime = 0;    // 创建/状态改变时间
};
```

**判断文件类型**：
```cpp
bool isDir = (stat.stMode & S_IFDIR) != 0;
bool isFile = (stat.stMode & S_IFREG) != 0;
```

### 5.2 GlobOptions

```cpp
struct GlobOptions
{
    std::filesystem::path cwd;    // 搜索起始目录
    bool includeDirs = true;      // 是否包含目录名
    int maxDepth = -1;            // 最大递归深度（-1 表示无限）
};
```

### 5.3 MkdirOptions

```cpp
struct MkdirOptions
{
    bool existOk = true;     // 目录已存在时不报错
    bool recursive = false;  // 递归创建父目录（mkdir -p）
};
```

### 5.4 RemoveOptions

```cpp
struct RemoveOptions
{
    bool recursive = false;  // 删除非空目录（rm -r）
    bool existOk = true;     // 文件不存在时不报错
};
```

## 6. 测试分析

### 6.1 LocalHostTest.cpp

**测试场景**：
```cpp
// 测试工作目录隔离
TEST(LocalHost, CwdIsolation) {
    LocalHost host1("/tmp");
    LocalHost host2("/home");
    
    // 两个 host 有不同 cwd
    EXPECT_EQ(host1.GetCwd(), "/tmp");
    EXPECT_EQ(host2.GetCwd(), "/home");
    
    // 改变 host1 不影响 host2
    host1.Chdir("/var");
    EXPECT_EQ(host2.GetCwd(), "/home");  // host2 不变
}

// 测试文件读写
TEST(LocalHost, FileOperations) {
    LocalHost host;
    
    host.WriteText("test.txt", "hello");
    EXPECT_EQ(host.ReadText("test.txt"), "hello");
    
    host.AppendText("test.txt", " world");
    EXPECT_EQ(host.ReadText("test.txt"), "hello world");
    
    host.Remove("test.txt");
}

// 测试进程执行
TEST(LocalHost, ProcessExec) {
    LocalHost host;
    
    auto proc = host.Exec("echo hello");
    auto result = proc->Drain(5000, {});
    
    EXPECT_TRUE(result->finished);
    EXPECT_EQ(result->exitCode, 0);
    EXPECT_EQ(result->out, "hello\n");
}
```

### 6.2 Mock Host

**测试时注入 Mock**：
```cpp
class MockHost : public Host {
public:
    MOCK_METHOD(std::string, GetCwd, (), (override));
    MOCK_METHOD(absl::StatusOr<std::string>, ReadText, (std::string_view), (override));
    MOCK_METHOD(absl::StatusOr<std::unique_ptr<HostProcess>>, Exec, (std::string_view, std::string_view), (override));
    // ...
};

// 在 Tool 测试中使用
TEST(ReadFileTool, MockHost) {
    MockHost mock;
    EXPECT_CALL(mock, ReadText("test.txt"))
        .WillOnce(Return("mocked content"));
    
    ReadFileTool tool;
    ToolContext ctx{.host = &mock};
    
    auto result = tool.Execute({{"path", "test.txt"}}, ctx);
    EXPECT_EQ(result->content, "mocked content");
}
```

## 7. 类关系图

```
┌───────────────────┐
│      Host         │  ← 抽象接口
│   (interface)     │
└─────────┬─────────┘
          │
          │ implements
          │
┌─────────┴─────────┐
│    LocalHost      │  ← 本地实现
│                   │
│  cwd: path        │  ← 内部工作目录
│  shellPath: str   │  ← Shell 路径
│                   │
│  ResolvePath()    │  ← 路径解析
│  ReadText()       │
│  WriteText()      │
│  Exec()           │
│  ...              │
└─────────┬─────────┘
          │
          │ creates
          │
┌─────────┴─────────┐
│   HostProcess     │  ← 进程抽象
│   (interface)     │
│                   │
│  WriteStdin()     │
│  ReadStdout()     │
│  ReadStderr()     │
│  Wait()           │
│  Kill()           │
│  Drain()          │  ← 核心方法
└───────────────────┘
```

## 8. 与其他模块的关系

```
Host 被以下模块使用：

Tools 层：
  BashTool → Host.Exec()
  ReadFileTool → Host.ReadText()
  WriteFileTool → Host.WriteText()
  GlobTool → Host.Glob()

Engine 层：
  ToolContext.host → 传给工具的 Host

Agent 层：
  Agent → 持有 Host，传给 TurnInput

Session 层：
  Session → 使用 Host 进行文件持久化
```

## 9. 小结

本章我们学习了：

- **为什么需要 Host 抽象**：测试、平台统一、状态隔离、扩展性
- **Host 接口**：路径、文件、进程三大类操作
- **HostProcess 接口**：进程控制、Drain 解决死锁
- **LocalHost 实现**：cwd 隔离、Windows Shell 检测、reproc++ 进程启动
- **类型定义**：StatResult、GlobOptions、MkdirOptions、RemoveOptions
- **测试方法**：MockHost 注入

## 10. 练习建议

1. **阅读源码**：打开 `Host/LocalHost.cpp`，看一个具体方法实现
2. **写测试**：尝试写一个测试用例，验证 Chdir 不改变进程 cwd
3. **思考题**：如果要实现 SSHHost，哪些方法需要改？哪些可以直接复用？

## 11. 下一步

下一章我们将深入 **LLM 层**，理解如何统一多个 LLM Provider 的接口。

→ [03-llm-layer.md](03-llm-layer.md)