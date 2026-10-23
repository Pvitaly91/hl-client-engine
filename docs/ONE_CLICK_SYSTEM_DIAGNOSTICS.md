# One-click system diagnostics (offline implementation record)

The optional launcher checkbox **«Системна діагностика CPU/GPU»** attaches a
separate, bounded Windows helper to one launcher-owned run. The existing game
diagnostics and F8 marker remain unchanged and are enabled for that launch
without changing the saved game setting. The launcher-created 32-hex Run ID,
run directory, game PID and process-creation FILETIME bind every output.

The helper's one-second sampling runs outside the game process. It records
available CPU, process, memory, power, frequency and GPU Engine counters with
source, units and availability status. Unsupported sensor data is `null`:
**«Температурні обмеження не перевірені: датчики недоступні.»** No client,
server, power-plan, fan, priority or Windows security setting is changed.
Inputs do not include game command lines, window content or network packets.

The helper stores QPC/UTC anchor pairs at its own start and finish. Game F8
markers are frame-swap-completion markers, not key-down timestamps. A marker
can be related to sampled QPC time only when the game recorder's identity and
clock origin are available and consistent; one-second system samples cannot
prove the cause of a single short frame. Diagnostic ring buffers can evict
older context. The helper starts before the game in the opt-in path, but its
first *process* sample necessarily follows publication of the game's PID and
creation identity; that startup gap is not claimed as covered.

Data remains local. A run-bound `system-diagnostics` folder contains bounded
CSV/JSON samples, a manifest and a short report; after the client closes, the
helper packages allowlisted run leaves into `system-diagnostics.zip`. A crash,
cancel, missing sensor/WPR capability or storage limit produces an explicit
partial report rather than invented zero readings or a false complete trace.
The launcher opens the run folder after packaging. No upload occurs.

## WPR and thermal scope

WPR availability and light-profile details are probed read-only. This first
implementation never starts an ETW trace, asks for elevation, or stops a
foreign WPR session. On the development PC, the built-in `CPU.light` and
`GPU.light` profiles declare buffers larger than the 256 MiB helper budget,
so using them automatically would not meet the lightweight requirement. No
HWiNFO installation, driver or ADK is assumed. An ETL that was not recorded
is marked unavailable, not silently substituted by the sample CSV. This is a
deliberately partial system diagnosis, not proof that there was no thermal
throttling. Because no elevated recording is attempted, UAC refusal and a
foreign recording cannot disrupt or stop a trace owned by somebody else.

## Validation record

Release helper and launcher built successfully. Launcher `--offline-ui-check`
returned 0. `scripts/test_system_diagnostics.ps1` passed 112 checks with a
fake child: normal close, nonzero child exit, forced cancel, mismatched PID,
helper-ready before child startup, and launcher failure before client startup.
The archive checks include exact identity, required files, exclusion of
arbitrary/configuration files, and omission of an over-8-MiB source. These
separate Run IDs also exercise repeated invocation. The helper reports its
collection time and own CPU/working-set cost; no off-versus-on gameplay FPS
measurement was performed, so no FPS impact claim is made. No live game,
Steam, HLDS or ETW trace was started for this task. The initial project state
was pushed to `origin/main` as `1487256` **before** the diagnostics
implementation; these diagnostics changes are not committed or pushed.
