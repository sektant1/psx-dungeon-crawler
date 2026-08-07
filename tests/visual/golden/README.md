# Visual goldens

One committed reference frame per capture, compared exactly (tolerance 0) by
`make visual-test`.

These are **not** under `artifacts/` on purpose: `artifacts/` is gitignored, so
a golden kept there is one nobody else and no CI ever compares against — which
is how the visual gate came to assert nothing at all for weeks.

## The rules

- A capture that differs from its golden **fails the run**, printing the number
  of differing pixels and the largest per-channel delta.
- A **missing** golden reports "not checked" and passes. It does not adopt
  silently: a gate that re-baselines itself cannot catch a regression.
- Adopting is explicit and reviewable:

  ```sh
  make visual-test VISUAL_ARGS="--adopt-golden"
  git add tests/visual/golden
  ```

  Look at the image before you commit it. That is the whole review.

## When a run fails

Two possibilities, and they are not distinguished automatically:

1. **You broke the image.** The PSX look — shaders, compositor, materials,
   presets — is a shipped result. Fix the change.
2. **You changed the image on purpose.** Adopt the new golden and say so in the
   commit message, so the diff is reviewable as a deliberate art change.

The capture that failed is in the run directory named in the error's `remedy`
field; compare it against the golden by eye, or numerically:

```sh
tools/image_diff.py tests/visual/golden/game.png <the failing capture>
```

## Determinism

Captures are byte-reproducible: the harness pins `RAVEN_FIXED_DT` and
`RAVEN_GEN_SEED`, and `Engine::setLoadingPhase(false)` rebases the step clock
and both abstract timelines so a variable-length load cannot shift which frame
is captured. Verified across repeated runs at frames 90 and 300.

If you ever see two consecutive unmodified runs disagree, that is a real
regression in determinism — not a flaky test — and it matters beyond this gate,
because replays and profiling rest on the same property.
