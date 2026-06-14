# Kimi Code 与 CodeHarness 对比分析

## 架构对比

### 整体架构

| 特性 | Kimi Code | CodeHarness |
|------|-----------|-------------|
| **语言** | TypeScript (Node.js) | C++20 |
| **包管理** | pnpm monorepo | CMake + vcpkg |
| **构建系统** | tsdown | CMake |
| **测试框架** | vitest | doctest |
| **TUI 框架** | pi-tui | 独立 TUI |

### 核心模块对比

| 模块 | Kimi Code | CodeHarness | 差异 |
|------|-----------|-------------|------|
| **LLM 抽象** | `kosong` (ChatProvider) | Provider 层 (OpenAI/Anthropic) | Kimi Code 支持更多 Provider |
| **执行环境** | `kaos` (Kaos interface) | OS 层 | Kimi Code 有统一抽象 |
| **Agent 核心** | `Agent` class + `loop/` | `QueryEngine` + `run_query` | 类似设计 |
| **工具系统** | `ToolManager` + `ExecutableTool` | `ToolRegistry` | Kimi Code 有更丰富的工具 |
| **权限系统** | `PermissionManager` (manual/yolo/auto) | `PermissionChecker` | Kimi Code 有三级权限 |
| **会话管理** | `Session` + `SessionStore` (JSONL wire) | JSON snapshot | Kimi Code 有事件溯源 |
| **钩子系统** | `HookEngine` (13 event types) | Hooks | Kimi Code 有更丰富的钩子 |
| **MCP 支持** | `McpConnectionManager` (stdio/http) | 有 | 类似设计 |
| **上下文压缩** | `FullCompaction` (LLM-based summary) | 有计划 | Kimi Code 已实现 |
| **子代理** | `SessionSubagentHost` + profiles | Tasks + subagent | 类似设计 |
| **技能系统** | `SkillRegistry` (discovery + activation) | Skills | Kimi Code 有更完整的生命周期 |

## 设计模式对比

### 共同设计模式

1. **代理循环**：都实现了 "submit message -> run query -> tool call -> backfill result -> continue" 的核心循环
2. **工具失败不崩溃**：工具失败转换为 `ToolResultBlock{is_error=true}` 返回给模型
3. **权限检查先于工具执行**：两套系统都在工具执行前检查权限
4. **事件驱动 UI**：引擎产生事件，UI 只是事件消费者
5. **会话持久化**：都支持会话保存和恢复
6. **上下文管理**：都有 token 计数和上下文压缩机制
7. **MCP 集成**：都支持 Model Context Protocol

### Kimi Code 独特之处

1. **Kaos 抽象层**：独特的执行环境抽象，支持本地和 SSH 远程执行
2. **ToolScheduler**：基于资源访问冲突的工具并发调度器
3. **ToolAccesses**：细粒度的文件读写冲突检测
4. **Tool Input Display**：工具输入的结构化显示 (`ToolInputDisplay`)
5. **Plan Mode**：内置的计划模式 (EnterPlanMode/ExitPlanMode 工具)
6. **Background Tasks**：强大的后台任务系统 (bash + agent tasks)
7. **Reverse RPC**：TUI 通过反向 RPC 接收 Agent 的审批/提问请求
8. **Kimi OAuth**：原生的 Moonshot AI OAuth 集成
9. **Video Input**：支持视频作为模型输入
10. **Migration System**：旧版迁移工具 (`migration-legacy`)

### CodeHarness 独特之处

1. **C++20 性能**：原生编译，性能优势
2. **跨平台**：Windows 和 Linux 支持
3. **vcpkg 依赖管理**：统一的依赖管理
4. **Event-driven 架构**：更清晰的事件驱动设计
5. **RuntimeBundle**：运行时 bundle 设计

## 重构价值点

### 高价值模块（建议优先重构）

1. **ToolScheduler**：基于资源访问冲突的工具并发调度
   - 当前项目没有类似实现
   - 可以显著提高工具执行效率
   - 避免读写冲突

