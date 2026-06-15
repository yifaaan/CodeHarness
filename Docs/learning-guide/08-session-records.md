# 第8章：会话与事件溯源

> 会话管理持久化 Agent 状态，事件溯源记录所有操作历史。

## 1. 为什么需要会话和事件溯源？

### 1.1 问题场景

**场景1：用户关闭终端后想继续**
```
用户: "帮我分析这个项目"
Agent: 开始分析...（执行了很多步骤）
用户: 关闭终端
--- 下次启动 ---
用户: 想继续之前的分析 → 需要恢复状态
```

**场景2：调试 Agent 行为**
```
用户: "为什么 Agent 执行了这个奇怪的命令？"
需要: 查看完整的操作历史，找出原因
```

**场景3：崩溃恢复**
```
Agent 正在执行 → 程序崩溃
需要: 重启后恢复到崩溃前的状态
```

### 1.2 解决方案

**Event Sourcing（事件溯源）**：
- 记录所有状态变化为事件
- 事件是追加的（append-only），不修改
- 恢复时：重新播放所有事件

**会话管理**：
- 将事件存储到文件
- 支持创建、恢复、关闭会话
- 管理会话元数据（标题、时间等）

## 2. 事件溯源详解

### 2.1 核心概念

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Event Sourcing 模式                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   当前状态 = 初始状态 + 所有事件的累积效果                           │
│                                                                      │
│   例如：                                                             │
│   事件1: TurnPrompt("分析项目")                                      │
│   事件2: ContextAppendMessage(User: "分析项目")                      │
│   事件3: ContextAppendLoopEvent(ToolCallStarted: Bash "ls")          │
│   事件4: ContextAppendLoopEvent(ToolResult: "...")                   │
│   事件5: ContextAppendMessage(Assistant: "项目结构是...")            │
│   ...                                                                │
│                                                                      │
│   恢复：依次应用这些事件，重建 Agent 的 history                      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 Record 类型

**位置**：`Source/CodeHarness/Records/RecordTypes.h`

```cpp
// 记录类型枚举
enum class RecordKind
{
    TurnPrompt,            // Turn 开始：用户输入
    TurnCancel,            // Turn 取消
    ContextAppendMessage,  // 消息追加到历史
    ContextAppendLoopEvent,// Loop 事件
};

// 记录元数据
struct RecordMeta
{
    std::int64_t ts = 0;          // 时间戳（毫秒）
    std::string protocol = "1.0"; // 协议版本
};

// 具体记录类型

// Turn 开始记录
struct TurnPromptRecord
{
    std::string turnId;                    // Turn ID
    std::vector<llm::ContentPart> input;   // 用户输入内容
    int origin = 0;                        // 来源：0=User, 1=SystemTrigger
};

// Turn 取消记录
struct TurnCancelRecord
{
    std::string turnId;  // 被取消的 Turn ID
};

// 消息追加记录
struct ContextAppendMessageRecord
{
    llm::Message message;  // 追加的消息
};

// Loop 事件记录
struct ContextAppendLoopEventRecord
{
    engine::LoopEvent event;  // Loop 事件
};

// Agent 记录变体
using AgentRecord = std::variant<
    TurnPromptRecord,
    TurnCancelRecord,
    ContextAppendMessageRecord,
    ContextAppendLoopEventRecord
>;

// 线上记录（元数据 + 具体记录）
struct WireRecord
{
    RecordMeta meta;
    AgentRecord record;
};
```

### 2.3 wire.jsonl 格式

**存储格式**：每行一个 JSON 记录

```jsonl
{"meta":{"ts":1700000000000,"protocol":"1.0"},"record":{"TurnPrompt":{"turnId":"turn_1","input":[{"Text":{"text":"分析项目"}}],"origin":0}}}
{"meta":{"ts":1700000000100,"protocol":"1.0"},"record":{"ContextAppendMessage":{"message":{"role":"User","content":[{"Text":{"text":"分析项目"}}]}}}}
{"meta":{"ts":1700000001000,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"ToolCallStarted":{"id":"call_1","name":"Bash","args":{"command":"ls"}}}}}}
{"meta":{"ts":1700000002000,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"ToolResult":{"id":"call_1","name":"Bash","result":{"content":"file1.cpp\nfile2.cpp","isError":false}}}}}}
{"meta":{"ts":1700000003000,"protocol":"1.0"},"record":{"ContextAppendMessage":{"message":{"role":"Tool","content":[{"Text":{"text":"file1.cpp\nfile2.cpp"}}],"toolCallId":"call_1"}}}}
{"meta":{"ts":1700000004000,"protocol":"1.0"},"record":{"ContextAppendMessage":{"message":{"role":"Assistant","content":[{"Text":{"text":"项目包含两个 C++ 文件..."}}]}}}}
```

