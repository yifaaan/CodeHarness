# Plan: Implement Agent Core Engine

## Status
- **Phase**: Agent Core
- **Priority**: High
- **Estimated Effort**: 3 weeks
- **Owner**: Core Team

## Objective

Implement the Agent class that orchestrates all subsystems (config, context, tools, permissions, hooks) and provides the main entry point for agent operations.

## Scope

### In Scope
- Implement Agent class with all subsystems
- Implement Agent construction and initialization
- Implement Agent RPC methods
- Add agent profiles and types
- Add unit tests

### Out of Scope
- Multi-agent orchestration (future phase)
- Agent spawning (future phase)
- Agent performance optimization (future phase)

## Implementation Steps

### Week 1: Agent Class and Subsystems

1. **Define Agent Types**
   - Create `src/codeharness/engine/agent_types.h`
   - Define `AgentConfig`, `AgentStatus`, `AgentEvent` types
   - Define `AgentProfile` for agent types (agent, coder, explore, plan)
   - Define `AgentAPI` interface

2. **Implement Agent Class**
   - Create `src/codeharness/engine/agent.h` and `agent.cpp`
   - Implement agent constructor with dependency injection
   - Implement subsystem initialization in correct order
   - Implement agent lifecycle (start, stop, cleanup)

3. **Create Unit Tests**
   - Create `tests/engine/agent_tests.cpp`
   - Test agent construction
   - Test subsystem initialization
   - Test agent lifecycle

### Week 2: Agent RPC and Event System

4. **Implement Agent RPC Methods**
   - Implement `prompt` method for user input
   - Implement `steer` method for mid-turn input
   - Implement `cancel` method for aborting turns
   - Implement `setModel` and `setPermissionMode` methods

5. **Implement Event System**
   - Implement event emission for all agent actions
   - Implement event recording for event sourcing
   - Implement event dispatching to subscribers

6. **Integration Testing**
   - Create integration tests with mock subsystems
   - Test complete agent flow
   - Test error handling and recovery
   - Test event emission and recording

### Week 3: Agent Profiles and Documentation

7. **Implement Agent Profiles**
   - Define profile for main agent (all tools)
   - Define profile for coder subagent (code tools)
   - Define profile for explore subagent (read-only tools)
   - Define profile for plan subagent (planning tools)

8. **Documentation**
   - Update architecture docs to include Agent
   - Add API reference for Agent class
   - Add agent profile guide
   - Add agent extension guide

## Dependencies

- **Loop**: Required for turn execution
- **Config**: Required for agent configuration
- **Context**: Required for conversation memory
- **Tools**: Required for tool execution
- **Permission**: Required for access control
- **Hooks**: Required for extension points

## Success Criteria

- [ ] Agent class implemented with all subsystems
- [ ] Agent RPC methods working correctly
- [ ] Event system implemented and tested
- [ ] Agent profiles defined and documented
- [ ] All unit and integration tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Construction order complexity | High | Document dependencies clearly, add construction order tests |
| Subsystem coupling | Medium | Use dependency injection, keep interfaces minimal |
| Event system performance | Medium | Batch events, async emission |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Dependency Injection**: All dependencies passed through constructor
2. **Construction Order**: Subsystems initialized in dependency order
3. **Event Sourcing**: All state changes recorded as events
4. **RPC Isolation**: Agent RPC methods are the only public interface

## References

- [Kimi Code Agent Core](../../kimi-code-analysis/core-components.md#1-agent-class)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [OpenAI: Agent-First Engineering](https://openai.com/index/codex-in-an-agent-first-world/)
