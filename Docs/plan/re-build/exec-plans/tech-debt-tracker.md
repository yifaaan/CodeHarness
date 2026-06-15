# Technical Debt Tracker

This document tracks technical debt in the CodeHarness project, following OpenAI's principle of "entropy collection" - regular cleanup of outdated patterns.

## Debt Categories

### 1. Architecture Debt
- **Description**: Architectural decisions that need revision
- **Impact**: High - affects maintainability and extensibility
- **Examples**: Missing abstraction layers, tight coupling

### 2. Code Quality Debt
- **Description**: Code that doesn't meet quality standards
- **Impact**: Medium - affects readability and maintainability
- **Examples**: Missing tests, code duplication, poor naming

### 3. Documentation Debt
- **Description**: Outdated or missing documentation
- **Impact**: Medium - affects onboarding and agent readability
- **Examples**: Missing API docs, outdated architecture docs

### 4. Performance Debt
- **Description**: Performance issues that need optimization
- **Impact**: Low-Medium - affects user experience
- **Examples**: Memory leaks, inefficient algorithms

### 5. Security Debt
- **Description**: Security vulnerabilities that need addressing
- **Impact**: High - affects system security
- **Examples**: Missing input validation, exposed secrets

## Current Debt Items

### High Priority (Must Fix)

| ID | Category | Description | Impact | Status | Due Date |
|----|----------|-------------|--------|--------|----------|
| TD-001 | Architecture | Missing Kaos abstraction layer | High | Resolved (MVP) | 2026-07-01 |
| TD-002 | Architecture | No ToolScheduler for concurrent execution | High | Resolved (MVP) | 2026-07-15 |
| TD-003 | Security | Incomplete permission checking | High | Resolved (MVP) | 2026-07-01 |

### Medium Priority (Should Fix)

| ID | Category | Description | Impact | Status | Due Date |
|----|----------|-------------|--------|--------|----------|
| TD-004 | Code Quality | Missing unit tests for tools | Medium | In Progress | 2026-07-15 |
| TD-005 | Documentation | Outdated architecture docs | Medium | Pending | 2026-07-30 |
| TD-006 | Performance | Inefficient context loading | Medium | Resolved (MVP) | 2026-08-15 |

### Low Priority (Nice to Have)

| ID | Category | Description | Impact | Status | Due Date |
|----|----------|-------------|--------|--------|----------|
| TD-007 | Code Quality | Code duplication in providers | Low | Pending | 2026-08-30 |
| TD-008 | Documentation | Missing API examples | Low | Pending | 2026-09-15 |
| TD-009 | Performance | Unoptimized string operations | Low | Pending | 2026-09-30 |

### Resolution Notes

- **TD-002 (Resolved 2026-06-15, MVP):** The Engine now has a `ToolScheduler` that batch-executes explicitly concurrency-safe tools while preserving deterministic event and history ordering. `ToolExecution::canRunConcurrently` is the opt-in marker; `ReadFile`, `Glob`, and `Grep` enable it by default, and mutating or stateful tools remain serial barriers. `TurnInput.toolScheduler.maxConcurrentTools` defaults to 4 and `<= 1` forces serial execution for compatibility/testing. Deferred: user-facing config for scheduler policy and broader per-resource conflict analysis for safe write/tool overlap.

- **TD-001 (Resolved 2026-06-15, MVP):** The original Kaos abstraction plan has landed under the C++ `Host` naming: `Host`, `LocalHost`, `HostPath`, and `CurrentHost` now provide filesystem, path, and process abstraction. Tools, Session, Config, CLI, Hooks, Skills, and MCP all perform world I/O through `Host*`. The deferred portion is remote/SSH execution, not the local abstraction layer.

- **TD-003 (Resolved 2026-06-15, MVP):** The Permission module landed. `PermissionGate` is now consulted between `ResolveExecution` and `Execute` in `Loop.cpp` — `ToolExecution::requiresPermission` is live data instead of dead. Manual mode prompts via `ApprovalCallback`; Yolo allows all; Auto falls back to Manual. The safety hole (ungated Write/Edit/Bash) is closed. **Still deferred** to a future iteration of plan #11: the permission rules DSL (`PermissionRule`/`Policy`), true session-scoped Auto mode, audit logging of decisions, and the full HookEngine. TD-003's *must-fix* core is done; the *nice-to-have* policy layer remains tracked under exec plan #11.

- **TD-006 (Resolved 2026-06-15, MVP):** The Context module landed. `Agent::history` is now a `ContextMemory` with a cached token estimate; before each turn, if history + the incoming prompt cross 75% of `GetCapability(model).maxContextTokens`, the Agent summarizes the prefix via a second `Generate` call and keeps the last 10 messages verbatim. `Loop.cpp`/`LoopTypes.h`/`LoopEvent`/providers are unchanged — the loop keeps receiving a plain (shorter) `std::vector<llm::Message>`. **Still deferred** to a future iteration of plan #06: the `InjectionManager` (plan/permission-mode injection), mid-turn compaction (between tool-call steps inside one turn, needs a wider `LoopHooks::beforeStep`), a real `CountTokens` provider virtual, and `ContextMessage` metadata. The *must-fix* core (unbounded history → context overflow) is done.

## Debt Reduction Strategy

### Weekly Cleanup (20% Time)
- **Friday**: Dedicated to technical debt reduction
- **Focus**: High priority items first
- **Output**: PRs that reduce debt

### Automated Detection
- **Linters**: Custom linters for architecture invariants
- **Tests**: Structural tests for dependency direction
- **CI**: Automated debt detection in CI pipeline

### Documentation Freshness
- **Monthly**: Review and update documentation
- **Quarterly**: Archive outdated documentation
- **Continuous**: Agent-driven documentation updates

## Metrics

### Debt Ratio
- **Target**: < 10% of codebase
- **Current**: TBD
- **Measurement**: Lines of code with debt / Total lines of code

### Debt Velocity
- **Target**: Reduce by 5% per month
- **Current**: TBD
- **Measurement**: Debt items closed / Debt items created

### Documentation Freshness
- **Target**: < 30 days average age
- **Current**: TBD
- **Measurement**: Average age of documentation files

## Process

### When to Add Debt
1. **Emergency fixes**: Add debt item with high priority
2. **Feature development**: Add debt item if shortcuts are taken
3. **Code review**: Identify debt during review

### When to Remove Debt
1. **Weekly cleanup**: Address high priority items
2. **Feature development**: Remove related debt when touching code
3. **Refactoring**: Remove debt during major refactors

### Tracking
- **Tools**: Git issues, project board
- **Reporting**: Monthly debt report
- **Review**: Weekly debt review meeting

## References

- [OpenAI: Codex in an Agent-First World](https://openai.com/index/codex-in-an-agent-first-world/)
- [Technical Debt Quadrant](https://martinfowler.com/bliki/TechnicalDebtQuadrant.html)
- [Refactoring by Martin Fowler](https://martinfowler.com/books/refactoring.html)
