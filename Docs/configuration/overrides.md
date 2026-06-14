# 配置覆盖

CodeHarness 的配置覆盖顺序固定，便于判断最终生效值。

## 优先级

从低到高：

1. 内置默认值
2. `settings.json`
3. 环境变量
4. 命令行参数
5. `active_profile` 解析 API key 等缺省字段

命令行参数通常只影响当前进程。环境变量适合当前 shell 或 CI。`settings.json` 适合长期默认值。

## 命令行选项

| 选项 | 覆盖字段 |
| --- | --- |
| `--profile` | `active_profile` |
| `--provider` | `provider_type` |
| `--model` | `model` |
| `--api-key` | `api_key` |
| `--base-url` | `base_url` |
| `--max-turns` | `max_turns` |
| `--cwd` | 当前工作目录 |
| `--plan` | 启动权限模式设为 `plan` |

## 典型场景

临时切到 echo provider：

```powershell
codeharness --provider echo -p "检查 CLI 是否能启动"
```

使用指定 profile：

```powershell
codeharness --profile work
```

在 CI 中限制轮数：

```sh
CODEHARNESS_MAX_TURNS=20 codeharness -p "run focused checks"
```

## 注意事项

如果命令行直接传了 `--provider`、`--model` 或 `--base-url`，运行时会把它视为显式 provider 配置，不再把 active profile 作为当前模型切换状态的唯一来源。
