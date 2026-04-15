# New Coder Repo Guide

## 1. Start With the Actual Shape of the Repository

Treat the repo as a set of operating systems for one node, not as a single C++
application:

- `src/LLP/` - networking, mining, session management, failover, connection lifecycles
- `src/TAO/` - consensus, ledger, contracts, registers, API behavior
- `src/LLD/` - persistent storage and database access
- `src/Legacy/` - backward-compatible wallet and RPC surface
- `tests/unit`, `tests/smoke`, `tests/live`, `tests/bench` - test layers for different risk levels
- `makefile.cli` - the active build entrypoint in this branch

The practical onboarding path is:

```bash
sudo bash contrib/devtools/install-build-deps.sh
make -f makefile.cli -j$(nproc)
make -f makefile.cli UNIT_TESTS=1 -j$(nproc)
```

## 2. Current Automation Reality

Before proposing new automation, understand what already exists:

- `.devcontainer/devcontainer.json` sets up the developer container and calls `.devcontainer/setup.sh`
- `.devcontainer/setup.sh` delegates dependency installation to `contrib/devtools/install-build-deps.sh`
- `.github/workflows/copilot-setup-steps.yml` currently validates setup/install only
- As of 2026-04-15, no in-repo references to `AEP-agent-element-protocol` or `dynAEP-dynamic-agent-element-protocol` were found in the repository search across `src/`, `docs/`, `.github/`, and the top-level docs

Conclusion: AEP/dynAEP should currently be treated as optional orchestration around
the repository, not as a core runtime or consensus dependency.

## 3. The Architectural Truth to Learn Early

The most expensive failures here are cross-layer correctness failures:

- networking ↔ mining
- mining ↔ sync
- sync ↔ shutdown
- shutdown ↔ thread ownership

This means "performance" and "correctness" often collapse into the same problem.
Slow, ambiguous, or under-specified state transitions can become stale templates,
dropped sessions, blocked shutdown, or fragile failover.

Recommended reading order:

1. `README.md`
2. `docs/onboarding/ai-assisted-onboarding.md`
3. `src/LLP/` mining and session paths
4. `src/TAO/Ledger/` consensus and sync paths

## 4. Verified Bugs and Architectural Risks

These should drive workflow design and refactoring order.

### High Severity

1. **Recursive sync failover**
   - `src/LLP/tritium.cpp`
   - `TritiumNode::SwitchNode()` catches exceptions thrown while unsubscribing/syncing the current peer and then calls `SwitchNode()` again from inside the catch block
   - Risk: retry storms, fragile failure handling, stack amplification, poor observability

2. **Shutdown detach lifetime hazard**
   - `src/LLP/data.cpp`
   - DataThread destructor detaches a waiter after 500 ms if join does not complete
   - Risk: thread lifetime can outlive owning object assumptions

### Medium-High Severity

3. **Ambiguous sync batching semantics**
   - `src/LLP/tritium.cpp`
   - the actual `nLimits` counter in the code is checked by the outer loop with `nLimits > 0`, but it is decremented only inside the inner loop
   - Risk: hard-to-reason-about sync budgeting and future regressions

### Medium Severity

4. **Dual mining server lifecycle cost**
   - `src/LLP/include/global.h`
   - `src/LLP/global.cpp`
   - node runs both `MINING_SERVER` and `STATELESS_MINER_SERVER`
   - Risk: duplicated threads, lifecycle management, memory, and operational complexity

5. **Onboarding / CI command drift**
   - This PR fixes contributor-facing docs that previously referred to `make test`
   - This branch is built around `make -f makefile.cli ...`
   - Risk: new contributors use the wrong command path and misdiagnose the build

## 5. Optimization Paths That Reduce Risk

Prioritize changes that make system behavior easier to reason about:

1. Replace recursive sync failover with explicit bounded retry/state-machine logic
2. Replace detach-on-timeout shutdown with deterministic ownership and cooperative join semantics
3. Separate sync budget concepts: block count, transaction batch size, and buffer pressure
4. Keep shared mining abstractions, but only unify server internals where protocol differences are small
5. Keep docs, devcontainer instructions, and CI commands aligned

## 6. Options for Fixing the Most Serious Architectural Issues

### Option A — Stabilize Failure Paths First

- Scope: `SwitchNode()` retry model, shutdown/join lifecycle, sync budgeting semantics
- Correctness: **9/10**
- Simplicity: **8/10**
- Best first technical choice

### Option B — Add Workflow Guardrails First

- Scope: docs cleanup plus CI that runs dependency bootstrap, production build, and unit-test build
- Correctness: **7/10**
- Simplicity: **9/10**
- Best onboarding and regression-prevention choice

### Option C — Consolidate Mining Servers Behind a Shared Adapter

- Scope: move shared legacy/stateless lifecycle behavior behind one manager while keeping protocol-specific handlers separate
- Correctness: **8/10**
- Simplicity: **5/10**
- Good medium-term payoff, but more invasive

### Option D — Full Unified Mining Server / Socket Architecture

- Scope: collapse legacy and stateless lanes into one runtime/socket architecture
- Correctness: **6/10**
- Simplicity: **3/10**
- Highest redesign risk; not a good first move for a new coder

## 7. Recommended Sequences

- **Safest path:** B → A → C
- **Fastest architecture-first path:** A → B → C
- **Most ambitious path:** A → C → reconsider D only if operational scaling justifies it

## 8. Using AEP / dynAEP With `.devcontainer` Automated Testing

### Good Uses

- Test orchestration or scenario specification
- Structured onboarding prompts for new contributors
- Failure triage workflows
- Repeatable agent tasks for bootstrap, smoke build, and unit-test build

### Maybe Uses

- Codified build-and-test playbooks around existing repo commands
- Generated checklists for mining/session investigation paths

### Bad Use Right Now

- Making node runtime or consensus correctness depend on an external orchestration repo
- Making CI success depend on AEP/dynAEP before security, maintenance, and ownership fit are proven

## 9. Practical Workflow Additions

Recommended baseline automation:

1. setup validation: run `contrib/devtools/install-build-deps.sh`
2. smoke build: run `make -f makefile.cli -j2`
3. unit-test build: run `make -f makefile.cli UNIT_TESTS=1 -j2`
4. later: add targeted execution for the most stable test tags/suites

## 10. One Thing to Remember

In this repo, performance bugs often become correctness bugs. The best early
improvements are the ones that make thread ownership, retry behavior, and state
transitions obvious.
