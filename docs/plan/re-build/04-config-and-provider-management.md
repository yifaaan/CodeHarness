# Config and Provider Management

**Source**: `packages/agent-core/src/config/` — configuration schema and provider resolution

## Purpose

This layer handles:
1. **Configuration schema and persistence**: Loading, validating, and writing the `config.toml` file
2. **Provider Manager**: Resolving model name strings → concrete `ChatProvider` instances with auth
3. **OAuth integration**: Injecting OAuth tokens into provider request auth

## Config Schema

### Config File Format

Kimi Code CLI uses **TOML** for its configuration file, located at `~/.kimi-code/config.toml` (or `$KIMI_CODE_HOME/config.toml`).

### Top-Level Fields

```toml
# --- General ---
default_model = "claude-sonnet-4"        # Default model alias
default_thinking = false                 # New sessions start with thinking off
default_permission_mode = "manual"       # manual | auto | yolo
default_plan_mode = false                # New sessions in plan mode?
merge_all_available_skills = true        # Merge skills from all dirs?
extra_skill_dirs = ["/path/to/skills"]   # Additional skill directories
telemetry = true                         # Anonymous telemetry

# --- Provider declarations ---
[providers]                              # Table of provider configs
  [providers.my-anthropic]
  type = "anthropic"
  api_key = "sk-ant-..."                 # Or use [providers.my-anthropic.env]
  
  [providers.my-openai]
  type = "openai"
  api_key = "sk-proj-..."
  base_url = "https://my-proxy.example.com/v1"
  
  [providers.moonshot]
  type = "kimi"
  api_key = "..."                        # Moonshot API key

# --- Model aliases ---
[models]
  "claude-sonnet-4" = { provider = "my-anthropic", model = "claude-sonnet-4-20250514" }
  "gpt-4o" = { provider = "my-openai", model = "gpt-4o" }
  "kimi-k2" = { provider = "moonshot", model = "kimi-k2" }

# --- Provider env sub-table ---
[providers.my-anthropic.env]
ANTHROPIC_API_KEY = "sk-ant-..."         # Alternative to api_key field
ANTHROPIC_BASE_URL = "https://..."       # Override base URL
```

### Zod Schema (Conceptual)

```typescript
interface KimiConfig {
  defaultModel?: string;
  defaultThinking?: boolean;
  defaultPermissionMode?: 'manual' | 'auto' | 'yolo';
  defaultPlanMode?: boolean;
  mergeAllAvailableSkills?: boolean;
  extraSkillDirs?: string[];
  telemetry?: boolean;
  
  providers?: Record<string, ProviderConfig>;
  models?: Record<string, ModelAlias>;
  thinking?: ThinkingConfig;
  loopControl?: LoopControlConfig;
  background?: BackgroundConfig;
  services?: ServicesConfig;
  permission?: PermissionConfig;
  hooks?: HookDef[];
}

interface ProviderConfig {
  type: 'anthropic' | 'openai' | 'kimi' | 'google-genai' | 'openai_responses' | 'vertexai';
  apiKey?: string;
  baseUrl?: string;
  oauth?: string;           // Reference to OAuth provider
  env?: Record<string, string>;  // Provider-specific env vars
  customHeaders?: Record<string, string>;
  clientFactory?: (auth: ProviderRequestAuth) => any;  // Advanced: custom SDK client
  // Provider-specific options:
  maxTokens?: number;
  thinkingBudget?: number;
}

interface ModelAlias {
  provider: string;     // References [providers.<name>]
  model: string;        // Model name as the provider knows it
}

interface ThinkingConfig {
  mode?: 'on' | 'off' | 'auto';
  budgetTokens?: number;
  effort?: 'low' | 'medium' | 'high';
}

interface PermissionConfig {
  rules?: PermissionRule[];
}

interface PermissionRule {
  decision: 'allow' | 'deny' | 'ask';
  scope?: 'turn-override' | 'session-runtime' | 'project' | 'user';
  pattern: string;        // DSL: "Read(/etc/**)", "Bash(rm *)"
  reason?: string;
  expiryMs?: number;
}

interface BackgroundConfig {
  keepAliveOnExit?: boolean;
  defaultTimeout?: number;
}
```

### Config Resolution

```
                           ┌───────────────┐
                           │  config.toml  │
                           │  (TOML file)  │
                           └───────┬───────┘
                                   │ smol-toml parse
                                   ▼
                           ┌───────────────┐
                           │ RawConfig     │
                           │ (unvalidated) │
                           └───────┬───────┘
                                   │ zod parse + transform
                                   ▼
                           ┌───────────────┐
                           │  KimiConfig   │
                           │ (validated)   │
                           └───────┬───────┘
                                   │
                    ┌──────────────┼──────────────┐
                    │              │              │
                    ▼              ▼              ▼
            ┌────────────┐ ┌────────────┐ ┌────────────┐
            │ Provider   │ │ Permission │ │  Hooks     │
            │  Manager   │ │  Manager   │ │  Engine    │
            └────────────┘ └────────────┘ └────────────┘
```

