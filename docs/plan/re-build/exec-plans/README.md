# Execution Plans

This directory contains execution plans for the re-implementation of Kimi Code in C++20.

## Directory Structure

```
exec-plans/
├── active/           # Current work in progress
├── completed/        # Finished plans
└── tech-debt-tracker.md  # Technical debt tracking
```

## Plan Naming Convention

Plans are named with a prefix indicating their status:
- `active/`: Plans currently being executed
- `completed/`: Plans that have been finished

## Plan Template

Each plan should follow this template:

```markdown
# Plan: [Title]

## Status
- **Phase**: [Foundation | Agent Core | Services | Extensions | Application]
- **Priority**: [High | Medium | Low]
- **Estimated Effort**: [Days/Weeks]
- **Owner**: [Team/Individual]

## Objective
[Clear description of what this plan aims to achieve]

## Scope
- **In Scope**: [What is included]
- **Out of Scope**: [What is excluded]

## Implementation Steps
1. [Step 1]
2. [Step 2]
3. [Step 3]

## Dependencies
- [Dependency 1]
- [Dependency 2]

## Success Criteria
- [Criterion 1]
- [Criterion 2]

## Risks and Mitigations
- **Risk**: [Description] | **Mitigation**: [Action]

## Progress Log
| Date | Status | Notes |
|------|--------|-------|
| YYYY-MM-DD | Started | ... |
```

## Current Active Plans

See [active/](active/) directory for plans currently being executed.

## Completed Plans

See [completed/](completed/) directory for plans that have been finished.

## Technical Debt

See [tech-debt-tracker.md](tech-debt-tracker.md) for tracking technical debt.
