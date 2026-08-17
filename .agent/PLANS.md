# Execution Plans for Codex

Use this file as the planning contract for changes that are multi-step, architectural, risky, or expected to touch several components.

Small localized tasks do not need ceremony.

## Before large implementation

1. Read `AGENTS.md`.
2. Read the relevant docs in `docs/`.
3. Inspect the current repository.
4. State the current milestone from `docs/ROADMAP.md`.
5. Identify assumptions and unknown hardware facts.
6. Create a concise execution plan with verifiable checkpoints.

## Plan format

Use:

```text
Goal
Scope
Non-goals
Current repository observations
Assumptions
Steps
Validation commands
Risks / unresolved items
```

## Planning rules

- Plans are living documents: update them if implementation reality differs.
- Do not expand scope to future roadmap items.
- Do not create abstractions solely for hypothetical future features.
- Do not invent hardware facts to complete a plan.
- Prefer a small working vertical slice over broad scaffolding.
- Every step should have an observable validation.
- A build passing is not proof of hardware correctness.
- Simulation results must be described as simulation results.

## Completion report

At the end of a planned task, report:

- files changed;
- design decisions made;
- commands/tests run;
- result of each validation;
- known limitations;
- hardware behavior still unverified;
- any proposed update to `docs/DECISIONS.md`.
