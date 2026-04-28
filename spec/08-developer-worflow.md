# Developer Workflow

This document defines how code moves from idea to merged commit: branching, CI/CD, environments, code review, and Claude Code configuration. It applies to both human contributors and AI-assisted development.

---

## 1. Branching Strategy

**Trunk-based development** with short-lived feature branches. No Git Flow, no release branches, no long-running parallel tracks. The codebase is a single desktop application — there are no production deployments to manage or hotfixes to cherry-pick.

### Branch Naming

```
main                          # Always buildable. Protected.
feature/<short-description>   # New functionality
fix/<short-description>       # Bug fixes
refactor/<short-description>  # Structural changes, no behavior change
test/<short-description>      # Test additions or improvements
docs/<short-description>      # Documentation only
```

Examples:
```
feature/pid-controller
feature/grpc-telemetry-stream
fix/volume-tracking-drift
refactor/hal-thread-safety
test/safety-monitor-integration
docs/architecture-update
```

### Rules

- **`main` is always buildable.** Every commit on `main` passes all tests and compiles without warnings.
- **Feature branches are short-lived.** Target 1-3 days. If a feature takes longer, break it into smaller deliverables.
- **Branch from `main`, merge to `main`.** No branch-to-branch merges.
- **Rebase before merge.** Keep linear history on `main` when practical. Squash-merge is acceptable for multi-commit feature branches.
- **Delete branches after merge.** No stale branches.

---

## 2. CI/CD Pipeline

CI runs on every push to a feature branch and on every merge to `main`. There is no CD — this is a desktop application, not a deployed service.

### Pipeline Stages

```
┌─────────┐    ┌─────────┐    ┌────────────┐    ┌───────────┐
│  Build   │───▶│  Lint   │───▶│ Unit Tests │───▶│Integration│
│          │    │         │    │            │    │   Tests   │
└─────────┘    └─────────┘    └────────────┘    └───────────┘
```

### Stage 1: Build

| Step | Command | Timeout | Fail Condition |
|------|---------|---------|----------------|
| Configure | `cmake -B build -DCMAKE_BUILD_TYPE=Debug` | 60s | CMake error |
| Build backend | `cmake --build build --target injector-backend` | 300s | Compile error or warning (`-Werror`) |
| Build frontend | `cmake --build build --target injector-frontend` | 300s | Compile error or warning |
| Build tests | `cmake --build build --target injector-unit-tests injector-integration-tests` | 120s | Compile error |

**Matrix:** Run on Windows (MSVC) and Linux (GCC). macOS is best-effort — not required to pass.

### Stage 2: Lint

| Step | Command | Fail Condition |
|------|---------|----------------|
| clang-tidy | `clang-tidy -p build backend/src/**/*.cpp` | Any warning (warnings-as-errors) |
| clang-format check | `clang-format --dry-run --Werror backend/src/**/*.{h,cpp} frontend/src/**/*.{h,cpp}` | Any formatting diff |

### Stage 3: Unit Tests

| Step | Command | Timeout | Fail Condition |
|------|---------|---------|----------------|
| Run unit tests | `ctest --test-dir build -R unit-tests --output-on-failure` | 30s | Any test failure |
| Sanitizers | Same binary, built with `-fsanitize=address,undefined` | 60s | Any sanitizer finding |
| Thread sanitizer | Separate build with `-fsanitize=thread` | 60s | Any data race detected |

**Note:** ASan + TSan cannot run in the same binary. Two separate debug builds are needed.

### Stage 4: Integration Tests

| Step | Command | Timeout | Fail Condition |
|------|---------|---------|----------------|
| Run integration tests | `ctest --test-dir build -R integration-tests --output-on-failure` | 120s | Any test failure |
| TSan on integration | Separate TSan build | 180s | Any data race |

### Pipeline Expectations

| Metric | Target |
|--------|--------|
| Total pipeline time | < 10 minutes |
| Flakiness tolerance | 0 — flaky tests are bugs, not noise |
| Required to merge | All stages green on at least one platform |

---

## 3. Environment Strategy

Single environment. No dev/staging/prod distinction — this is a local desktop tool.

### Development Machine

| Component | Configuration |
|-----------|--------------|
| OS | Windows 11 (primary), Linux optional for RT testing |
| CPU | AMD Ryzen 7 7735HS (8 cores) — core 3 reserved for control loop during development |
| Build | CMake + vcpkg for dependencies |
| Run | Backend and frontend as separate processes on localhost |
| Debug | Attach debugger to either process independently |

### Test Environment

Same as development. Tests run on the same machine. Integration tests use `localhost:0` (ephemeral port) to avoid conflicts with a running backend.

### Linux RT Testing (Optional)

For verifying real-time timing claims:
```bash
# Boot with isolated core
# Kernel param: isolcpus=3 nohz_full=3 rcu_nocbs=3

# Run backend with RT priority
sudo chrt -f 80 taskset -c 3 ./injector-backend --config config.json

# Observe jitter
# Export tick data, analyze timing histogram
```

Not required for CI — this is a manual verification step for NFR 1.1.

---

## 4. Code Review Guidelines

### What to Review

