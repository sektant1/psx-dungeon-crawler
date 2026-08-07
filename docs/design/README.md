# Design docs {#doc-design-readme}

Living design documentation for the PSX dungeon crawler.

- **[GDD.md](GDD.md)** — Game Design Document. Player-facing intent: the game
  vision, core loop, village hub, combat, dungeon, risk/loss economy, quests &
  factions. What we are building and why it should feel good.
- **[GEDD.md](GEDD.md)** — Raven Engine Design Document. Engine implementation:
  tech stack, architecture, subsystems, scene model, game code layout, build,
  determinism, tooling, refactor roadmap. How it is built.
- **[Prototype reset and content pipeline](2026-07-27-prototype-content-reset-and-pipeline.md)** — prototype-only reset, `.scn` source / `.map` runtime design, fallbacks, and the first demo asset checklist.
- **[Content format research](2026-07-27-content-pipeline-format-research.md)** — primary-source research supporting the pipeline decision.
- **[Content-pipeline design review](2026-07-27-content-pipeline-design-review.md)** — role-guided blockers, contracts, VFX safety, gameplay integration, and QA gates.

Keep both current as features land. The GDD is the target; the GEDD is the truth.