## Provider Manager

**Source**: `packages/agent-core/src/providers/`

The `ProviderManager` is responsible for converting model name strings to callable `ChatProvider` instances.

```typescript
class ProviderManager {
  constructor(config: KimiConfig, authTokenResolver?: OAuthTokenProviderResolver);

  /**
   * Resolve a model name to a runtime provider.
   * Looks up the model alias → finds provider config → creates ChatProvider.
   */
  resolveProviderForModel(modelName: string): ResolvedRuntimeProvider;

  /**
   * Resolve just the provider config for a model (no ChatProvider creation).
   */
  resolveProviderConfigForModel(modelName: string): ResolvedProviderConfig;

  /**
   * Create an auth resolver for a specific model.
   * Returns a function that provides per-request auth (for OAuth token injection).
   */
  createAuthResolverForModel(modelName: string): ProviderRequestAuthResolver;

  /**
   * Return a new ProviderManager with a prompt cache key set.
   */
  withPromptCacheKey(key: string): ProviderManager;
}

interface ResolvedRuntimeProvider {
  provider: ChatProvider;          // The kosong ChatProvider instance
  model: string;                   // The actual model name
  providerName: string;            // Provider config name from config.toml
  providerType: ProviderType;      // anthropic, openai, etc.
}

interface ResolvedProviderConfig {
  providerType: string;
  modelName: string;
  maxTokens: number;
  supportsThinking: boolean;
  supportsImages: boolean;
  supportsVideos: boolean;
}
```

### Resolution Algorithm

```
resolveProviderForModel("claude-sonnet-4"):
  1. Look up "claude-sonnet-4" in config.models
     → { provider: "my-anthropic", model: "claude-sonnet-4-20250514" }
  
  2. Look up "my-anthropic" in config.providers
     → { type: "anthropic", apiKey: "sk-ant-...", baseUrl: "..." }
  
  3. Resolve auth:
     a. If apiKey field is set → use it
     b. If [providers.my-anthropic.env] has ANTHROPIC_API_KEY → use it
     c. If OAuth token resolver is available → get token
     d. Error: "missing credentials"
  
  4. Create ChatProvider:
     → AnthropicProvider(apiKey, baseUrl, modelName)
  
  5. Apply thinking config (if configured):
     → provider.withThinking(config.thinking.effort)
  
  6. Return ResolvedRuntimeProvider { provider, model, providerName, providerType }
```

## OAuth Integration

**Source**: `packages/kimi-code-oauth/src/`

```typescript
interface OAuthTokenProviderResolver {
  (providerName: string): ProviderRequestAuth | null;
}
```

The OAuth system supports two flows:

1. **Device Code Flow** (for Kimi Code managed auth):
   - Browser opens with device code
   - User authenticates in browser
   - CLI polls for token completion
   - Token stored in `~/.kimi-code/credentials/` (file-based, proper-lockfile for concurrency)

2. **API Key Flow** (for Moonshot AI open platform):
   - User provides API key via config or login prompt
   - Key stored in config file

### Token Storage

```
~/.kimi-code/
├── credentials/
│   ├── oauth/                    # OAuth tokens per provider
│   │   └── <provider-name>.json  # Encrypted/serialized tokens
│   └── mcp/                      # MCP server OAuth tokens
│       └── <server-name>.json
```

## Provider Types and Their SDKs

| Provider Type | SDK Used | Auth Method | Notes |
|--------------|----------|-------------|-------|
| `anthropic` | `@anthropic-ai/sdk` | API key | Supports prompt caching |
| `openai` | `openai` (Chat Completions) | API key | Wide compatibility |
| `openai_responses` | `openai` (Responses API) | API key | Newer OpenAI API |
| `kimi` | `openai` SDK (compatible) | API key | Moonshot AI |
| `google-genai` | `@google/genai` | API key | Google AI Studio |
| `vertexai` | `@google/genai` (Vertex) | ADC or API key | GCP Vertex AI |

## Re-implementation Notes

1. **Config is the first integration point**: Port the TOML schema first. It defines how providers, models, permissions, hooks, MCP servers, and background tasks are configured.

2. **ProviderManager is a factory**: It takes a model alias like `"claude-sonnet-4"` and produces a `ChatProvider`. The resolution chain is: model alias → provider config → credentials → SDK client → ChatProvider wrapper.

3. **Two credential sources**: Both `apiKey` field and `[providers.<name>.env]` sub-table must be checked. The `env` sub-table is NOT the process environment — it's a TOML table that happens to use environment variable names as keys.

4. **OAuth is optional**: For a reimplementation, API key authentication is sufficient. OAuth adds device code flow + token file storage.

5. **Config validation is critical**: Malformed config should produce clear error messages. The zod schema validates types, ranges, and required fields.
