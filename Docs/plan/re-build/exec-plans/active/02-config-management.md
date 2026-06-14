# Plan: Implement Config and Provider Management

## Status
- **Phase**: Foundation
- **Priority**: High
- **Estimated Effort**: 2 weeks
- **Owner**: Core Team

## Objective

Implement the configuration system and provider management to connect LLM providers to the application config, enabling provider authentication and model selection.

## Scope

### In Scope
- Define Config schema (TOML format)
- Implement ConfigManager for loading/saving config
- Implement ProviderManager for provider resolution
- Add OAuth integration for provider authentication
- Add unit tests

### Out of Scope
- UI for config editing (future phase)
- Remote config sync (future phase)
- Config migration from other formats (future phase)

## Implementation Steps

### Week 1: Config Schema and Manager

1. **Define Config Schema**
   - Create `src/codeharness/config/config.h`
   - Define `KimiConfig` struct with all configuration fields
   - Define provider configuration structure
   - Define permission rules structure

2. **Implement ConfigManager**
   - Create `src/codeharness/config/config_manager.h` and `config_manager.cpp`
   - Implement TOML parsing using `toml++` or `yaml-cpp`
   - Implement config validation
   - Implement config save/load

3. **Create Unit Tests**
   - Create `tests/config/config_manager_tests.cpp`
   - Test config loading from file
   - Test config validation
   - Test config save/load cycle

### Week 2: Provider Management and OAuth

4. **Implement ProviderManager**
   - Create `src/codeharness/config/provider_manager.h` and `provider_manager.cpp`
   - Implement provider resolution from config
   - Implement provider credential management
   - Implement provider health check

5. **Implement OAuth Integration**
   - Create `src/codeharness/config/oauth.h` and `oauth.cpp`
   - Implement OAuth device code flow
   - Implement token storage and refresh
   - Implement OAuth error handling

6. **Integration Testing**
   - Create integration tests for provider resolution
   - Test OAuth flow with mock servers
   - Test credential storage and retrieval

7. **Documentation**
   - Update architecture docs to include Config
   - Add API reference for ConfigManager
   - Add configuration file format documentation

## Dependencies

- **Kaos**: Required for file I/O operations
- **External**: `toml++` or `yaml-cpp` for config parsing

## Success Criteria

- [ ] Config schema defined with all required fields
- [ ] ConfigManager implemented and passing all unit tests
- [ ] ProviderManager implemented with provider resolution
- [ ] OAuth integration working with mock servers
- [ ] All provider types supported (OpenAI, Anthropic, Google, Kimi)
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| TOML parsing complexity | Medium | Use well-tested library, start with minimal schema |
| OAuth flow errors | High | Implement comprehensive error handling, add retry logic |
| Config validation too strict | Medium | Start with warnings, upgrade to errors later |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Config as Single Source**: All configuration flows through ConfigManager
2. **Provider Abstraction**: Config does not depend on specific provider implementations
3. **Secure Storage**: Credentials stored securely, never in plain text
4. **Validation**: All config validated at load time

## References

- [Kimi Code Config Schema](../../kimi-code-analysis/core-components.md#9-configuration-system)
- [TOML Specification](https://toml.io/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
