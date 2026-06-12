# Kosong — LLM Provider Abstraction Layer

**Source**: `packages/kosong/src/`

## Purpose

Kosong provides a **unified interface for interacting with LLM providers** (Anthropic, OpenAI, Google GenAI, Kimi/Moonshot). It abstracts away the differences between provider APIs — streaming formats, tool call protocols, thinking blocks, image/video content, authentication — behind a single `ChatProvider` interface.

The agent never calls provider SDKs directly. It only talks to kosong's `generate()` function, which routes to the appropriate provider implementation.

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│                       agent-core                             │
│  KosongLLM wraps ChatProvider + generate() → used by Loop    │
└─────────────────────���────┬───────────────────────────────────┘
                           │ calls
                           ▼
┌──────────────────────────────────────────────────────────────┐
│  kosong package                                              │
│                                                               │
│  generate(systemPrompt, tools, history, options?)            │
│     │                                                         │
│     └──> provider.generate(...) → StreamedMessage            │
│            │                                                  │
│            ├── AnthropicProvider                              │
│            │     └── @anthropic-ai/sdk                        │
│            │                                                  │
│            ├── OpenAILegacyProvider                           │
│            │     └── openai (chat completions)                │
│            │                                                  │
│            ├── OpenAIResponsesProvider                        │
│            │     └── openai (responses API)                   │
│            │                                                  │
│            ├── GoogleGenAIProvider                            │
│            │     └── @google/genai                            │
│            │                                                  │
│            └── KimiProvider                                   │
│                  └── openai SDK + file upload extensions      │
└──────────────────────────────────────────────────────────────┘
```

## Core Interface

### ChatProvider

**Source**: `packages/kosong/src/provider.ts`

```typescript
interface ChatProvider {
  /** Provider name: "anthropic", "openai", "kimi", "google-genai" */
  readonly name: string;
  
  /** The specific model name, e.g. "claude-sonnet-4-20250514" */
  readonly modelName: string;
  
  /** Current thinking effort level, or null if unsupported */
  readonly thinkingEffort: ThinkingEffort | null;

  /**
   * Generate a response from the LLM.
   * 
   * @param systemPrompt - System instructions (may include skill prompts, permissions, etc.)
   * @param tools - Tool definitions available to the model
   * @param history - Conversation history (user/assistant/tool messages)
   * @param options - Signal for cancellation, auth override
   * @returns StreamedMessage — an async iterable of content parts
   */
  generate(
    systemPrompt: string,
    tools: Tool[],
    history: Message[],
    options?: GenerateOptions,
  ): Promise<StreamedMessage>;

  /** Returns a new provider instance with the given thinking effort */
  withThinking(effort: ThinkingEffort): ChatProvider;

  /** Returns a new provider with max completion tokens adjusted */
  withMaxCompletionTokens?(max: number): ChatProvider;

  /** Upload video for multimodal input (optional) */
  uploadVideo?(input: VideoUploadInput, options?: VideoUploadOptions): Promise<VideoURLPart>;

  /** Get model capabilities (optional) */
  getCapability?(model?: string): ModelCapability;
}

interface GenerateOptions {
  signal?: AbortSignal;
  auth?: ProviderRequestAuth;  // Per-request auth override
}

interface ProviderRequestAuth {
  apiKey?: string;
  headers?: Record<string, string>;
}
```

### StreamedMessage

```typescript
interface StreamedMessage {
  /** Async generator that yields content parts as they arrive */
  [Symbol.asyncIterator](): AsyncIterator<StreamedMessagePart>;
  
  /** Message ID from provider */
  id: string;
  
  /** Token usage (populated after streaming completes) */
  usage: TokenUsage | null;
  
  /** Normalized finish reason */
  finishReason: FinishReason | null;
  
  /** Raw finish reason from provider */
  rawFinishReason: string | null;
}
```

### Message and Content Types

**Source**: `packages/kosong/src/message.ts`

```typescript
// --- Message roles ---
type Message = UserMessage | AssistantMessage | ToolMessage;

interface UserMessage {
  role: 'user';
  content: ContentPart[];
}

interface AssistantMessage {
  role: 'assistant';
  content: ContentPart[];
  toolCalls?: ToolCall[];
  thinking?: ThinkPart;  // non-streaming thinking content
}

interface ToolMessage {
  role: 'tool';
  toolCallId: string;
  content: ToolResultContent[];
}

// --- Content Parts ---
type ContentPart = TextPart | ThinkPart | ImageURLPart | AudioURLPart | VideoURLPart;

