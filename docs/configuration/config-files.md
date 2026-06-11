# 配置文件

CodeHarness 使用 JSON 配置。全局配置文件默认位于 `~/.codeharness/settings.json`，凭据文件默认位于 `~/.codeharness/credentials.json`。配置加载顺序是：

1. 内置默认值
2. `settings.json`
3. 环境变量
4. 命令行参数
5. 解析 `active_profile`

## 配置文件位置

默认配置目录由 `CODEHARNESS_CONFIG_DIR` 覆盖，否则使用用户主目录下的 `.codeharness`。

```powershell
$env:CODEHARNESS_CONFIG_DIR = "D:\codeharness-config"
```

数据目录由 `CODEHARNESS_DATA_DIR` 覆盖，否则使用 `<config_dir>/data`。

## 顶层字段

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `active_profile` | string | `default` | 启动时使用的 profile id |
| `profiles` | object | `{ default: ... }` | 命名模型/供应商配置 |
| `provider_type` | string | `openai` | 当前供应商类型：`openai`、`anthropic`、`echo` |
| `model` | string | 空 | 模型名称，省略时由供应商默认处理 |
| `base_url` | string | 空 | API 基础 URL |
| `max_tokens` | integer | `4096` | 单次请求输出预算 |
| `max_turns` | integer | `200` | Agent loop 最大轮数 |
| `permission` | object | `default` | 权限模式和规则 |
| `mcp_servers` | array | `[]` | MCP server 配置 |
| `hooks` | array | `[]` | 生命周期 hook 配置 |
| `allow_project_skills` | boolean | `true` | 是否加载项目级 Skills |
| `allow_project_plugins` | boolean | `false` | 是否加载项目级 Plugins |
| `config_dir` | string | 自动解析 | 配置目录 |
| `data_dir` | string | 自动解析 | 数据目录 |
| `memory_root` | string | `<data_dir>/memory` | Memory 存储根目录 |

## 完整示例

```json
{
  "active_profile": "default",
  "profiles": {
    "default": {
      "name": "default",
      "label": "OpenAI default",
      "provider_type": "openai",
      "model": "gpt-4.1",
      "base_url": "https://api.openai.com/v1",
      "auth_source": "env:OPENAI_API_KEY"
    },
    "local-echo": {
      "name": "local-echo",
      "label": "Echo",
      "provider_type": "echo"
    }
  },
  "max_turns": 200,
  "permission": {
    "mode": "default",
    "denied_tools": ["bash"],
    "path_rules": [
      {
        "action": "deny",
        "pattern": "**/.env",
        "tools": ["read_file", "write_file", "edit_file"]
      }
    ],
    "command_rules": [
      {
        "action": "ask",
        "pattern": "git\\s+push"
      }
    ]
  },
  "mcp_servers": [
    {
      "transport": "stdio",
      "name": "filesystem",
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "."]
    }
  ],
  "hooks": [
    {
      "event": "PreToolUse",
      "type": "command",
      "matcher": "bash",
      "priority": 0,
      "timeout_seconds": 5,
      "config": {
        "command": "node D:/codeharness-hooks/check-bash.mjs"
      }
    }
  ]
}
```

## `profiles`

每个 profile 描述一个可切换的模型运行时配置。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | string | profile 名称，通常与 key 相同 |
| `label` | string | UI 中显示的名称 |
| `provider_type` | string | `openai`、`anthropic` 或 `echo` |
| `api_format` | string | 预留字段，当前主要由 provider 类型决定 |
| `model` | string | 模型标识 |
| `base_url` | string | API URL |
| `auth_source` | string | `env:VAR`、`credentials:NAME` 或字面 API key |
| `extra_headers` | object | 自定义请求头，当前配置结构保留 |

`credentials.json` 可保存按 profile 命名的 API key：

```json
{
  "profiles": {
    "work": {
      "api_key": "sk-..."
    }
  }
}
```

对应 `auth_source` 写为 `credentials:work`。

## `permission`

权限模式取值：

| 值 | 说明 |
| --- | --- |
| `default` | 只读工具自动放行，写入和执行类工具需要确认 |
| `plan` | 只允许只读分析，禁止修改 |
| `full_auto` | 普通操作自动放行，敏感路径和 deny 规则仍会拦截 |

规则字段：

- `allowed_tools`：精确工具名白名单。
- `denied_tools`：精确工具名黑名单。
- `path_rules`：按 glob 匹配路径，可限制到指定工具。
- `command_rules`：按正则匹配命令字符串，主要用于 `bash`。

## `mcp_servers`

MCP 支持 stdio 和 HTTP 两类配置。详见 [Model Context Protocol](../customization/mcp.md)。

## `hooks`

Hooks 使用数组配置，事件名和配置形状详见 [Hooks](../customization/hooks.md)。