**优点**：
- 追加写入：高效，不修改已有数据
- 易于解析：每行独立
- 人类可读：JSON 格式
- 跨语言：任何语言都能读写

## 3. AgentRecords 实现

### 3.1 接口定义

```cpp
// 位置：Source/CodeHarness/Records/AgentRecords.h

class AgentRecords
{
public:
    virtual ~AgentRecords() = default;

    // 记录一个事件
    // 返回失败表示写入错误
    virtual absl::Status Log(RecordKind kind, nlohmann::json data) = 0;
    
    // 读取所有记录
    virtual absl::StatusOr<std::vector<WireRecord>> ReadAll() = 0;
    
    // 刷新缓冲区到磁盘
    virtual absl::Status Flush() = 0;
    
    // 关闭文件
    virtual absl::Status Close() = 0;
};
```

### 3.2 FilePersistence 实现

```cpp
// 文件持久化实现
class FileAgentRecords : public AgentRecords
{
public:
    // 打开 wire.jsonl 文件
    explicit FileAgentRecords(const std::string& path);
    ~FileAgentRecords() override;

    absl::Status Log(RecordKind kind, nlohmann::json data) override {
        // 构建记录
        WireRecord record;
        record.meta.ts = NowMs();
        record.meta.protocol = "1.0";
        
        // 根据 kind 构建具体记录
        record.record = BuildRecord(kind, data);
        
        // 序列化为 JSON 行
        std::string line = Serialize(record) + "\n";
        
        // 追加写入文件
        file_ << line;
        file_.flush();
        
        return absl::OkStatus();
    }
    
    absl::StatusOr<std::vector<WireRecord>> ReadAll() override {
        std::vector<WireRecord> records;
        
        // 从头读取文件
        file_.seekg(0);
        std::string line;
        while (std::getline(file_, line)) {
            auto record = Parse(line);
            if (record) {
                records.push_back(*record);
            }
        }
        
        return records;
    }

private:
    std::ofstream file_;
    std::string path_;
};
```

## 4. 会话管理

### 4.1 SessionStore

**职责**：管理会话目录和索引

```
~/.codeharness/
├── config.toml                    # 全局配置
├── session_index.jsonl            # 会话索引
└── sessions/
    └── <workdir-key>/             # 工作目录编码
        └── <session-uuid>/        # 会话 ID
            ├── state.json         # 会话元数据
            └── agents/
                └── main/
                    └── wire.jsonl # 主 Agent 的事件记录
```

### 4.2 Session 类

**位置**：`Source/CodeHarness/Session/Session.h`

```cpp
// 会话配置
struct SessionConfig
{
    host::Host* host = nullptr;              // Host 接口
    llm::ChatProvider* provider = nullptr;   // LLM Provider
    tools::ToolManager* toolManager = nullptr; // 工具管理器
    std::string workdir;                     // 工作目录（仅 Create 用）
    std::string title;                       // 会话标题（仅 Create 用）
};

// 会话类
class Session
{
public:
    // 创建新会话
    static absl::StatusOr<std::unique_ptr<Session>> Create(
        SessionStore* store,
        SessionConfig cfg
    );
    
    // 恢复已有会话
    static absl::StatusOr<std::unique_ptr<Session>> Resume(
        SessionStore* store,
        SessionConfig cfg,
        std::string_view sessionId
    );
    
    ~Session();
    
    // 访问 Agent
    agent::Agent* MainAgent() const { return agent_.get(); }
    
    // 会话信息
    const std::string& Id() const { return sessionId_; }
    const SessionMeta& Meta() const { return meta_; }
    bool IsClosed() const { return closed_; }
    
    // 关闭会话（刷新记录）
    absl::Status Close();

private:
    SessionStore* store_;
    std::string sessionId_;
    std::string sessionPath_;
    SessionMeta meta_;
    
    std::unique_ptr<records::AgentRecords> records_;
    std::unique_ptr<agent::Agent> agent_;
    bool closed_ = false;
};
```

### 4.3 会话创建流程

