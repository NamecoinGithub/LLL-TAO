# Coding Agent Best Practices

## Purpose
Use this guide to create precise, diagram-first specs that fit within the 3,000 character limit and
reduce ambiguity for human-AI collaboration.

## Character Limit (3,000 chars)
- Treat 3,000 characters as a hard ceiling.
- Allocate 60-70% of the budget to implementation details.
- Prefer diagrams + bullet lists over prose to maximize signal density.

## Collaboration Framework (Human-AI)
### Communication Asymmetry
- AI reads ~1000x faster than humans; humans parse visuals faster than dense text.
- Write specs that humans can validate in seconds and agents can parse in full.

### Diagram Parity Principle
- Visuals give humans parity with AI scanning speed.
- Information density: 1 diagram can replace ~1000 words (rule of thumb).

### Validation Speed Advantage
- Validate a diagram in seconds vs minutes for long prose (rule of thumb).
- Shorten review cycles by front-loading visuals.

### Cognitive Load Reduction Strategies
- Chunk requirements into numbered lists.
- Keep each requirement atomic (one action per bullet).
- Use consistent labels across text and diagrams (component names, states).
- Move optional details to “Notes” to avoid cognitive overload.

## Diagram Types Taxonomy
- **Architecture diagrams**: component relationships and data flow.
- **State transition diagrams**: session lifecycle, lane switching.
- **Sequence diagrams**: handshake flows, recovery protocols.
- **Decision trees**: when to use patterns and error-handling paths.

## Optimal Diagram Formats
- Use monospace ASCII diagrams (render consistently in markdown).
- Keep diagrams <= 80 columns for readability.
- Label edges with verbs (e.g., “submit”, “validate”).
- Use one diagram per behavior (avoid mega-diagrams).

## ASCII Templates
### System Architecture (boxes + arrows)
```
[Client] ---> [API] ---> [Service] ---> [DB]
```

### Flow Chart (decision diamonds + process boxes)
```
[Start] -> [Validate] -> <Valid?>
             | yes       | no
             v           v
         [Process]   [Return Error]
             |           |
             v           v
           [End]       [End]
```

### State Machine (circles + transitions)
```
(o Idle) --start--> (o Active) --pause--> (o Paused)
(o Paused) --resume--> (o Active) --stop--> (o Stopped)
```

### Data Pipeline (producer → processor → consumer)
```
[Producer] -> [Processor] -> [Consumer]
```

## PR Structure Patterns
- **Problem**: 1-2 sentences, <150 chars.
- **Solution**: 1-2 sentences, <150 chars.
- **Architecture**: ASCII diagram + short legend.
- **Files**: list every file with a one-line purpose.
- **Implementation**: numbered requirements (60-70% budget).
- **Flow Diagram**: before/after behavior.
- **Acceptance**: testable checks.

## Precision Techniques
- Provide exact file paths and line numbers when possible.
- Name functions, classes, and structs explicitly.
- Use numbered steps for multi-step logic.
- Specify “Files to AVOID” when scope is tight.

## Agent Limitation Awareness
- **Build environment issues**: external dependencies may fail (e.g., Berkeley DB here); specify “skip build, implement only.”
- **Ambiguous requirements**: agent asks questions; provide exact file paths + line numbers.
- **Scope creep**: agent modifies unrelated files; add explicit “Files to AVOID.”
- **Integration confusion**: components don’t connect; add an architecture diagram.

## Iterative Refinement Patterns
- **When agent stalls**: “Focus on [specific component] only.” Provide precise locations.
- **When agent over-implements**: “Revert changes to [file]. Scope is [X only].”
- **When agent misunderstands**: send updated diagram + numbered steps.
