# Supported disc revision

This project targets **Disruptor (USA), SLUS-00224, NTSC-U** only.

## Supplied-file identity

| File | Size | SHA-256 |
| --- | ---: | --- |
| `SYSTEM.CNF` | 68 bytes | `b706460d193883470d3e3f0b9f0bddfef76555811692008cabe44205e7d1e596` |
| `SLUS_002.24` | 401,408 bytes | `48e8c3143b7f5de10340c9d4a9bac8cb7e97c15eda7a0897d3cf337ad96cb2c4` |
| `Disruptor (USA).cue` | 81 bytes | `55af98bc2ba307ede0ecca3eca24cdba7d7a8e72323ae8f3bbc41acdc8c0e659` |
| `Disruptor (USA).bin` | 636,350,064 bytes | `3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4` |

`SYSTEM.CNF` declares:

```text
BOOT = cdrom:\slus_002.24;1
TCB = 4
EVENT = 16
STACK = 801FFFF0
```

The CUE declares one `MODE2/2352` data track at `00:00:00`. The BIN contains
270,557 raw sectors and must not be converted to a cooked ISO.

## Uploaded split-archive identity

The verified disc arrived as a two-part 7-Zip archive. These hashes identify
the transfer container, while the BIN hash above identifies the reconstructed
disc track:

| Part | Size | SHA-256 |
| --- | ---: | --- |
| `Disruptor-disc.7z.001` | 419,430,400 bytes | `00640949842d656235a69e26ad95ff9f772a898187aa35c5f86a77c36a14d7cc` |
| `Disruptor-disc.7z.002` | 29,630,248 bytes | `c136a41320e2d9495a6d22211bcb03f767b33d5acb4e0389e3660706e07de67e` |

The archive test passes, and a sector-aligned streamed reconstruction of all
eight local chunks reproduces the full BIN SHA-256 above.

## PS-X EXE layout

| Field | Value |
| --- | --- |
| Entry PC | `0x80048CE4` |
| Load address | `0x80010000` |
| Text size | `0x00061800` |
| Loaded text end | `0x80071800` |
| Initial stack | `0x801FFFF0` |
| Initial GP | `0x00000000` |
| Region marker | North America |

## Resident code range

The retail EXE mixes data and code in the single payload declared by its
header. Direct inspection establishes these boundaries:

- `0x80010000`–`0x800111FF`: strings and resident data.
- `0x80011200`–`0x80056937`: contiguous executable code.
- `0x80056938` onward: tables and other resident data.

`tools/extract_code_image.py` therefore creates a local, derived PS-X EXE
containing only the middle range. This prevents data bytes from being emitted
as false native functions. The original executable on the disc is never
modified and is still what the BIOS loads at runtime.

Do not assume two images are identical solely because they use the same serial;
the project accepts only the identities recorded above.
