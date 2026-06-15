# Plan: Implement Skill System

## Status
- **Phase**: Extensions
- **Priority**: Low
- **Estimated Effort**: 1 week
- **Owner**: Core Team
- **State**: Completed (2026-06-15) — code review pending

## Objective

Implement the skill system for reusable markdown documents and slash commands.

## Scope

### In Scope
- Implement Skill definition and parsing
- Implement Skill discovery and registration
- Implement Skill activation
- Add slash command integration
- Add unit tests

### Out of Scope
- Skill marketplace (future phase)
- Skill versioning (future phase)
- Skill composition (future phase)

## Implementation Steps

### Week 1: Skill System

1. **Define Skill Types**
   - Create `src/codeharness/skills/skill_types.h`
   - Define `Skill`, `SkillDefinition` types
   - Define `SkillType` (slash, tool, agent)
   - Define `SkillConfig` for skill settings

2. **Implement SkillParser**
   - Create `src/codeharness/skills/skill_parser.h` and `skill_parser.cpp`
   - Implement markdown parsing with frontmatter
   - Implement parameter extraction
   - Implement template rendering

3. **Implement SkillRegistry**
   - Create `src/codeharness/skills/skill_registry.h` and `skill_registry.cpp`
   - Implement skill discovery from directories
   - Implement skill registration
   - Implement skill lookup by name

4. **Implement SkillManager**
   - Create `src/codeharness/skills/skill_manager.h` and `skill_manager.cpp`
   - Implement skill activation
   - Implement slash command handling
   - Implement skill context injection

5. **Create Unit Tests**
   - Create `tests/skills/skill_tests.cpp`
   - Test skill parsing
   - Test skill discovery
   - Test skill activation

6. **Documentation**
   - Update architecture docs to include Skills
   - Add API reference for SkillSystem
   - Add skill development guide

## Dependencies

- **Agent**: Required for skill integration
- **Kaos**: Required for file discovery

## Success Criteria

- [x] Skill definition and parsing implemented
- [x] Skill discovery and registration working
- [x] Skill activation working
- [x] Slash command integration working (`--skill name[:args]`; interactive `/skill-name` deferred to plan #14)
- [x] All unit tests passing
- [x] Documentation updated
- [ ] Code review completed
- [x] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Parsing errors | Medium | Comprehensive error handling, validation |
| Skill conflicts | Low | Clear naming conventions, precedence rules |
| Performance | Low | Lazy loading, caching |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |
| 2026-06-12 | Implemented | SkillParser/SkillRegistry/SkillScanner/SkillManager/SkillTool landed in `cc225ed`; wired into Agent + Cli + ToolManager |
| 2026-06-15 | Completed | Closed remaining gaps: `RenderSkillIndex` advertises invocable skills in the system prompt (consumes `whenToUse`/`disableModelInvocation`); `SkillType` prompt↔inline routing (prompt → system content, inline → user message); `[skills]` config (`allow_project_skills` + `extra_skill_dirs`); per-turn system-prompt rebuild with prompt-skill staging. Fixed pre-existing `SkillScanner::Scan` signature mismatch (header `std::span` vs cpp `std::vector`). Tests: RenderSkillIndex / prompt-inline routing / `[skills]` parsing. Docs: AGENTS.md module table + section, learning-guide chapter 12, customization guide reconciled. 352/352 tests green. Code review pending. |

## Architecture Invariants

This plan enforces the following invariants:

1. **Skill Isolation**: Skills do not share state
2. **Deterministic Activation**: Same inputs produce same behavior
3. **Error Resilience**: Skill failures do not crash the harness
4. **Lazy Loading**: Skills loaded on demand

## References

- [Kimi Code Skill System](../../kimi-code-analysis/core-components.md#12-skill-system)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
