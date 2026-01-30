# Coding Agent Cheat Sheet

## Character Budget Optimization
### Allocation by PR Complexity
- **Simple (1 file)**: Problem 200, Solution 150, Implementation 2200, Rest 450
- **Medium (3-5 files)**: Problem 150, Solution 100, Implementation 2400, Rest 350
- **Complex (7+ files)**: Problem 100, Solution 100, Implementation 2500, Rest 300

### Compression Techniques
- Replace "approximately line 450" with "line ~450" (saves ~10 chars).
- Use bullets instead of paragraphs (saves 20-30% space).
- Abbreviate file paths where unambiguous.
- Combine related changes into single requirement.

### Anti-Patterns to Avoid
- Code examples (often hundreds of chars per example).
- Redundant explanations (say once, precisely).
- Background history (agent doesn't need context).
- C++ syntax details (agent knows language).

## Diagram Reference
Use the ASCII templates in [CODING_AGENT_BEST_PRACTICES.md](CODING_AGENT_BEST_PRACTICES.md) or
[docs/diagrams](diagrams/README.md) for ready-to-copy diagrams.

## Case Studies (PRs #214-217)
### PR #214 Failure Analysis
- Issue: 7000 chars truncated at ~3000.
- Agent saw incomplete requirements.
- Missing: MiningContext helper methods.
- Result: Compilation errors.
- Lesson: Hard 3000 char limit enforcement.

### PR #215 Success Analysis
- Approach: 2950 chars, fully visible.
- Included: All required method signatures.
- Added: Unit test specifications.
- Result: Complete implementation.
- Lesson: Precision + brevity = success.

### PR #216 Failure Analysis
- Issue: Truncated instructions (>3000 chars).
- Missing: Integration points.
- Result: Incomplete unwrapping logic.
- Lesson: Verify char count before submission.

### PR #217 Innovation
- Breakthrough: Diagram-first documentation.
- Impact: Visual parity in collaboration.
- Validation: Instant comprehension.
- Lesson: Diagrams transcend text limitations.