| Aspect | Look For |
|--------|----------|
| **Correctness** | Does the code do what the spec says? Are edge cases handled? |
| **Thread safety** | Shared state accessed under correct lock? Atomics used correctly? No data races? |
| **Real-time path** | No allocations, no exceptions, no unbounded operations in control loop or safety monitor? |
| **Safety** | Safety monitor changes are minimal and obviously correct. Complexity = risk. |
| **Contract adherence** | Do message formats match the proto definition? Do internal structs match the contracts in spec 04? |
| **Test coverage** | New code has tests. New behavior has acceptance criteria coverage. |
| **Naming** | Follows conventions in spec 06. |

### Review Checklist (PR Template)

```markdown
## Changes
- [ ] What changed and why (1-3 sentences)

## Testing
- [ ] Unit tests added/updated
- [ ] Integration tests added/updated (if boundary changes)
- [ ] Manual testing done (if UI changes)

## Safety Impact
- [ ] No changes to safety monitor logic
- [ ] OR: safety changes reviewed with extra scrutiny

## Thread Safety
- [ ] No new shared state introduced
- [ ] OR: new shared state has documented synchronization

## Spec Compliance
- [ ] Changes align with spec (cite section if non-obvious)
- [ ] OR: spec updated to reflect design changes
```

### Review Speed

- Small PRs (< 200 lines): review within 1 day
- Large PRs: break them up. If a PR is > 500 lines changed, it probably should have been 2+ PRs.
- Safety monitor changes: always get a second look, even if trivial.

---

## 5. Claude Code Configuration

### CLAUDE.md

The project-level `CLAUDE.md` (see **06-repo-structure.md, Section 4** for template) provides Claude Code with build commands, architecture context, and conventions. It is the primary mechanism for maintaining consistency across sessions.

### Coding Rules for Claude Code

All coding standards, naming conventions, and patterns are defined in **06-repo-structure.md, Sections 2–3**. The CLAUDE.md template in **06-repo-structure.md, Section 4** contains the condensed version that Claude Code reads at session start.

**Additional workflow guidance for Claude Code sessions:**
- Reference specific spec sections when asking for implementation — Claude generates more consistent code when it has explicit contracts.
- Ask Claude to write tests first (from acceptance criteria in spec 02), then implementation.
- After implementation, ask Claude to run the tests and fix any failures.
- Review generated code for thread safety — Claude may not always get lock ordering or atomic usage right without explicit prompting.
- Update `CLAUDE.md` after significant changes so the next session has current context.

### Definition of Done

A task is done when:

1. **Code compiles** without warnings on both MSVC and GCC (`-Wall -Wextra -Werror`)
2. **Unit tests pass** for all modified and new components
3. **Integration tests pass** if component boundaries were changed
4. **Sanitizers clean** — zero findings from ASan, TSan, UBSan
5. **Code formatted** — `clang-format` produces no diff
6. **Spec alignment** — implementation matches the contracts in spec 04 and the requirements in spec 02/05
7. **CLAUDE.md updated** — if new build commands, components, or conventions were introduced

---

## 6. Development Workflow (Day-to-Day)

### Starting a New Feature

```bash
# 1. Start from latest main
git checkout main && git pull

# 2. Create feature branch
git checkout -b feature/pid-controller

# 3. Build to verify clean starting point
cmake --build build

# 4. Implement (write tests alongside or before code)
# ... edit files ...

# 5. Build + test frequently
cmake --build build && ctest --test-dir build --output-on-failure

# 6. Format before committing
clang-format -i backend/src/**/*.{h,cpp}

# 7. Commit with meaningful message
git add <specific files>
git commit -m "Add PID controller with anti-windup and acceleration ramp"

# 8. Push and open PR
git push -u origin feature/pid-controller
gh pr create --title "Add PID controller" --body "..."
```

### Working with Claude Code

```bash
# Start Claude Code in the project directory
claude

# Claude reads CLAUDE.md automatically for project context
# Reference specs when asking for implementation:
# "Implement the PID controller per spec 03a, Component Detail: PID Controller"
# "Write unit tests for MotorModel per spec 07, Section 1"
# "Add the LoadProtocol gRPC handler matching the contract in spec 04"
```

**Tips for effective Claude Code use:**
- Reference specific spec sections — Claude generates more consistent code when it has explicit contracts.
- Ask Claude to write tests first (from acceptance criteria), then implementation. The tests become the specification.
- After implementation, ask Claude to run the tests and fix any failures.
- Review generated code for thread safety — Claude may not always get lock ordering or atomic usage right without explicit prompting.
- Update `CLAUDE.md` after significant changes so the next session has current context.

### Debugging Multi-Process

```bash
# Terminal 1: Backend with debug logging
./build/backend/injector-backend --config config.json 2>&1 | tee backend.log

# Terminal 2: Frontend
./build/frontend/injector-frontend --backend=localhost:50051

# Terminal 3: gRPC test client (for targeted testing without UI)
./build/backend/tests/injector-integration-tests --gtest_filter="GrpcIntegration.CommandRoundTrip"

# Attach debugger to backend only (frontend can keep running)
# VS Code: launch.json with "attach to process" configuration
```

### Handling Test Failures

1. **Read the failure message.** gtest output tells you exactly what failed and what was expected.
2. **Check if it's a real failure or a flaky test.** Run the failing test 3 times in isolation. If it's flaky, it's a bug in the test (likely a threading issue).
3. **For threading issues:** Run under TSan first. It will tell you exactly which access is racy.
4. **For timing-related failures:** Check if the test depends on wall-clock time. It shouldn't. If it does, fix the test to use deterministic time.
5. **Never skip or disable a failing test.** Fix it or revert the change that broke it.
