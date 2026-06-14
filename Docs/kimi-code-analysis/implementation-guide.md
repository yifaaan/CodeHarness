# Kimi Code 重构实现指南

## 重构原则

基于 OpenAI 文章的智能体优先原则：

1. **代码仓库即记录系统**：所有知识必须在代码仓库中
2. **智能体可读性**：文档对智能体友好
3. **规范架构与品味**：通过强制执行不变量而非微观管理
4. **渐进式披露**：从高层到详细组织
5. **熵与垃圾收集**：建立循环清理流程

## 重构路线图

### Phase 1: 基础抽象层 (Foundation)

**目标**：建立统一的执行环境和 LLM 抽象

| 模块 | 优先级 | 依赖 | 说明 |
|------|--------|------|------|
| **Kaos** | #1 | 无 | 文件系统/进程抽象层 |
| **Config** | #2 | 无 | 配置系统和提供商管理 |
| **Kosong** | #3 | Config | LLM 提供商抽象层 |

#### Kaos 实现计划

```cpp
// 统一的执行环境接口
class Kaos {
public:
    // 文件系统操作
    virtual Result<std::string> readText(const std::filesystem::path& path) = 0;
    virtual Result<void> writeText(const std::filesystem::path& path, std::string_view content) = 0;
    virtual Result<std::vector<std::filesystem::path>> glob(std::string_view pattern) = 0;
    virtual Result<FileInfo> stat(const std::filesystem::path& path) = 0;
    
    // 进程执行
    virtual Result<ExecResult> exec(std::string_view command, const ExecOptions& options = {}) = 0;
    
    // 路径操作
    virtual std::filesystem::path resolve(std::string_view path) = 0;
    virtual std::filesystem::path relative(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
};

// 本地实现
class LocalKaos : public Kaos { ... };

// SSH 实现
class SSHKaos : public Kaos { ... };
```

#### Config 实现计划

```cpp
// 配置系统
struct KimiConfig {
    // 模型配置
    std::string model;
    std::string provider;
    
    // 权限配置
    PermissionMode permissionMode;
    std::vector<PermissionRule> permissionRules;
    
    // MCP 配置
    std::vector<McpServer> mcpServers;
    
    // 钩子配置
    std::vector<Hook> hooks;
};

// 配置管理器
class ConfigManager {
public:
    Result<KimiConfig> loadConfig(const std::filesystem::path& configPath);
    Result<void> saveConfig(const KimiConfig& config);
    Result<void> updateConfig(const PartialConfig& updates);
};
```

#### Kosong 实现计划

```cpp
// LLM 提供商接口
class ChatProvider {
public:
    virtual ~ChatProvider() = default;
    
    virtual Result<Response> generate(const std::vector<Message>& messages, 
                                     const GenerateOptions& options = {}) = 0;
    virtual AsyncIterable<ThinkingDelta> withThinking(const std::vector<Message>& messages,
                                                      const GenerateOptions& options = {}) = 0;
};

// 提供商管理器
class ProviderManager {
public:
    Result<std::unique_ptr<ChatProvider>> createProvider(const std::string& providerName);
    std::vector<std::string> availableProviders();
};
```

### Phase 2: Agent 核心引擎 (Agent Core)

**目标**：实现核心代理循环和上下文管理

| 模块 | 优先级 | 依赖 | 说明 |
|------|--------|------|------|
| **Loop** | #4 | Kosong | 无状态代理循环 |
| **Agent** | #5 | Loop, Kaos | 核心协调器 |
| **Context** | #6 | Agent | 对话历史管理 |
| **Turn** | #7 | Agent | Turn 生命周期管理 |

#### Loop 实现计划

```cpp
// 无状态代理循环
struct TurnInput {
    std::string turnId;
    std::unique_ptr<AbortSignal> signal;
    ChatProvider* llm;
    std::function<std::vector<Message>()> buildMessages;
    LoopEventDispatcher dispatchEvent;
    std::vector<std::unique_ptr<ExecutableTool>> tools;
    std::unique_ptr<LoopHooks> hooks;
    size_t maxSteps = 100;
};

Result<TurnResult> runTurn(const TurnInput& input);
```

#### Agent 实现计划

```cpp
// 核心协调器
class Agent {
public:
    Agent(const AgentConfig& config);
    
    // 核心子系统
    ConfigState config;
    ContextMemory context;
    TurnFlow turn;
    ToolManager tools;
    PermissionManager permission;
    HookEngine hooks;
    AgentRecords records;
    
    // RPC 方法
    AgentAPI rpcMethods();
};
```

### Phase 3: 服务层 (Services)

**目标**：实现工具、权限和钩子系统

| 模块 | 优先级 | 依赖 | 说明 |
|------|--------|------|------|
| **Records** | #8 | Agent | 事件溯源 |
| **Session** | #9 | Agent | 会话管理 |
| **Tools** | #10 | Agent, Kaos | 工具系统 |
| **Permission** | #11 | Agent | 权限系统 |
| **Hooks** | #12 | Agent | 钩子系统 |

