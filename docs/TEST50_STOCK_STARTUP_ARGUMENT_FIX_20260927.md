# Stock startup argument compatibility for the optional 50 HP profile

Result: `test50_stock_startup_argument_corrected_offline_verified`.
New live sessions=0; Steam initialization/WFP/capture=not_run;
staging/commit/push=none. Corrected-build manual validation=pending.

## Observed failure and confirmed defect

Run `34b226c8be7940a08ab519e532692cbc`, crossfire/reference/TestStartHealth=50,
server PID 37416. `attribution=user_report`: supplied terminal output ends with
server ready, client PID 0, and client initialization not reached. The retained
server log now includes both `stage=attached` and
`stage=server_activated configured=false`: the previous console-channel fix
worked. Native primary failure is
`test-start-health-helper-startup-not-confirmed`. The final same-run wrapper
attests owned-process cleanup=exact, restoration=exact, cleanup errors=0,
result=`local_server_ready_client_blocked`. No client exit code exists.

Read-only static inspection of the installed research `swds.dll` identified
the stock startup command parser through its unique stuffcmds usage-string
reference. It scans individual characters and ends a command at either `+`
or `-`, including inside an argument. Its command extraction therefore turns

```text
+localinfo hlc_test_profile test-server-assisted
```

into `localinfo hlc_test_profile test`. The helper required the untruncated
value, making this launch contract incompatible with the installed engine.
Quoting does not fix this character scanner. The raw localinfo value was not
retained in this run, so additional failed configuration prerequisites cannot
be excluded from the historical log; the incompatible argument is independently
proven and deterministically reproduced.

Binary evidence (no loading, execution or modification):

- `swds.dll` SHA-256:
  `64B6872F9C2875EA7DA88EB694A34E6E46F2AAC7CB35FB436744F462611C03AE`.
- Preferred PE image base `0x10000000`; inferred parser entry `0x101B6970`;
  usage string VA `0x102B22B4`, referenced at `0x101B6985`.
- Inner scanner compares the current byte with `0x2B` at `0x101B6A8F` and
  `0x2D` at `0x101B6A93`; both branch to delimiter handling `0x101B6AA4`.
  `0x101B6A9B` loads the next character; `0x101B6AAC` terminates the command.

The [current ReHLDS startup parser](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/cmd.cpp)
uses argument-token boundaries instead. Treating that implementation as proof
of installed-stock argument compatibility would miss this defect.

## Correction and preserved contracts

The native host and helper now use internal localinfo sentinel
`test_server_assisted`. The user-facing profile remains `test-server-assisted`;
there are no new manual arguments. Run ID, target name, map, slot, accepted
loopback connection and edict serial validation remain in place. The old
hyphenated sentinel and the truncated value `test` are rejected.

Helper configuration now reports a fixed reason enum: ready, run_missing,
run_invalid, profile_missing, profile_mismatch, globals_unavailable,
deathmatch_unavailable, client_limit_invalid or map_mismatch. The native
redactor retains only these reasons, and a known failure reason augments the
existing native primary failure. PowerShell exposes
`helper_configuration_reason` and preserves its failure reason. No raw
localinfo values, names, paths or authentication material are emitted.

Readiness still requires attached and configured ServerActivate markers.
The existing single first-spawn health write and the separate server-application
plus fresh receiving-client 50 HP confirmation remain unchanged. No increase
in deadline, fallback to another profile, fabricated health, respawn writer,
or change to client movement/gameplay/rendering was introduced.

Changed ordinary project files:

- `apps/hlclient_stock_runtime_orchestrator/main.cpp`: internal sentinel,
  fixed diagnostic retention and independent stock-parser contract fixtures.
- `test_server/start_health/plugin.cpp`: matching sentinel and specific
  configuration diagnostics through the already corrected ServerPrint channel.
- `test_server/start_health/offline_test.cpp`: exact keyed localinfo fixture,
  24 scenarios with developer=0 and developer=1, including each rejected input.
- `scripts/test_start_health_profile.psm1` and
  `scripts/test_test_start_health.ps1`: bounded enum evidence and regressions.

## Offline verification

Before editing, the existing source-copy/hash inventory preserved 167 dirty or
untracked source files in
`manual-artifacts/task-records/test50-stock-argument-prechange/source`.
All saved hashes remain intact; only the five listed source counterparts
changed. Historical reports/run files were not rewritten. This report is new.
Branch `codex/stock-runtime-campaign-5e48b7c1`, HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66` and staging are unchanged.

- Prechange helper baseline: 423 checks; native startup contract passed.
- The new legacy character-parser regression was built and run against the
  old production argv before changing the sentinel: it failed as expected.
  After the correction it passed in Debug and Release, including complete
  startup command extraction and preservation of subsequent map, maxplayers,
  sv_lan, meta require and status commands. No server or socket was started.
- Incremental helper and native host Debug/Release builds passed. Helper
  DLL/ABI fake-engine tests passed 773 checks per configuration. Native startup
  and functional-log checks passed in both configurations.
- Each helper configuration failure, developer filtering, early attach,
  incomplete configuration retry, invalid/truncated sentinel, wrong target,
  duplicate/healing/respawn/reconnect and one-shot health evidence were tested.
- Independent PowerShell failure/evidence fixtures and all 12 no-stock launcher
  cases passed. Enum-only diagnostics do not become health confirmation.
  The old run remains unconfirmed when read through the updated extractor.
- Component source/binary receipt was refreshed and verified. Actual launcher
  CheckOnly passed from system32 for reference/TestStartHealth=50 and ordinary
  off; reference/off/map/damage-respawn-check contracts remain available.
- Tracked `git diff --check` and task-source whitespace checks passed.
  Core-only, ASan and OpenGL suites were not rerun: engine/client/game module,
  rendering, wire, movement and restoration code did not change in this fix.
  No full suite, clean build, old build removal or unrelated process termination.

Prepared Release helper SHA-256:
`AC96548EFF7E4B39FA34543C0E17CEC3DC666338332E971BCE7A97A0EA232703`.
Prepared Release native host SHA-256:
`6A80418B05E2174859A9ED04CA396F1F0D1AA694208CB3EB59DC926AC117D07E`.
Unchanged Release client SHA-256:
`0DA3D959ABD294AD386F459568D078936AF6941868345C96C5209D394B043CC3`.

## Manual handoff

Actual corrected startup, first-spawn 50 HP and charger healing still require
the user's next manual session; offline success is not live proof.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```
