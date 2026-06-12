# Config Schema

Configuration system and provider management.

## One-Liner

TOML-based configuration with Zod validation, supporting multiple LLM providers, model aliases, and OAuth authentication.

## Config File Location

`~/.kimi-code/config.toml` (or `$KIMI_CODE_HOME/config.toml`)

## Schema Overview

```toml
# General
default_model = "claude-sonnet-4"
default_thinking = false
default_permission_mode = "manual"  # manual | auto | yolo

# Provider declarations
[providers.my-anthropic]
type = "anthropic"
api_key = "sk-ant-..."

[providers.my-openai]
type = "openai"
api_key = "sk-proj-..."

# Model aliases
[models]
"claude-sonnet-4" = { provider = "my-anthropic", model = "claude-sonnet-4-20250514" }
"gpt-4o" = { provider = "my-openai", model = "gpt-4o" }
```

## Key Concepts

1. **Provider Resolution Chain**:
   ```
   Model alias → Provider config → Credentials → ChatProvider
   ```

2. **Credential Sources** (in order):
   - `api_key` field in config
   - `[providers.<name>.env]` sub-table
   - OAuth token resolver

3. **Permission Rules**:
   ```toml
   [[permission.rules]]
   decision = "allow"  # allow | deny | ask
   scope = "session-runtime"  # turn-override | session-runtime | project | user
   pattern = "Read(/etc/**)"  # ToolName(glob-pattern)
   ```

## ProviderManager

```typescript
class ProviderManager {
  resolveProviderForModel(modelName: string): ResolvedRuntimeProvider;
  resolveProviderConfigForModel(modelName: string): ResolvedProviderConfig;
  createAuthResolverForModel(modelName: string): ProviderRequestAuthResolver;
}
```

## OAuth Integration

- Device code flow for managed auth
- Token storage in `~/.kimi-code/credentials/`
- Per-request auth injection via `ProviderRequestAuthResolver`

## See Also

- [kosong-interface.md](kosong-interface.md) — Provider interface
- [permission-hooks.md](permission-hooks.md) — Permission rules
