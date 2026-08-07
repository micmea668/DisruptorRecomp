# Contributing

This repository is a source-only continuity checkpoint. Contributions must not
contain data derived from a retail Disruptor disc beyond factual addresses,
hashes, configuration, tests, or independently authored patches.

Never commit:

- a disc image, cue sheet, extracted executable, asset, save, screenshot, log,
  result archive, or memory capture;
- generated translations of retail MIPS code or captured overlay code;
- a built executable or proprietary PlayStation BIOS;
- credentials, machine-specific paths, or unrelated framework changes.

Change project source, configuration, seeds, or `psxrecomp-overlay/`, then
regenerate locally. Any intentional framework-overlay addition must also be
listed in `PSXRECOMP_OVERLAY_FILES.txt`. Do not hand-edit `generated/*.c`. Keep
experimental visual features opt-in and record both successful and negative
user-visible results in `STATUS.md`.

Before committing, run the relevant deterministic tests and inspect both
`git diff` and `git diff --cached --name-only`. The ignore rules are a guardrail,
not a substitute for reviewing every staged path.