interface TextPart { type: 'text'; text: string; }
interface ThinkPart { type: 'think'; think: string; encrypted?: string; }

interface ImageURLPart {
  type: 'image_url';
  imageUrl: { url: string; id?: string; };
}

interface AudioURLPart {
  type: 'audio_url';
  audioUrl: { url: string; id?: string; };
}

interface VideoURLPart {
  type: 'video_url';
  videoUrl: { url: string; id?: string; };
}

// --- Streaming parts ---
type StreamedMessagePart = ContentPart | ToolCall;

// --- Tool Calls ---
interface ToolCall {
  type: 'function';
  id: string;
  function: { name: string; arguments: string | null; };
  extras?: Record<string, unknown>;
  _streamIndex?: number | string;  // For routing parallel tool call deltas
}
```

### Streaming Merge

The `mergeInPlace()` function handles progressive streaming accumulation:

```
Input: a stream of parts arrives one by one
  - TextPart + TextPart → concatenate text
  - ThinkPart + ThinkPart → concatenate think text (preserve encrypted signature if present)
  - ToolCall + ToolCall (same id) → merge arguments via string concatenation
  - New ToolCall → append to pending tool calls list
  - _streamIndex: routes interleaved argument deltas to the correct ToolCall
```

### Tool Definitions

**Source**: `packages/kosong/src/tool.ts`

```typescript
interface Tool {
  name: string;
  description: string;
  inputSchema: JSONSchema;    // JSON Schema for tool parameters
  type?: 'function' | 'builtin_function';  // builtin_function for Kimi $tools
}
```

## Capability System

**Source**: `packages/kosong/src/capability.ts`

```typescript
interface ModelCapability {
  image_in: boolean;       // Can accept image inputs
  video_in: boolean;       // Can accept video inputs
  audio_in: boolean;       // Can accept audio inputs
  thinking: boolean;       // Supports thinking/reasoning
  tool_use: boolean;       // Supports tool calling
  max_context_tokens: number;  // Maximum context window size
}
```

`UNKNOWN_CAPABILITY` is returned for uncatalogued models — a non-fatal fallback that allows private deployments and future models to work without code changes.

## Thinking/Reasoning System

**ThinkingEffort** is normalized across all providers:

```typescript
type ThinkingEffort = 'off' | 'low' | 'medium' | 'high' | 'xhigh' | 'max';
```

Each provider maps this to its own API:

| Kosong Effort | Anthropic | OpenAI | Google GenAI | Kimi |
|--------------|-----------|--------|-------------|------|
| `off` | disabled | no reasoning | disabled | `thinking: {type: "disabled"}` |
| `low` | `low` | `reasoning_effort: "low"` | `thinking_level: "LOW"` | `thinking: {type: "enabled", budget_tokens: 1024}` |
| `medium` | `medium` | `reasoning_effort: "medium"` | `thinking_level: "MEDIUM"` | enabled |
| `high` | `high` | `reasoning_effort: "high"` | `thinking_level: "HIGH"` | enabled |
| `xhigh` | `high` (max) | not supported | not supported | enabled |
| `max` | max budget | not supported | not supported | enabled |

## Finish Reason Normalization

```typescript
type FinishReason = 'completed' | 'tool_calls' | 'truncated' | 'filtered' | 'paused' | 'other';
```

Each provider maps its stop reasons:

| Kosong | Anthropic | OpenAI (chat) | OpenAI (responses) | Google GenAI |
|--------|-----------|--------------|-------------------|-------------|
| `completed` | `end_turn` | `stop` | `"completed"` | `STOP` |
| `tool_calls` | `tool_use` | `tool_calls` | `"incomplete"` (with tool calls) | `TOOL_USE` |
| `truncated` | `max_tokens` | `length` | `"max_tokens"` | `MAX_TOKENS` |
| `filtered` | (mapped) | `content_filter` | (mapped) | `SAFETY` / `RECITATION` |
| `paused` | n/a | n/a | `"incomplete"` (no tools) | n/a |

## Token Usage

```typescript
interface TokenUsage {
  inputOther: number;              // Non-cached input tokens
  output: number;                  // Completion tokens
  inputCacheRead: number;          // Tokens served from cache
  inputCacheCreation: number;      // Tokens written to cache
}
```

## Error Hierarchy

**Source**: `packages/kosong/src/errors.ts`

```
ChatProviderError (base — includes provider, model, statusCode)
├── APIConnectionError         — Network connectivity failure
├── APITimeoutError            — Request timed out
├── APIStatusError             — Non-200 HTTP response
│   └── APIContextOverflowError  — Context window exceeded
│       (detected via pattern matching on error messages)
└── APIEmptyResponseError      — No content or tool calls in response
```

## Provider Implementations

### Anthropic Provider

**Source**: `packages/kosong/src/providers/anthropic.ts`

**Authentication**: `ANTHROPIC_API_KEY` via SDK or headers.

**Key Features**:
1. **Prompt Caching**: Injects `cache_control: { type: 'ephemeral' }` on:
   - The system prompt
   - The last user message block
   - The last tool definition (if tools are provided)
   
2. **Thinking Support**:
   - Claude 4.6+: Uses `output_config.effort` (adaptive thinking, model determines budget)
   - Earlier models: Uses `budget_tokens` (fixed budget)
   - Interleaved thinking beta header: `interleaved-thinking-2025-05-14`
   - Think parts are extracted from streaming `content_block_delta` events with `type: 'thinking'`

3. **Max Tokens Resolution**: Per-version ceilings from Anthropic model cards (e.g., Opus 4.7 = 128K output tokens).

4. **Parallel Tool Calls**: Merges consecutive tool-result-only user messages into one (Anthropic requires tool results from the same turn to be in a single message).

5. **Streaming Conversion**: Maps Anthropic `MessageStreamEvent` types to kosong parts:
   - `content_block_start` → first TextPart or ThinkPart
   - `content_block_delta.text_delta` → concat to TextPart
   - `content_block_delta.thinking_delta` → concat to ThinkPart
   - `content_block_delta.input_json_delta` → ToolCall argument delta
   - `message_delta` → final usage + finish reason

**Claude Version Parsing**: Handles diverse naming variants:
```
claude-3-5-sonnet, claude-3.5.sonnet, 3-5-sonnet
→ All resolve to Claude version 3.5
```

### OpenAI Legacy Provider (Chat Completions)

**Source**: `packages/kosong/src/providers/openai-legacy.ts`

**Authentication**: `OPENAI_API_KEY` via SDK or headers.

**Key Features**:
1. **Reasoning Content**: Configurable `reasoningKey` — e.g., `"reasoning_content"` for DeepSeek models that return reasoning in a non-standard field.

2. **Reasoning Effort**: Maps to `reasoning_effort` parameter in `extra_body`.

3. **Tool Message Conversion**: `extract_text` strategy — some providers (e.g., Ollama, vLLM) require tool results as plain text rather than structured `tool` role messages. Configurable per provider.

4. **Streaming**: Uses `stream_options: { include_usage: true }` to get usage stats in the final chunk.

5. **Parallel Tool Calls**: Uses `index` field in delta to route argument tokens to the correct tool call.

### OpenAI Responses Provider

**Source**: `packages/kosong/src/providers/openai-responses.ts`

**Authentication**: `OPENAI_API_KEY` via SDK or headers.

Uses the newer OpenAI Responses API (`/v1/responses` endpoint).

**Key Differences from Legacy**:
1. **Structured Input**: Uses `input_text`, `input_image`, `input_file` types instead of the messages array format.
2. **Reasoning Items**: `reasoning` type items with `encrypted_content` and `summary` fields.
3. **Function Calls**: Inline `function_call` items in the response (no separate `tool_calls` finish reason).
4. **Developer Role**: Some models (o1, o3, gpt-4.1) use `role: 'developer'` instead of `role: 'system'`.
5. **Granular Events**: Streaming includes events like `response.output_text.delta`, `response.function_call_arguments.delta`.

### Google GenAI Provider

**Source**: `packages/kosong/src/providers/google-genai.ts`

**Authentication**: `GOOGLE_API_KEY` via SDK. For Vertex AI, uses `vertexai: true` + `googleCloudProject` + `googleCloudLocation`.

**Key Features**:
1. **Multimodal**: Full support for image, audio, video via `inlineData`/`fileData` parts.
2. **Tool Results**: Packs function responses and media parts into a single user turn (Gemini requirement).
3. **Thinking Config**:
   - Gemini 3+: `thinking_level` (MINIMAL / LOW / MEDIUM / HIGH)
   - Other models: `thinking_budget` (token count)
4. **Manual Abort**: The GenAI SDK doesn't forward AbortSignal, so the provider manually checks for abort between streaming chunks.
5. **Tool Result Ordering**: Sorts tool results to match preceding assistant's tool call order (Gemini requires ordering by function call index).

### Kimi Provider

**Source**: `packages/kosong/src/providers/kimi.ts`

**Authentication**: `KIMI_API_KEY` via SDK or headers. Default base URL: `https://api.moonshot.ai/v1`.

