# LLL-TAO Documentation Index

This index organizes active documentation by purpose. Current behavior and invariants live in `docs/reference/`; historical investigations live in `docs/archive/`.

## Sections

| Section | Purpose | Key Documents |
|---|---|---|
| [build/](build/) | Platform-specific build instructions and flags | [Linux](build/build-linux.md), [OSX](build/build-osx.md), [Windows](build/build-win.md), [RISC-V](build/riscv-build-guide.md) |
| [guides/](guides/) | Operator and workflow how-to guides | [API How-To](guides/how-to-api.md), [Docker How-To](guides/how-to-docker.md), [Mining Auto Credit](guides/mining-auto-credit.md) |
| [architecture/](architecture/) | System architecture and design decisions | [Blockchain Flow Alignment](architecture/BLOCKCHAIN_FLOW_ALIGNMENT.md), [Session Freshness Hardening](architecture/SESSION_FRESHNESS_HARDENING.md), [RISC-V Design](architecture/riscv-design.md) |
| [reference/](reference/) | Canonical technical references and invariants | [Sigchain Last Resolution](reference/sigchain-last-resolution.md), [Hard-Won Invariants](reference/invariants.md), [nexus.conf](reference/nexus.conf.md) |
| [protocol/](protocol/) | Protocol-level behavior specifications | [Mining Protocol](protocol/mining-protocol.md) |
| [API/](API/) | API command and format reference | [API README](API/README.MD), [Queries](API/QUERIES.md) |
| [current/](current/) | Current implementation notes by domain | [Node Index](current/node/index.md), [Mining](current/mining/), [Testing](current/testing/) |
| [design/](design/) | Design proposals and implementation planning | [Unified Mining Server Architecture](design/unified-mining-server-architecture.md) |
| [diagrams/](diagrams/) | Architecture and protocol diagrams | [Diagram Index](diagrams/README.md), [Upgrade Path Diagrams](diagrams/upgrade-path/README.md) |
| [onboarding/](onboarding/) | New contributor onboarding and cheat sheets | [AI-Assisted Onboarding](onboarding/ai-assisted-onboarding.md), [New Coder Repo Guide](onboarding/new-coder-repo-guide.md) |
| [philosophy/](philosophy/) | High-level collaboration and system philosophy | [AI-Human Advancement](philosophy/ai-human-advancement.md), [Computing Paradigms](philosophy/computing-paradigms.md) |
| [release/](release/) | Release-specific notes and operator docs | [Windows Node Release Notes](release/windows-node/README.md) |
| [upgrade-guides/](upgrade-guides/) | Version upgrade instructions | [Upgrading to 5.1](upgrade-guides/upgrading-to-5.1.md) |
| [archive/](archive/) | Historical write-ups and superseded designs | [Archive README](archive/README.md), [NSEQ mempool ReadLast fix (superseded)](archive/NSEQ_DIAG_MEMPOOL_READLAST_FIX.md) |

## Conventions

- Prefer `docs/reference/` for canonical current behavior.
- Use `docs/archive/` for historical write-ups that describe superseded behavior.
- Keep cross-links relative and keep this index updated when moving docs.
