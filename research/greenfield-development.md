# Greenfield Development Procedure

A step-by-step process for building new software from scratch using Claude Code. Complete each step before moving to the next — earlier decisions constrain later ones. Each step produces concrete artifacts that feed into subsequent steps.

## Steps

1. Problem Framing
2. Functional Specification
3. System Architecture
4. Schema & Contracts
5. Non-Functional Requirements
6. Repository & Code Structure
7. Test Strategy
8. Developer Workflow
9. Incremental Build Plan

---

## 1. Problem Framing

Lock down _why_ before jumping into system design.

### Artifacts

- Problem statement (1–2 paragraphs)
- Target users/personas
- Core use cases (top 5 only)
- Success metrics (what does "good" look like?)

This prevents overbuilding and keeps focus on the right outcomes.

## 2. Functional Specification

Turn the problem framing into a structured, testable spec.

### Artifacts

- Feature list with priorities (MVP vs. later phases)
- User flows (step-by-step interactions)
- Edge cases and failure scenarios
- Acceptance criteria per feature (Given/When/Then)

**Tip:** Rewrite vague requirements into testable acceptance criteria before moving on. If you can't test it, you can't build it.

## 3. System Architecture

Define the shape of the system _and_ lock in key technology choices at the same time. Separating "what the architecture looks like" from "what tech we chose" leads to drift — decide together.

### Artifacts

- High-level architecture diagram (ASCII is fine)
- Component/service boundaries
- Data flow between components
- External dependencies (APIs, hardware, auth providers, etc.)
- **Tech decision records** for each major choice:
  - What was chosen and what alternatives were considered
  - Tradeoffs (performance vs. simplicity, etc.)
  - Known risks

### Key decisions to lock early

- Language(s) and runtime
- Communication style (REST, WebSocket, message queues, IPC, etc.)
- Hosting/deployment model
- State management approach
- Framework choices (UI, backend, real-time)

**Tip:** Write decision records inline in the architecture spec, not as a separate document. They stay relevant longer when co-located with the design they justify.

## 4. Schema & Contracts

Highest ROI step — and often skipped. A strong schema produces far better generated code.

### Artifacts

- Core entities (ER diagram or structured list)
- API/message contracts (request/response schemas, command/telemetry formats)
- Validation rules and constraints
- Versioning strategy (if applicable)

**Tip:** Define contracts before writing implementation code. Claude generates much more consistent code when it has explicit schemas to follow.

## 5. Non-Functional Requirements (NFRs)

Where most greenfield projects fail if ignored. Define the quality attributes the system must meet.

### Artifacts

- Performance expectations (latency, throughput, timing guarantees)
- Scalability assumptions (or explicit non-goals)
- Security model (auth, roles, data protection)
- Reliability (failure handling, safe states, recovery)
- Observability (logs, metrics, tracing)

**Tip:** NFRs often surface constraints that change architecture decisions. If they do, go back and update step 3 — don't just bolt them on.

## 6. Repository & Code Structure

Define the skeleton before generating code. This prevents inconsistent structure across modules.

### Artifacts

- Folder structure with clear module boundaries
- Naming conventions (files, functions, types)
- Coding standards and patterns
- `CLAUDE.md` project context (build commands, architecture summary, conventions)

**Tip:** Generate the scaffold first, then populate it. Update `CLAUDE.md` with build/run commands as they become real — this is the primary way Claude Code understands your project across sessions.

## 7. Test Strategy

Don't bolt testing on later. Define it before writing implementation code.

### Artifacts

- Unit test strategy (what gets unit tested, what doesn't)
- Integration test plan (component boundaries to verify)
- End-to-end scenarios (derived from user flows in step 2)
- Test data strategy (fixtures, factories, simulation)

**Tip:** Consider TDD — have Claude generate tests from acceptance criteria _before_ implementation. The acceptance criteria from step 2 map directly to test cases.

## 8. Developer Workflow

Often overlooked but critical for maintaining velocity and quality.

### Artifacts

- Branching strategy (trunk-based, Git flow, etc.)
- CI/CD pipeline outline (what runs on commit, on PR, on merge)
- Environment strategy (dev/staging/prod, or single-environment for tools)
- Code review guidelines
- Claude Code configuration (coding rules, patterns, constraints, definition of done)

**Tip:** The Claude Code configuration (`CLAUDE.md` rules, hooks, prompt templates) lives here. Examples: "Generate code that strictly follows this API contract", "Do not introduce new dependencies unless specified."

## 9. Incremental Build Plan

Break the system into buildable vertical slices. Each slice should be demoable and testable independently.

### Artifacts

- Milestones (thin vertical features, not horizontal layers)
- Task decomposition per milestone
- Dependency order (what must be built first)
- Definition of done per milestone

**Tip:** Build the thinnest possible end-to-end slice first (e.g., "one injection protocol executes through all 7 layers with hardcoded values"). This validates the architecture before you invest in flexibility.

---

## Principles

- **Complete each step before coding.** The cost of rework from skipped planning is always higher than the cost of the planning itself.
- **Earlier steps constrain later ones.** If a later step reveals a problem with an earlier decision, go back and update — don't paper over it.
- **Artifacts are living documents.** Update them as you learn. Stale specs are worse than no specs.
- **Vertical slices over horizontal layers.** Build features end-to-end, not "all the database first, then all the API, then all the UI."
