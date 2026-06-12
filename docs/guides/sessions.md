# 会话与上下文

## 会话存储

CodeHarness 会话快照由 `SessionStore` 管理，默认位于：

```text
<data_dir>/sessions
```

数据目录默认是 `~/.codeharness/data`，可通过 `CODEHARNESS_DATA_DIR` 覆盖。

## 查看历史会话

在斜杠命令中使用：

```text
/sessions
/sessions 20
```

不传参数时列出默认数量的历史快照；传入数字时作为返回数量上限。

## 恢复会话

```text
/resume <session-id>
```

恢复结果会显示 session id、模型、消息数量和摘要。实际可恢复程度取决于当前运行时对 session snapshot 的支持。

## 上下文压缩

源码中已有 `PreCompact` 和 `PostCompact` hook 事件枚举，但完整的用户可见压缩命令和流程仍属于后续工作。计划见 [上下文压缩计划](../plan/context-compaction.md)。

## 派生与导出

会话 fork、导出和标题管理尚未作为用户命令落地。计划见 [会话 fork/export 计划](../plan/session-fork-export.md)。
