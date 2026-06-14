# 环境变量

环境变量位于配置文件之后、命令行参数之前。它们适合在不同 shell、CI 或本地 profile 之间切换运行时配置。

## 核心路径

| 变量 | 说明 |
| --- | --- |
| `CODEHARNESS_CONFIG_DIR` | 覆盖配置目录 |
| `CODEHARNESS_DATA_DIR` | 覆盖数据目录 |
| `CODEHARNESS_MEMORY_ROOT` | 覆盖 Memory 根目录 |

## 供应商与模型

| 变量 | 说明 |
| --- | --- |
| `CODEHARNESS_PROFILE` | 覆盖 `active_profile` |
| `CODEHARNESS_PROVIDER` | 覆盖供应商类型 |
| `CODEHARNESS_MODEL` | 覆盖模型 |
| `CODEHARNESS_API_KEY` | 直接提供 API key |
| `CODEHARNESS_BASE_URL` | 覆盖 API URL |
| `CODEHARNESS_MAX_TURNS` | 覆盖最大 agent loop 轮数 |

## Provider 默认凭据

当 `auth_source` 为空且没有其它显式 API key 时：

- `openai` 会读取 `OPENAI_API_KEY`
- `anthropic` 会读取 `ANTHROPIC_API_KEY`
- `echo` 不需要 API key

## 示例

```powershell
$env:CODEHARNESS_PROVIDER = "anthropic"
$env:CODEHARNESS_MODEL = "claude-3-5-sonnet-latest"
$env:ANTHROPIC_API_KEY = "sk-ant-..."
codeharness -p "总结这个仓库"
```

```sh
CODEHARNESS_PROFILE=local-echo codeharness -p "hello"
```
