# Windows reparse provenance

## Purpose

M4.7.1.1.4 adds a read-only Windows boundary for diagnosing reparse points
without first following them. The boundary opens the link with
`FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS`, obtains a bounded
`FSCTL_GET_REPARSE_POINT` response, and validates every header, length, offset
and UTF-16 slice before decoding it.

This is filesystem provenance only. It does not infer a protocol opcode,
runtime entity field, authority rule or user-command format.

## Public and private data

The private observation retains the exact 32-bit tag, Microsoft and
name-surrogate bits, directory/file bit, payload length and SHA-256, and—for a
documented path contract—the substitute name, print name, relative flag and a
diagnostic normalized expression. These values can fingerprint a workstation
and must remain inside the ignored review directory.

Public output is path-free. It contains only a target ordinal and typed values:
tag category, expression kind, reachability, failure phase, native-error
category, inventory availability and eligibility. It never prints the raw tag,
target expression, final handle path, file identity or payload digest.

## Decoding and tag policy

The decoder independently validates literal mount-point and symbolic-link
payloads. It checks the common reparse header, exact data length, path-header
size, even UTF-16 offsets and lengths, checked ranges, non-overlapping
substitute/print ranges and the symbolic-link flags. A NUL terminator is not
required. A malformed range is never sliced or reinterpreted as a target.

Known tags are grouped as mount point, symbolic link, AppExecLink, Cloud and
Cloud variants, WCI and WCI tombstone, WOF, dedup, HSM, DFS, SIS, projected file
system, other Microsoft name-surrogate/non-name-surrogate, and third-party
name-surrogate/non-name-surrogate. Only mount-point and symbolic-link payloads
have a supported path contract. Every other payload is bounded and hashed
privately, classified as opaque, and is not followed.

Target expressions are diagnostic, not identity. Their kinds are relative,
drive-absolute, volume-GUID, NT object-manager, UNC, device, application
execution alias, opaque non-path, none and malformed. Target identity requires
a followed handle plus its volume serial and 128-bit file ID; lexical path
normalization never establishes identity.

## Reachability and failure

The followed-handle attempt reports reachable, target path/component/volume
not found, access denied, not a directory, remote/device, cycle, excessive
depth, changed, other open failure or not applicable. Native errors are retained
privately and mapped to stable public categories. In particular,
`ERROR_PATH_NOT_FOUND` is not eligibility and is never interpreted as an empty
directory.

A readable dangling junction or symlink, missing-volume mount, unsupported
opaque tag, malformed payload, inaccessible target or bounded nested failure
can complete a diagnostic review. Completion means the reason was observed
safely; it is not approval. Dangling links are ineligible, unsupported tags
stay fail-closed, unavailable inventory values are reported as unavailable—not
as zero—and no missing target is created, repaired, deleted or materialized.

Bounds cover payload bytes, target-expression characters, failure witnesses,
nested reparse depth and diagnostic target count. Exceeding a bound fails
closed.
