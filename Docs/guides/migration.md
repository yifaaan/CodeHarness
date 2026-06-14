# 迁移到 CodeHarness

## 迁移目标

CodeHarness 不是配置兼容层，而是 C++20 runtime 的实现。迁移时应把旧工具、命令和配置概念映射到当前源码中真实存在的接口。

## 配置迁移

把旧的供应商和模型配置合并为 `settings.json` 的 `profiles`：

```json
{
  "active_profile": "work",
  "profiles": {
    "work": {
      "name": "work",
      "label": "Work model",
      "provider_type": "openai",
      "model": "gpt-4.1",
      "auth_source": "credentials:work"
    }
  }
}
```

把 API key 放入 `credentials.json` 或环境变量。

## 命令迁移

当前 CodeHarness 只实现源码中注册的命令。常见映射：

| 目标 | CodeHarness 当前入口 |
| --- | --- |
| 模式切换 | `/plan`、`/act`、`/fullauto`、`/default` |
| 查看权限模式 | `/mode` |
| Skill 列表 | `/skills` |
| Memory 管理 | `/memory` |
| 会话列表 | `/sessions` |
| 恢复会话 | `/resume <id>` |
| Plugin 列表 | `/plugin` |

未实现的命令不要写入用户手册的已支持列表；对应计划见 [命令补齐计划](../plan/kimi-style-command-parity.md)。

## 工具迁移

工具名使用 snake_case，例如 `read_file`、`write_file`、`bash`。不要把其它 CLI 的 PascalCase 工具名直接放入 CodeHarness 配置。
