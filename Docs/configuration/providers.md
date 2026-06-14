# 平台与模型

CodeHarness 当前支持三类 provider：`openai`、`anthropic`、`echo`。模型和凭据通过 `profiles` 组织，TUI 可以基于 profile 列表切换当前会话使用的模型配置。

## `openai`

OpenAI-compatible provider 使用 `provider_type = "openai"`。

```json
{
  "active_profile": "openai-main",
  "profiles": {
    "openai-main": {
      "name": "openai-main",
      "label": "OpenAI",
      "provider_type": "openai",
      "model": "gpt-4.1",
      "base_url": "https://api.openai.com/v1",
      "auth_source": "env:OPENAI_API_KEY"
    }
  }
}
```

如果 `auth_source` 为空，CodeHarness 会回退读取 `OPENAI_API_KEY`。

## `anthropic`

Anthropic provider 使用 `provider_type = "anthropic"`。

```json
{
  "profiles": {
    "claude": {
      "name": "claude",
      "label": "Claude",
      "provider_type": "anthropic",
      "model": "claude-3-5-sonnet-latest",
      "auth_source": "env:ANTHROPIC_API_KEY"
    }
  }
}
```

如果 `auth_source` 为空，CodeHarness 会回退读取 `ANTHROPIC_API_KEY`。

## `echo`

`echo` provider 用于本地调试，不需要网络和 API key。

```json
{
  "profiles": {
    "local-echo": {
      "name": "local-echo",
      "label": "Echo",
      "provider_type": "echo"
    }
  }
}
```

## API key 来源

`auth_source` 支持三种形式：

| 写法 | 行为 |
| --- | --- |
| `env:VAR_NAME` | 从环境变量读取 |
| `credentials:PROFILE` | 从 `credentials.json` 的 `profiles.PROFILE.api_key` 读取 |
| 非空普通字符串 | 作为字面 API key 使用 |

更推荐 `env:` 或 `credentials:`，避免把密钥写进可共享配置。