#### Tools 实现计划

```cpp
// 工具接口
class ExecutableTool {
public:
    virtual ~ExecutableTool() = default;
    
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual Schema inputSchema() const = 0;
    
    virtual Result<ToolValidation> validate(const ToolInput& input) = 0;
    virtual Result<ToolResult> execute(const ToolInput& input, const ToolContext& context) = 0;
};

// 工具管理器
class ToolManager {
public:
    void registerTool(std::unique_ptr<ExecutableTool> tool);
    void registerMcpTools(const std::vector<McpTool>& tools);
    std::vector<ToolInfo> availableTools() const;
    Result<ToolResult> executeTool(const ToolCall& call, const ToolContext& context);
};
```

#### Permission 实现计划

```cpp
// 权限管理器
class PermissionManager {
public:
    PermissionMode mode() const;
    void setMode(PermissionMode mode);
    
    Result<PermissionResult> beforeToolCall(const ToolCall& call);
    Result<PermissionResult> checkSensitiveFile(const std::filesystem::path& path);
};
```

### Phase 4: 扩展系统 (Extensions)

**目标**：实现技能和 MCP 集成

| 模块 | 优先级 | 依赖 | 说明 |
|------|--------|------|------|
| **Skills** | #13 | Agent | 技能系统 |
| **MCP** | #14 | Agent, Kaos | MCP 集成 |

#### Skills 实现计划

```cpp
// 技能系统
class SkillManager {
public:
    Result<Skill> discoverSkill(std::string_view name);
    Result<void> activateSkill(const Skill& skill);
    std::vector<Skill> availableSkills() const;
};
```

### Phase 5: 应用层 (Application)

**目标**：实现 CLI/TUI 和 SDK

| 模块 | 优先级 | 依赖 | 说明 |
|------|--------|------|------|
| **CLI/TUI** | #15 | Agent, SDK | 用户界面 |
| **SDK** | #16 | Agent | 公共 API |

#### CLI/TUI 实现计划

```cpp
// CLI 入口
int main(int argc, char* argv[]) {
    // 解析命令行参数
    // 加载配置
    // 创建 Agent
    // 启动 TUI 或单次模式
}

// TUI 框架
class TuiApp {
public:
    void run();
    void handleInput(const std::string& input);
    void render(const AgentEvent& event);
};
```

## 实现优先级

### 高优先级 (必须先实现)

1. **Kaos**：所有工具都依赖文件系统/进程抽象
2. **Config**：连接提供商到配置
3. **Kosong**：核心 LLM 抽象

### 中优先级 (核心功能)

4. **Loop**：无状态循环，最容易移植
5. **Agent**：组合所有子系统
6. **Context**：启用实际对话
7. **Turn**：桥接用户输入到循环

### 低优先级 (扩展功能)

8. **Records**：启用持久化
9. **Session**：启用多代理
10. **Tools**：给代理动作
11. **Permission/Hooks**：控制安全性
12. **Skills/MCP**：扩展功能

## 测试策略

### 单元测试

- 为每个模块编写单元测试
- 使用 doctest 框架
- Mock 外部依赖 (Kaos, ChatProvider)

### 集成测试

- 测试模块间交互
- 测试完整代理循环
- 测试会话持久化

### 端到端测试

- 测试完整用户流程
- 测试 CLI/TUI 交互
- 测试多代理协作

## 迁移策略

### 从 OpenHarness 迁移

1. **识别复用代码**：识别可以复用的代码
2. **渐进式替换**：逐步替换为新实现
3. **保持兼容性**：保持 API 兼容性
4. **测试验证**：确保功能等价

### 从 Kimi Code 迁移

1. **理解设计意图**：理解 Kimi Code 的设计决策
2. **适配 C++**：将 TypeScript 代码适配为 C++
3. **优化性能**：利用 C++ 的性能优势
4. **保持一致性**：保持与现有代码风格一致

## 文档策略

### 文档结构

```
docs/
├── kimi-code-analysis/          # Kimi Code 分析文档
│   ├── INDEX.md
│   ├── architecture-overview.md
│   ├── core-components.md
│   ├── design-patterns.md
│   └── implementation-guide.md
├── plan/
│   └── re-build/                # 重构设计文档
│       ├── AGENTS.md
│       ├── ARCHITECTURE.md
│       ├── design-docs/
│       ├── exec-plans/
│       └── references/
└── ...                          # 其他文档
```

### 文档原则

1. **渐进式披露**：从高层到详细组织
2. **智能体友好**：文档对智能体可读
3. **代码即真相**：代码是最终真相来源
4. **可维护性**：文档结构清晰，易于维护

## 质量保证

### 代码质量

- 遵循 C++ Core Guidelines

### 代码风格

- Google C++ Style

### 文档质量

- 文档与代码同步更新
- 文档结构清晰
- 文档对智能体友好
- 文档可维护

### 测试质量

- 测试覆盖率 > 80%
- 测试用例完整
- 测试可重复
- 测试可维护
