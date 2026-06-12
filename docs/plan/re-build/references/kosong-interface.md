# Kosong Interface

LLM provider abstraction layer.

## One-Liner

Kosong provides a unified interface for interacting with LLM providers (Anthropic, OpenAI, Google GenAI, Kimi/Moonshot), abstracting away API differences.

## Core Interface

```typescript
interface ChatProvider {
  readonly name: string;
  readonly modelName: string;
  readonly thinkingEffort: ThinkingEffort | null;

  generate(
    systemPrompt: string,
    tools: Tool[],
    history: Message[],
    options?: GenerateOptions,
  ): Promise<StreamedMessage>;

  withThinking(effort: ThinkingEffort): ChatProvider;
}

interface StreamedMessage {
  [Symbol.asyncIterator](): AsyncIterator<StreamedMessagePart>;
  id: string;
  usage: TokenUsage | null;
  finishReason: FinishReason | null;
}
```

## Key Concepts

1. **Streaming Normalization**: All providers return `StreamedMessage` with async iterator. Progressive content merging handled internally.

2. **Thinking Normalization**: Maps provider-specific reasoning to unified `ThinkPart`:
   - Anthropic: `thinking` blocks
   - OpenAI: `reasoning_content` field
   - Kimi: `reasoning_content` field

3. **Finish Reason Mapping**: Normalizes provider stop reasons to unified enum:
   - `completed`, `tool_calls`, `truncated`, `filtered`, `paused`

## Supported Providers

| Provider | SDK | Auth |
|----------|-----|------|
| Anthropic | `@anthropic-ai/sdk` | API key |
| OpenAI (Legacy) | `openai` | API key |
| OpenAI (Responses) | `openai` | API key |
| Google GenAI | `@google/genai` | API key |
| Kimi | `openai` SDK | API key |

## Capability System

```typescript
interface ModelCapability {
  image_in: boolean;
  video_in: boolean;
  audio_in: boolean;
  thinking: boolean;
  tool_use: boolean;
  max_context_tokens: number;
}
```

Capability registry maps model name patterns to capabilities. `UNKNOWN_CAPABILITY` fallback for uncatalogued models.

## Thinking Effort Mapping

| Kosong | Anthropic | OpenAI | Google |
|--------|-----------|--------|--------|
| `off` | disabled | no reasoning | disabled |
| `low` | `low` | `reasoning_effort: "low"` | `LOW` |
| `medium` | `medium` | `reasoning_effort: "medium"` | `MEDIUM` |
| `high` | `high` | `reasoning_effort: "high"` | `HIGH` |

## Error Hierarchy

```
ChatProviderError
├── APIConnectionError
├── APITimeoutError
├── APIStatusError
│   └── APIContextOverflowError
└── APIEmptyResponseError
```

## See Also

- [config-schema.md](config-schema.md) — Provider configuration
- [loop-engine.md](loop-engine.md) — Uses Kosong for LLM calls