```cpp
absl::StatusOr<std::unique_ptr<Session>> Session::Create(
    SessionStore* store,
    SessionConfig cfg
) {
    // 1. 分配会话 ID
    std::string sessionId = GenerateUUID();
    
    // 2. 创建会话目录
    std::string sessionPath = store->AllocatePath(cfg.workdir, sessionId);
    std::filesystem::create_directories(sessionPath + "/agents/main");
    
    // 3. 创建会话元数据
    SessionMeta meta;
    meta.id = sessionId;
    meta.title = cfg.title;
    meta.workdir = cfg.workdir;
    meta.createdAt = NowMs();
    meta.updatedAt = meta.createdAt;
    
    // 写入 state.json
    WriteStateJson(sessionPath, meta);
    
    // 4. 创建 Session 对象
    auto session = std::unique_ptr<Session>(new Session(store, sessionId, sessionPath, meta));
    
    // 5. 构建并连接 Agent 和 Records
    session->WireMainAgent(cfg, false);  // 新会话，不需要 replay
    
    return session;
}
```

### 4.4 会话恢复流程

```cpp
absl::StatusOr<std::unique_ptr<Session>> Session::Resume(
    SessionStore* store,
    SessionConfig cfg,
    std::string_view sessionId
) {
    // 1. 查找会话目录
    std::string sessionPath = store->FindPath(sessionId);
    if (sessionPath.empty()) {
        return absl::NotFoundError("Session not found");
    }
    
    // 2. 读取会话元数据
    auto meta = ReadStateJson(sessionPath);
    
    // 3. 创建 Session 对象
    auto session = std::unique_ptr<Session>(new Session(store, std::string(sessionId), sessionPath, *meta));
    
    // 4. 构建并连接 Agent 和 Records
    session->WireMainAgent(cfg, true);  // 需要 replay
    
    return session;
}

absl::Status Session::WireMainAgent(SessionConfig cfg, bool replay) {
    // 1. 创建 AgentRecords
    std::string wirePath = sessionPath_ + "/agents/main/wire.jsonl";
    records_ = std::make_unique<FileAgentRecords>(wirePath);
    
    // 2. 创建 Agent
    agent_ = std::make_unique<agent::Agent>(cfg.provider, cfg.host, cfg.toolManager);
    
    // 3. 连接 Records 到 Agent
    agent_->SetRecords(records_.get());
    
    // 4. 如果是恢复，重放记录
    if (replay) {
        agent_->Resume();
    }
    
    return absl::OkStatus();
}
```

### 4.5 Agent::Resume 实现

```cpp
absl::Status Agent::Resume()
{
    if (!records) {
        return absl::FailedPreconditionError("No records set");
    }
    
    // 读取所有记录
    auto allRecords = records->ReadAll();
    if (!allRecords.ok()) {
        return allRecords.status();
    }
    
    // 重放记录
    for (const auto& record : *allRecords) {
        std::visit(overloaded{
            [&](const TurnPromptRecord& r) {
                // 记录 turnId
                lastTurnId = r.turnId;
            },
            [&](const ContextAppendMessageRecord& r) {
                // 追加消息到历史
                history.push_back(r.message);
            },
            [&](const ContextAppendLoopEventRecord& r) {
                // Loop 事件通常不需要恢复到状态
                // 但可能需要记录
            },
            [&](const TurnCancelRecord& r) {
                // 取消事件
            }
        }, record.record);
    }
    
    return absl::OkStatus();
}
```

## 5. 记录集成到 Agent

### 5.1 Agent 中记录事件

```cpp
// Agent::Prompt 中
absl::StatusOr<PromptResult> Agent::Prompt(std::string_view text)
{
    // ... 前置检查 ...
    
    // 记录 Turn 开始
    if (records) {
        records->Log(RecordKind::TurnPrompt, {
            {"turnId", turnId},
            {"input", ContentPartsToJson(input)},
            {"origin", 0}  // User
        });
    }
    
    // 添加用户消息
    history.push_back(userMsg);
    
    // 记录消息追加
    if (records) {
        records->Log(RecordKind::ContextAppendMessage, {
            {"message", MessageToJson(userMsg)}
        });
    }
    
    // 设置 Loop 事件分发器
    input.dispatchEvent = [this](const engine::LoopEvent& event) {
        // 分发给 UI
        Dispatch(LoopEvent{event});
        
        // 记录到文件
        if (records) {
            records->Log(RecordKind::ContextAppendLoopEvent, {
                {"event", LoopEventToJson(event)}
            });
        }
    };
    
    // 执行 Loop...
}
```

## 6. 数据流向

```
用户输入
    ↓
Agent.Prompt()
    ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        记录流程                                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. Log(TurnPrompt)                                                 │
│     ↓                                                               │
│  2. history.push(userMsg)                                           │
│     ↓                                                               │
│  3. Log(ContextAppendMessage)                                       │
│     ↓                                                               │
│  4. Loop.RunTurn()                                                  │
│     │                                                               │
│     ├─ LoopEvent → dispatchEvent → Log(ContextAppendLoopEvent)     │
│     │                                                               │
│     └─ 工具调用 → 消息追加 → Log(ContextAppendMessage)              │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
    ↓
wire.jsonl 文件
```

