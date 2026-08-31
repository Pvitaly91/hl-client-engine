# Stock runtime external-target review

## Purpose and status

M4.7.1.1.3 defines a read-only review step for the exceptional case where the
stock Half-Life source tree contains directory links whose physical targets are
outside the source root. Review is a filesystem-provenance operation. It does
not authorize materialization, launch a stock process, inspect protocol bytes,
or produce runtime evidence.

This document describes the fail-closed interface. It does not claim that a
real installation produced an eligible review, that an approval or research
copy was created, or that a canary or stock-runtime campaign was accepted.

## Read-only command

```powershell
.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -ReviewExternalTargets `
  -SourceHalfLifeRoot "D:\Steam\steamapps\common\Half-Life" `
  -ReviewOutputRoot ".\manual-artifacts\stock-runtime-source-review"
```

`-ReviewExternalTargets` is source-only. It must not resolve, create or alter a
destination, follow an unbounded path, change the Steam tree, or materialize a
copy. The wrapper accepts only the repository-local
`manual-artifacts/stock-runtime-source-review` parent and returns an opaque
32-lower-hex `review-id`; it suppresses the native absolute review-root handoff.
Public output is bounded to the review schema/status, aggregate counts,
per-target ordinal/classification/counts and the opaque review ID. The private
review-set digest is not printed. Public output never contains a source-relative link path, target
path, private identity, target filename, username or source/target content
hash.

Every new successful diagnostic publishes the following private, ignored v2
JSON files under
`manual-artifacts/stock-runtime-source-review/<review-id>/`:

- `review-request.json`, schema
  `hlclient.stock-runtime-external-target-review-request.v2`;
- `target-0001-private.json` through the exact target count, schema
  `hlclient.stock-runtime-external-target-private.v2`;
- `review-summary.json`, schema
  `hlclient.stock-runtime-external-target-review-summary.v2`.

The public review command reports interface schema
`hlclient.stock-runtime-external-target-review.v2`. The private request and
target records intentionally retain the normalized source-relative link path,
canonical source/target paths, final-path identities, volume/file IDs, reparse
identity, inventory digest and bounded classification counts. Those records
are local operator material: do not commit, upload or paste them into an issue
or report.
They contain metadata rather than source file bytes, protocol packets,
authentication material or runtime evidence.

The review inventories the complete source through retained Windows handles.
For every potentially eligible escaped directory target it binds the logical
entry, reparse kind/tag, physical target identity and content/topology
fingerprints into one deterministic review digest. The review neither embeds
runtime semantics nor labels any protocol message. It is not an evidence
claim.

A complete review returns exit code 0 even when `result=ineligible`; that is a
diagnostic result, not approval. Malformed arguments, unsafe output placement,
an incomplete scan or publication failure returns nonzero.

M4.7.1.1.4 retains strict readers for complete eligible v1 review/approval
sets; it does not silently extend their schemas. The new strictly versioned v2
request, private-target and summary artifacts carry exact reparse diagnostics.
A v2 target records tag category,
expression kind, reachability, failure phase, typed native-error category and
whether inventory is available. Raw tag, payload digest, expression, numeric
native error and any handle-derived identity remain private. Public output is
path-free. A readable dangling or unsupported target can complete with
`result=ineligible` and exit 0; unavailable inventory counters are absent or
`unavailable`, never fabricated zeros. `ERROR_PATH_NOT_FOUND` is not
eligibility. A missing target is not repaired, created, approved or
materialized, and an opaque tag is not followed.

## Eligibility is narrow

Review may classify an external target as approval-eligible only when the
target is a stable, bounded, local fixed-volume directory tree and every
materialized leaf is non-executable and non-mutable for the research
transaction. Executables, DLLs, launchers, scripts, configuration or other
mutable runtime inputs are not eligible. A reviewed target is not automatically
approved.

Eligibility also requires exact Half-Life application provenance. The source
and target must each resolve inside a physically identified
`steamapps/common/Half-Life` application root backed by an ordinary, bounded,
unambiguous `appmanifest_70.acf` with exact AppID/installation metadata. A
separately installed AppID-70 root may participate only when the source
installation's ordinary, bounded, single-link/no-ADS `libraryfolders.vdf`
lists both physical library roots under exact nested AppID-70 membership and
the secondary root has its own exact app manifest. The application roots,
manifests and library-membership file identities, final-path bindings and
content digests are folded into the private target inventory digest. A
lookalike directory, missing/duplicate/conflicting or unlisted registration,
other application, workshop tree or arbitrary assets-only directory is
ineligible.

An allowlisted extension is only a hint. Eligible binary assets must pass the
corresponding bounded structural checks. The current evidence profile accepts
only the exact empty BSP v29/v30 form (124-byte header, all 15 offsets equal to
124 and all lengths zero) and the exact empty WAD2/WAD3 form; every non-empty
BSP/WAD remains `unknown` until full lump parsers are added. Executable,
platform-binary, shortcut and package/archive magic is scanned across the
complete file, and textual UTF-8/UTF-16 content fails closed. Formats without a
sufficiently strict validator remain `unknown`; a renamed script or archive
cannot become eligible by using an asset extension.

The exact classifications are
`eligible_non_executable_asset_tree`, `contains_executable_code`,
`contains_script_or_command`, `contains_mutable_user_state`,
`another_application_tree`, `operating_system_tree`,
`temporary_or_cache_tree`, `remote_or_device_target`,
`nested_external_link`, `unsupported_reparse_topology`,
`content_limit_exceeded`, `changed_during_review` and `unknown`. Only the first
classification can pass the approval helper.

The following remain non-materializable and fail closed:

- arbitrary, newly discovered or incompletely inventoried external links;
- executable-bearing or mutable targets;
- volume mount points, unsupported reparse tags, cycles or excessive depth;
- alternate data streams, remote/UNC/substituted-volume targets;
- entry, path, stream, file-size or aggregate-byte bound exhaustion;
- identity, content, topology or target drift during review.

Contained links continue through the ordinary strict materialization policy and
need no external-target approval. See
[external-target approval](STOCK_RUNTIME_EXTERNAL_TARGET_APPROVAL.md) for the
separate human authorization boundary.

External executable/code or mod targets remain forbidden. This filesystem
review does not alter production protocol behavior: runtime-body parsing
remains M4.7.1.2 work and stock usercmd remains M4.7.2 work.
