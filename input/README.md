# Local game files

This directory is intentionally excluded from version control and release
archives. Copy files from your own **Disruptor (USA, SLUS-00224)** disc here:

- `SLUS_002.24` — the extracted boot executable.
- `Disruptor (USA).cue` and its referenced `.bin` track.

Verified SHA-256 identities are listed in `../DISC.md`. In particular, the BIN
must be 636,350,064 bytes and hash to
`3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4`.

If your disc dump is a split archive such as `Disruptor-disc.7z.001` and
`.002`, keep every part in one directory and extract the `.001` file with
7-Zip. It will automatically consume the remaining numbered parts and produce
the single BIN/CUE pair expected here.

The build creates `SLUS_002.24.code` beside these files as a temporary analysis
image. It contains only the verified resident-code interval and is safe to
delete; the retail executable is never changed. Both files remain private and
must never be staged, committed, or published.

If your cue sheet has a different filename, change `game.disc` in
`../game.toml`. Do not rename the track referenced *inside* the cue sheet unless
you update its `FILE` line too.
