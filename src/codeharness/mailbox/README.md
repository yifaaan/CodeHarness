# mailbox/ — 智能体间消息邮箱模块

## 设计目标

为多智能体系统提供跨进程、可持久化的消息通信机制。基于文件系统实现，天然支持 crash-safe 和进程间通信。

## 架构

```
Mailbox (文件系统队列)
  ├─ root/{task_id}/inbox/{msg_id}.json    ← 消息存储
  ├─ send(to_agent, payload)               ← 发送消息
  ├─ poll()                                ← 读取未读消息
  ├─ mark_read(msg_id)                     ← 标记已读
  └─ clear()                               ← 清空邮箱

WorkerMailboxDrain (消费辅助)
  └─ drain_worker_mailbox()                ← 读取+标记+分类

TeamLifecycleManager (团队管理)
  └─ create_team / delete_team / add_member / remove_member

SendMessageTool (LLM 可调用的工具)
  └─ 让智能体通过 Mailbox 发送消息给其他智能体
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `Mailbox` | 核心消息队列，线程安全、跨进程 |
| `WorkerMailboxDrain` | 封装读取+标记+分类的模式 |
| `TeamLifecycleManager` | 管理团队配置文件 `{root}/{team}/team.json` |
| `SendMessageTool` | 封装为 Tool 接口，供 LLM 调用 |

## 设计要点

- 基于文件系统而非内存队列：消息写入先写临时文件再 `rename`，保证原子性和 crash-safety
- 跨进程：子智能体和父智能体通过同一文件系统路径通信
- 消息分类：`drain_worker_mailbox()` 将消息分为用户消息、任务结果、权限请求、关闭信号等

## 初学者指南

- 这个模块只在多智能体模式下使用
- 消息格式是 JSON，包含 sender、type、payload、timestamp 等字段
- 核心路径：`Agent A` → `SendMessageTool` → `Mailbox::send(Agent B, payload)` → `Agent B` 通过 `WorkerMailboxDrain` 轮询接收