2. **PermissionManager**：三级权限模式（manual/yolo/auto）
   - 当前项目有简单的权限检查
   - 可以提供更灵活的权限控制
   - 支持 AFK 模式

3. **HookEngine**：13 种钩子事件，支持外部命令执行
   - 当前项目有基础的钩子系统
   - 可以提供更丰富的扩展点
   - 支持外部命令执行

4. **Context Compaction**：LLM 驱动的上下文压缩
   - 当前项目有计划但未实现
   - 可以处理长会话
   - 自动摘要旧上下文

5. **Background Tasks**：强大的后台任务系统
   - 当前项目有计划但未实现
   - 支持非阻塞执行
   - 支持任务监控和终止

### 可复用设计模式

1. **渐进式披露**：文档组织方式
   - 当前项目文档结构已类似
   - 可以进一步优化

2. **事件溯源**：状态管理方式
   - 当前项目使用 JSON snapshot
   - 可以迁移到事件溯源

3. **两阶段执行**：工具安全机制
   - 当前项目是单阶段执行
   - 可以实现两阶段执行

4. **无状态循环**：核心执行引擎设计
   - 当前项目已有类似设计
   - 可以进一步优化

## 实现建议

### 短期 (1-2 周)

1. **实现 Kaos 抽象层**
   - 定义统一接口
   - 实现 LocalKaos
   - 迁移现有工具

2. **实现 ToolScheduler**
   - 分析工具访问冲突
   - 实现并发调度
   - 增加冲突检测

3. **增强 PermissionManager**
   - 实现三级权限模式
   - 支持规则 DSL
   - 增加 Policy 评估

### 中期 (2-4 周)

4. **实现 HookEngine**
   - 实现 13 种钩子事件
   - 支持正则匹配器
   - 增加 Fail Open 策略

5. **实现 Context Compaction**
   - 实现 LLM 驱动的摘要生成
   - 支持手动和自动压缩
   - 保留最近的对话历史

6. **实现 Background Tasks**
   - 实现 BackgroundManager
   - 支持 bash 和 agent 任务类型
   - 增加任务状态监控

### 长期 (1-2 月)

7. **实现事件溯源**
   - 将操作记录为事件
   - 实现事件重放
   - 支持会话恢复

8. **实现 Profile 系统**
   - 支持 YAML 配置文件
   - 实现 Profile 继承
   - 增加 Profile 组合

9. **优化 TUI**
   - 实现反向 RPC
   - 增加审批面板
   - 支持媒体显示

## 风险评估

### 技术风险

1. **C++ 实现复杂度**
   - Kimi Code 使用 TypeScript，CodeHarness 使用 C++
   - 需要适配 C++ 的内存管理和错误处理
   - **缓解**：使用 RAII、智能指针、Result<T>

2. **性能开销**
   - 抽象层可能引入性能开销
   - **缓解**：使用内联、模板、编译时优化

3. **测试覆盖**
   - 新模块需要完整的测试覆盖
   - **缓解**：使用 doctest，编写单元测试和集成测试

### 进度风险

1. **范围蔓延**
   - 重构范围可能不断扩大
   - **缓解**：严格遵循路线图，分阶段交付

2. **依赖冲突**
   - 新模块可能与现有代码冲突
   - **缓解**：渐进式替换，保持兼容性

3. **资源不足**
   - 重构需要大量时间和精力
   - **缓解**：优先实现高价值模块，逐步完善

## 成功标准

### 功能标准

1. **功能等价**：新实现的功能与 Kimi Code 等价
2. **性能达标**：性能不低于现有实现
3. **测试覆盖**：测试覆盖率 > 80%
4. **文档完整**：文档结构清晰，对智能体友好

### 质量标准

1. **代码质量**：遵循 C++ Core Guidelines
2. **可维护性**：代码结构清晰，易于维护
3. **可扩展性**：支持未来功能扩展
4. **可测试性**：易于编写测试用例

### 业务标准

1. **用户体验**：用户体验不低于现有实现
2. **开发效率**：开发效率提高 50% 以上
3. **维护成本**：维护成本降低 30% 以上
4. **团队满意度**：团队对新架构满意
