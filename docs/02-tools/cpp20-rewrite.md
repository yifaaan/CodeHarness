# Tools C++20 重写方案

## 目录建议

```text
src/codeharness/tools/
  tool.hpp
  tool_registry.hpp
  tool_registry.cpp
  tool_result.hpp
  tool_context.hpp
  json_schema.hpp
  builtin_tools.hpp
  file_read_tool.cpp
  file_write_tool.cpp
  file_edit_tool.cpp
  glob_tool.cpp
  grep_tool.cpp
  bash_tool.cpp
  todo_write_tool.cpp
  ask_user_tool.cpp
  mcp_tool_adapter.cpp
```

## ITool 接口

```cpp
struct ToolExecutionContext {
    std::filesystem::path cwd;
    nlohmann::json metadata = nlohmann::json::object();
    HookExecutor* hooks = nullptr;
};

struct ToolResult {
    std::string output;
    bool isError = false;
    nlohmann::json metadata = nlohmann::json::object();
};

class ITool {
public:
    virtual ~ITool() = default;

    virtual std::string_view name() const = 0;
    virtual std::string_view description() const = 0;
    virtual nlohmann::json inputSchema() const = 0;

    virtual bool isReadOnly(const nlohmann::json& arguments) const {
        return false;
    }

    virtual ToolResult execute(const nlohmann::json& arguments,
                               const ToolExecutionContext& context) = 0;
};
```

第一版可以同步执行。后续如果工具要异步，可以把返回值换成 `std::future<ToolResult>` 或 coroutine task。

## ToolRegistry

```cpp
class ToolRegistry {
public:
    void registerTool(std::shared_ptr<ITool> tool);
    std::shared_ptr<ITool> get(std::string_view name) const;
    std::vector<std::shared_ptr<ITool>> listTools() const;
    nlohmann::json toApiSchema() const;

private:
    std::unordered_map<std::string, std::shared_ptr<ITool>> tools_;
};
```

`toApiSchema()` 输出给 provider。Anthropic 和 OpenAI 的外层格式不同，但 tool 的 JSON Schema 可以共用。

## 输入 schema 和强类型参数

建议组合使用：

1. 工具提供手写 JSON Schema。
2. 执行前用 JSON Schema validator 检查。
3. 工具内部把 JSON 转为强类型 struct。

示例：

```cpp
struct ReadFileInput {
    std::filesystem::path path;
    int offset = 0;
    int limit = 200;
};

ReadFileInput parseReadFileInput(const nlohmann::json& j) {
    ReadFileInput input;
    input.path = j.at("path").get<std::string>();
    input.offset = j.value("offset", 0);
    input.limit = j.value("limit", 200);
    return input;
}
```

## ReadFileTool

关键行为：

- 解析 path。
- 转换为 cwd 下的绝对路径。
- 检查路径存在且是普通文件。
- 支持 offset/limit 行范围。
- 长行截断，避免一行超大。
- 返回带行号文本，方便模型引用。

路径解析建议：

```cpp
std::filesystem::path resolveUnderCwd(std::filesystem::path cwd,
                                      std::filesystem::path userPath);
```

## WriteFileTool

关键行为：

- 写入前确认目标路径在 cwd 内。
- 父目录不存在时可选创建。
- 写入使用 atomic write：先写 `.tmp`，再 rename。
- 返回写入字节数和路径。

如果接入权限系统，`write_file` 在 default 模式下应需要确认。

## EditFileTool

初版不要做太复杂。建议支持简单 replace：

```json
{
  "path": "src/main.cpp",
  "old_string": "...",
  "new_string": "...",
  "replace_all": false
}
```

行为：

- `old_string` 必须唯一匹配，除非 `replace_all=true`。
- 匹配不到返回错误。
- 多次匹配且未开启 replace_all 返回错误。
- 写入前生成 diff 给权限弹窗或日志。

## GlobTool 和 GrepTool

C++ 标准库没有完整的 grep。按当前项目约束，搜索相关外部库通过 xmake 导入。建议：

- glob：用 `std::filesystem::recursive_directory_iterator` 做遍历，自己实现简单 wildcard matcher。
- grep：用 `re2` 做正则匹配，通过 xmake `add_requires("re2")` 导入。RE2 是 C++ API，避免直接使用 PCRE2 的 C API。

注意：

- 默认跳过 `.git`、build、二进制文件。
- 限制最大文件大小。
- 限制最大结果数量。

不建议通过 shell 调 `ripgrep` 作为第一版实现，否则工具行为会依赖用户环境。需要更高性能时，可以在 C++ 内部保留 `re2` matcher 并优化文件遍历、ignore 规则和并发扫描。

## BashTool

建议接口：

```json
{
  "command": "xmake test",
  "timeout_seconds": 120,
  "cwd": "."
}
```

C++ 需要封装跨平台 process runner：

```cpp
struct ProcessResult {
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    bool timedOut = false;
};

class ProcessRunner {
public:
    ProcessResult runShell(std::string command,
                           std::filesystem::path cwd,
                           std::chrono::seconds timeout);
};
```

输出建议合并 stdout/stderr，或分别存 metadata。

## Tool output 截断

```cpp
struct ToolOutputPolicy {
    size_t inlineChars = 16000;
    size_t previewChars = 3000;
    std::filesystem::path artifactDir;
};

struct PreparedToolOutput {
    std::string inlineText;
    std::optional<std::filesystem::path> artifactPath;
};
```

行为：

- 输出小于 `inlineChars`：直接返回。
- 输出大于限制：保存完整输出到 artifact，返回 preview 和路径。

## 权限目标提取

ToolExecutor 需要从参数中提取权限目标：

```cpp
struct PermissionTarget {
    std::optional<std::filesystem::path> path;
    std::optional<std::string> command;
};
```

简单规则：

- 参数里有 `path`、`file_path`、`root` 时当路径。
- `bash` 参数的 `command` 当命令。
- 工具也可以覆写 `permissionTarget()`，比猜字段更可靠。

推荐在 `ITool` 中增加：

```cpp
virtual PermissionTarget permissionTarget(const nlohmann::json& arguments) const;
```

## 内置工具注册

```cpp
std::shared_ptr<ToolRegistry> createDefaultToolRegistry(RuntimeServices services) {
    auto registry = std::make_shared<ToolRegistry>();
    registry->registerTool(std::make_shared<ReadFileTool>());
    registry->registerTool(std::make_shared<WriteFileTool>());
    registry->registerTool(std::make_shared<EditFileTool>());
    registry->registerTool(std::make_shared<GlobTool>());
    registry->registerTool(std::make_shared<GrepTool>());
    registry->registerTool(std::make_shared<BashTool>(services.processRunner));
    return registry;
}
```

## 不建议第一版做的事

- 动态加载 C++ 插件工具 `.dll/.so`。
- 完整 Jupyter notebook 编辑。
- 图片生成。
- LSP。
- cron。
- remote trigger。

这些可以在核心稳定后加。

## 测试建议

- `ToolRegistry` 重名注册会覆盖还是拒绝，需要明确。
- `read_file` 行范围正确。
- `edit_file` 多重匹配返回错误。
- `bash` timeout。
- 路径逃逸被拒绝。
- 大输出保存 artifact。
- schema JSON snapshot 稳定。
