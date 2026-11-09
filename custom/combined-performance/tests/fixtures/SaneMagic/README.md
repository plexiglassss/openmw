# Captured Sane Magic baseline fixtures

These two gzip files preserve the exact installed Sane Magic scripts from the
November 9 performance capture, including the prior accepted live-view and
combat-notification fixes. They are test inputs, not additional installation
payloads. Compression uses a zero timestamp and no embedded filename. The parity
harness decompresses them and checks the original script SHA-256 values before
executing them under controlled game services. This preserves every captured
byte, including the original trailing whitespace.

| Fixture | Decompressed script SHA-256 |
| --- | --- |
| `npc.lua.gz` | `f9a3e0e338f8b34319ece5106ed189f79a575fdc5c3874dbb2c5e7502a57a7be` |
| `daesumm_pn.lua.gz` | `a5d741551d4fac3b62f46fe6ed2b3480e62aa7c47b079e9e14ec6dd611c7e12c` |

The test compares these captured originals with the candidate adapters in
`custom/combined-performance/mods/SaneMagic/scripts/SaneMagic`. It does not
derive the expected baseline from the adapter implementation. No configuration
contents or private installation paths are included.

An explicit `--baseline-dir` may supply raw `npc.lua` and `daesumm_pn.lua`
instead. Both input formats must match the same original hashes.