## 7. 测试分析

### 7.1 AgentRecords 测试

```cpp
TEST(FileAgentRecords, LogAndRead) {
    // 创建临时文件
    std::string path = std::filesystem::temp_directory_path() / "test.jsonl";
    
    FileAgentRecords records(path);
    
    // 记录事件
    records.Log(RecordKind::TurnPrompt, {
        {"turnId", "turn_1"},
        {"input", nlohmann::json::array()},
        {"origin", 0}
    });
    
    records.Log(RecordKind::ContextAppendMessage, {
        {"message", {{"role", "user"}, {"content", "hello"}}}
    });
    
    records.Flush();
    
    // 读取
    auto all = records.ReadAll();
    ASSERT_TRUE(all.ok());
    EXPECT_EQ(all->size(), 2);
    
    // 验证第一条
    auto& first = (*all)[0];
    EXPECT_TRUE(std::holds_alternative<TurnPromptRecord>(first.record));
}
```

### 7.2 Session 测试

```cpp
TEST(Session, CreateAndResume) {
    // 准备依赖
    MockHost host;
    MockChatProvider provider;
    MockToolManager tools;
    
    SessionStore store(std::filesystem::temp_directory_path() / "sessions");
    
    // 创建会话
    auto createResult = Session::Create(&store, {
        .host = &host,
        .provider = &provider,
        .toolManager = &tools,
        .workdir = "/test/project",
        .title = "Test Session"
    });
    
    ASSERT_TRUE(createResult.ok());
    auto session = std::move(*createResult);
    
    std::string sessionId = session->Id();
    EXPECT_FALSE(sessionId.empty());
    
    // 执行一些操作
    session->MainAgent()->Prompt("hello");
    
    // 关闭会话
    session->Close();
    
    // 恢复会话
    auto resumeResult = Session::Resume(&store, {
        .host = &host,
        .provider = &provider,
        .toolManager = &tools
    }, sessionId);
    
    ASSERT_TRUE(resumeResult.ok());
    auto resumed = std::move(*resumeResult);
    
    // 验证历史恢复
    auto& history = resumed->MainAgent()->GetHistory();
    EXPECT_GT(history.size(), 0);
}
```

## 8. 类关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                        SessionStore                                  │
│  - 管理会话目录                                                      │
│  - 维护会话索引                                                      │
└─────────────────────────────┬───────────────────────────────────────┘
                              │ manages
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         Session                                      │
├─────────────────────────────────────────────────────────────────────┤
│  owns:                                                              │
│  - agent_: unique_ptr<Agent>                                        │
│  - records_: unique_ptr<AgentRecords>                               │
│                                                                      │
│  static methods:                                                    │
│  + Create(store, config) → Session                                  │
│  + Resume(store, config, sessionId) → Session                       │
│                                                                      │
│  instance methods:                                                  │
│  + MainAgent() → Agent*                                             │
│  + Close()                                                          │
└─────────────────────────────┬───────────────────────────────────────┘
                              │ owns
                              ▼
┌───────────────────────────┐ ┌───────────────────────────────────────┐
│          Agent            │ │           AgentRecords                │
│                          │ │                                       │
│  - records: AgentRecords* │ │  + Log(kind, data)                   │
│                          │ │  + ReadAll() → vector<WireRecord>     │
│  + SetRecords()          │ │  + Flush()                            │
│  + Resume()              │ │  + Close()                            │
└───────────────────────────┘ └───────────────────────────────────────┘
                                                                │
                                                                │ writes to
                                                                ▼
                                                    ┌───────────────────────┐
                                                    │    wire.jsonl        │
                                                    │    (append-only)     │
                                                    └───────────────────────┘
```

## 9. 小结

本章我们学习了：

- **为什么需要事件溯源**：持久化、调试、崩溃恢复
- **Event Sourcing 模式**：记录事件，重放恢复
- **Record 类型**：TurnPrompt、TurnCancel、ContextAppendMessage、ContextAppendLoopEvent
- **wire.jsonl 格式**：每行一个 JSON 记录
- **AgentRecords**：Log、ReadAll、Flush 接口
- **Session 管理**：Create、Resume、Close 流程

## 10. 练习建议

1. **阅读源码**：打开 `Records/FilePersistence.cpp`，理解文件读写
2. **查看 wire.jsonl**：运行一次 Agent，查看生成的事件记录
3. **思考题**：如果要支持子 Agent，事件记录需要如何扩展？

## 11. 下一步

下一章我们将 **串联所有模块**，跟踪一次完整请求的旅程。

→ [09-putting-it-together.md](09-putting-it-together.md)