**Key Features**:
1. **Reasoning Content**: `reasoning_content` field (Moonshot proprietary extension).
2. **Thinking Config**: `{ type: 'enabled'/'disabled' }` in `extra_body.thinking`.
3. **Builtin Functions**: Tools starting with `$` use `type: 'builtin_function'` instead of `type: 'function'`.
4. **File Upload**: `KimiFiles` class for video upload to Moonshot's file API.
5. **Usage Extraction**: Supports both top-level `usage` and `choices[0].usage` (Moonshot proprietary).
6. **Max Tokens**: Converts legacy `max_tokens` to `max_completion_tokens` automatically.

## Provider Factory

**Source**: `packages/kosong/src/providers/index.ts`

```typescript
type ProviderConfig =
  | { type: 'anthropic' } & AnthropicOptions
  | { type: 'openai' } & OpenAILegacyOptions
  | { type: 'kimi' } & KimiOptions
  | { type: 'google-genai' } & GoogleGenAIOptions
  | { type: 'openai_responses' } & OpenAIResponsesOptions
  | { type: 'vertexai' } & GoogleGenAIOptions;

function createProvider(config: ProviderConfig): ChatProvider;
```

## Capability Registry (`capability-registry.ts`)

Maps known model name patterns to capabilities:

| Regex Pattern | image_in | video_in | audio_in | thinking | tool_use |
|-------------|----------|----------|----------|----------|----------|
| `o\d` (OpenAI reasoning) | — | — | — | ✓ | ✓ |
| `gpt-4o` | ✓ | — | — | — | ✓ |
| `gpt-4-turbo` | ✓ | — | — | — | ✓ |
| `gpt-4.1` | ✓ | — | — | — | ✓ |
| `gpt-4.5` | ✓ | — | — | — | ✓ |
| `gpt-3.5-turbo` | — | — | — | — | ✓ |
| `claude-3` (except opus) | ✓ | — | — | — | ✓ |
| `claude-4` | ✓ | — | — | ✓ | ✓ |
| `gemini-1.5` | ✓ | ✓ | ✓ | — | ✓ |
| `gemini-2.0` | ✓ | ✓ | ✓ | — | ✓ |
| `gemini-2.5` | ✓ | ✓ | ✓ | ✓ | ✓ |

