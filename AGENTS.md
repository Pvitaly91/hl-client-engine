# Delivery synchronization

The user maintains two active installations of this project:

- Local development-PC delivery: `D:\DEV\CPP\HL-Client-engine\build\bin\Release`.
- Shared delivery for another Windows 10 PC: `\\192.168.0.104\storage\HLClient-Win10-Test`.

Every client/launcher fix must be built and verified once from this working tree
and delivered to BOTH active installations before reporting it fully delivered.
Also keep `dist/HLClientEngine` and its canonical ZIP consistent with that Release
candidate through the existing audited packaging workflow. Do not fork the game
or launcher code by installation. Compare SHA-256 of changed shipped binaries
across both destinations; preserve rollback copies and matching package metadata.

Keep settings, Half-Life resource paths and diagnostic data separate per PC.
Never copy personal settings/logs/auth between installations or overwrite shared
`test-data`. Preserve the single native launcher and its optional diagnostics.
If a running program or unavailable share prevents replacement, report which
destination is pending and request closure/access; do not silently update only one.

Give the local launcher link for testing on this PC and the shared link for the
other PC. Historical checkout/delivery paths are not the active local entrypoint.
