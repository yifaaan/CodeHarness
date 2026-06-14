# 交互与输入

## TUI

不带 `-p/--prompt` 启动时，CodeHarness 进入终端交互界面。TUI 会显示会话状态、模型信息、目录、Skill 数量和对话 transcript。

```powershell
codeharness
```

## 非交互执行

使用 `-p` 或 `--prompt` 执行单条指令：

```powershell
codeharness -p "运行项目测试并总结结果"
```

如果 prompt 以 `/` 开头，会先按斜杠命令解析；动态 Skill 命令可渲染成 prompt 后继续进入 agent loop。

## Plan 模式

Plan 模式用于只读分析。启动参数：

```powershell
codeharness --plan
```

斜杠命令：

```text
/plan
/act
```

`/plan` 会切到只读分析，`/act` 回到默认权限模式。

## Full-auto 模式

```text
/fullauto
/full_auto
/default
```

Full-auto 会让普通 mutating 工具自动放行，但敏感路径和 deny 规则仍应被权限系统拦截。

## Backend-only

`--backend-only` 启动 JSON Lines 协议模式，适合外部 UI 或测试宿主通过 stdin/stdout 驱动 runtime。

```powershell
codeharness --backend-only
```

## 当前未实现的交互能力

主题切换、外部编辑器、图片/视频粘贴、上下文压缩 UI、会话 fork/export 等能力记录在 `docs/plan/` 中。