## Request Auth

**Source**: `packages/kosong/src/providers/request-auth.ts`

Authorization resolution order:
1. `clientFactory` from options (receives per-request auth) → creates SDK client with auth
2. Cached client (if no per-request auth) → reuse existing client
3. Build fresh client with `requireProviderApiKey()` → errors if no key configured

This keeps short-lived OAuth tokens out of shared client state and allows per-request auth override.

```typescript
interface ProviderRequestAuthResolver {
  (modelName: string): ProviderRequestAuth | null;
}
```

## Re-implementation Notes

1. **Provider interface is the key abstraction**: Implement `ChatProvider` once per LLM backend. Each provider is an independent HTTP client — they share no state.

2. **Streaming is the hardest part**: Each provider has a different streaming format. The kosong abstraction normalizes them to async iterables. Your streaming implementation must:
   - Handle progressive content merging (text + thinking + tool calls arriving in any order)
   - Support parallel tool call detection (tool A and tool B being called simultaneously)
   - Preserve thinking content through streaming
   - Report finish reason and usage after streaming completes

3. **Error normalization**: Map each provider's HTTP errors to the kosong hierarchy. Context overflow detection is particularly important — it triggers compaction in the agent.

4. **Thinking/reasoning is provider-specific but concept is universal**: The `thinking` content type represents model reasoning. Anthropic calls it "thinking blocks", OpenAI calls it "reasoning content", Google calls it "thinking". Normalize to `{ type: 'think', think: string }`.

5. **Start with OpenAI Chat Completions**: It's the most widely supported protocol. Many LLM backends (vLLM, Ollama, TGI, etc.) are OpenAI-compatible.

6. **Capability registry is optional**: The `UNKNOWN_CAPABILITY` fallback means you can skip the registry and assume basic capabilities work.

7. **Prompt caching is optimization**: Only the Anthropic provider implements cache control. Skip this for initial port — it affects cost but not correctness.
