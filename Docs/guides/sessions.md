# 会话与上下文

## 会话存储

CodeHarness 会话由 `SessionStore`（`codeharness::session`）管理，目录布局为：

```text
<sessions_root>/<workdir-key>/<session-id>/
├── state.json              # {title, createdAt, updatedAt, workdir}
└── agents/
    └── main/
        └── wire.jsonl      # 事件流（对话历史的真源）
```

其中：

- `sessions_root` 解析规则与配置文件一致：设置了 `CODEHARNESS_HOME` 时为 `$CODEHARNESS_HOME/sessions`，否则为 `$HOME/.codeharness/sessions`。
- `workdir-key` 是创建会话时所在工作目录绝对路径的编码（可读前缀 + FNV-1a-64 哈希后缀），保证文件系统安全且可调试。
- `session-id` 形如 `sess_<unix-ms>_<6-hex>`，在单用户本地场景下唯一。
- 根目录下还有 `session_index.jsonl`，作为 `{sessionId, workdir, title, createdAt}` 的追加式快速查找缓存。

所有 I/O 都通过 `Host*` 抽象，无直接磁盘访问，便于测试。

## 会话生命周期

- **创建**：`Session::Create(store, cfg)` 分配目录、写入初始 `state.json`，并把 `Agent` + `AgentRecords` 接到计算好的 `wire.jsonl` 路径上。
- **恢复**：`Session::Resume(store, cfg, sessionId)`（`sessionId` 可用唯一前缀）定位目录、重建 `Agent` + `Records`，并调用 `Agent::Resume()` 把 `wire.jsonl` 回放进内存历史。回放期间 Records 的 `restoring_` 守卫会阻止重复记录。
- **关闭**：`Session::Close()` 刷新 records、以原子方式（写临时文件 + `Host::Rename`）更新 `state.json` 的 `updatedAt`。

## 查看历史会话

`SessionStore::List(workdir)` 返回该工作目录下的所有会话元信息。计划中的斜杠命令（尚未在 CLI/TUI 层落地）：

```text
/sessions
/sessions 20
```

## 恢复会话

```text
/resume <session-id>
```

`<session-id>` 支持唯一前缀匹配（`SessionStore::Find`）。恢复结果会显示 session id、模型、消息数量和摘要。当前 MVP 已支持单 `'main'` agent 的创建/恢复/关闭往返；子 agent、fork、export 见下文"待办"。

## 上下文压缩

源码中已有 `PreCompact` 和 `PostCompact` hook 事件枚举，但完整的用户可见压缩命令和流程仍属于后续工作（Context/Compaction 模块，执行计划 #06）。

## 待办（执行计划 #09 后续）

当前 Session MVP 已落地：`SessionStore`（create/get/list/find/remove/rename + `state.json` + `session_index.jsonl`）、`Session`（单 `'main'` agent 的 create/resume/close）、`Host::Remove`/`Rename`。以下仍属后续工作：

- **RPC 协议**（CoreAPI/AgentAPI，双向，UI ↔ agent core）
- **派生与导出**：会话 fork、export、标题管理作为用户命令落地
- **子 agent**：当前每个 session 只有一个 `'main'` agent
- **Skill/MCP/Hook 归属**：等对应模块上线

