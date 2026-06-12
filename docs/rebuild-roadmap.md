# CodeHarness 重构路线图

基于 Kimi Code 架构分析和 OpenAI 智能体优先工程原则的重构计划。

## 重构原则

1. **代码仓库即记录系统**：所有知识必须在代码仓库中
2. **智能体可读性**：文档对智能体友好
3. **规范架构与品味**：通过强制执行不变量而非微观管理
4. **渐进式披露**：从高层到详细组织
5. **熵与垃圾收集**：建立循环清理流程

## 重构阶段

### Phase 1: 基础抽象层 (Foundation) - 4 周

**目标**：建立统一的执行环境和 LLM 抽象

| 模块 | 优先级 | 依赖 | 说明 | 状态 |
|------|--------|------|------|------|
| **Kaos** | #1 | 无 | 文件系统/进程抽象层 | 计划中 |
| **Config** | #2 | 无 | 配置系统和提供商管理 | 计划中 |
| **Kosong** | #3 | Config | LLM 提供商抽象层 | 计划中 |

**关键交付物**：
- Kaos 接口定义和 LocalKaos 实现
- Config 系统和 ProviderManager
- Kosong ChatProvider 接口和基础实现

### Phase 2: Agent 核心引擎 (Agent Core) - 6 周

**目标**：实现核心代理循环和上下文管理

| 模块 | 优先级 | 依赖 | 说明 | 状态 |
|------|--------|------|------|------|
| **Loop** | #4 | Kosong | 无状态代理循环 | 计划中 |
| **Agent** | #5 | Loop, Kaos | 核心协调器 | 计划中 |
| **Context** | #6 | Agent | 对话历史管理 | 计划中 |
| **Turn** | #7 | Agent | Turn 生命周期管理 | 计划中 |

**关键交付物**：
- runTurn 无状态循环
- Agent 类组合所有子系统
- ContextMemory 和上下文管理
- TurnFlow 用户交互入口

### Phase 3: 服务层 (Services) - 8 周

**目标**：实现工具、权限和钩子系统

| 模块 | 优先级 | 依赖 | 说明 | 状态 |
|------|--------|------|------|------|
| **Records** | #8 | Agent | 事件溯源 | 计划中 |
| **Session** | #9 | Agent | 会话管理 | 计划中 |
| **Tools** | #10 | Agent, Kaos | 工具系统 | 计划中 |
| **Permission** | #11 | Agent | 权限系统 | 计划中 |
| **Hooks** | #12 | Agent | 钩子系统 | 计划中 |

**关键交付物**：
- AgentRecords 事件溯源
- Session 会话管理
- ToolManager 和内置工具
- PermissionManager 三级权限
- HookEngine 13 种钩子事件

### Phase 4: 扩展系统 (Extensions) - 4 周

**目标**：实现技能和 MCP 集成

| 模块 | 优先级 | 依赖 | 说明 | 状态 |
|------|--------|------|------|------|
| **Skills** | #13 | Agent | 技能系统 | 计划中 |
| **MCP** | #14 | Agent, Kaos | MCP 集成 | 计划中 |

**关键交付物**：
- SkillManager 技能系统
- McpConnectionManager MCP 集成

### Phase 5: 应用层 (Application) - 6 周

**目标**：实现 CLI/TUI 和 SDK

| 模块 | 优先级 | 依赖 | 说明 | 状态 |
|------|--------|------|------|------|
| **CLI/TUI** | #15 | Agent, SDK | 用户界面 | 计划中 |
| **SDK** | #16 | Agent | 公共 API | 计划中 |

**关键交付物**：
- CLI 入口和参数解析
- TUI 终端界面
- SDK 公共 API

## 依赖关系

```
Phase 1: Foundation
    Kaos → Config → Kosong

Phase 2: Agent Core
    Loop → Agent → Context → Turn
    (依赖 Phase 1)

Phase 3: Services
    Records → Session → Tools → Permission/Hooks
    (依赖 Phase 2)

Phase 4: Extensions
    Skills → MCP
    (依赖 Phase 3)

Phase 5: Application
    CLI/TUI → SDK
    (依赖 Phase 4)
```

## 资源估算

| 阶段 | 时间 | 人力 | 主要风险 |
|------|------|------|----------|
| Phase 1 | 4 周 | 1-2 人 | 接口设计复杂度 |
| Phase 2 | 6 周 | 2-3 人 | 循环正确性 |
| Phase 3 | 8 周 | 2-3 人 | 工具系统复杂度 |
| Phase 4 | 4 周 | 1-2 人 | MCP 协议实现 |
| Phase 5 | 6 周 | 2-3 人 | TUI 交互设计 |
| **总计** | **28 周** | **2-3 人** | - |

## 里程碑

### M1: 基础抽象层完成 (Week 4)
- [ ] Kaos 接口定义
- [ ] LocalKaos 实现
- [ ] Config 系统
- [ ] Kosong ChatProvider

### M2: Agent 核心引擎完成 (Week 10)
- [ ] runTurn 无状态循环
- [ ] Agent 类
- [ ] ContextMemory
- [ ] TurnFlow

### M3: 服务层完成 (Week 18)
- [ ] AgentRecords 事件溯源
- [ ] Session 会话管理
- [ ] ToolManager 和内置工具
- [ ] PermissionManager 三级权限
- [ ] HookEngine 13 种钩子事件

### M4: 扩展系统完成 (Week 22)
- [ ] SkillManager 技能系统
- [ ] McpConnectionManager MCP 集成

### M5: 应用层完成 (Week 28)
- [ ] CLI 入口
- [ ] TUI 终端界面
- [ ] SDK 公共 API

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

## 风险管理

### 技术风险
1. **C++ 实现复杂度**
   - 缓解：使用 RAII、智能指针、Result<T>
2. **性能开销**
   - 缓解：使用内联、模板、编译时优化
3. **测试覆盖**
   - 缓解：使用 doctest，编写单元测试和集成测试

### 进度风险
1. **范围蔓延**
   - 缓解：严格遵循路线图，分阶段交付
2. **依赖冲突**
   - 缓解：渐进式替换，保持兼容性
3. **资源不足**
   - 缓解：优先实现高价值模块，逐步完善

## 参考资源

- [Kimi Code 架构分析](../kimi-code-analysis/INDEX.md)
- [OpenAI 智能体优先工程](https://openai.com/index/codex-in-an-agent-first-world/)
- [当前项目架构](../plan/re-build/ARCHITECTURE.md)
