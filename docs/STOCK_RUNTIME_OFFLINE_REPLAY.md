# Stock runtime offline replay

## Pure replay boundary

`StockRuntimeCaptureCorpusLoader` validates one exact ignored run directory,
all schemas, run ID, filenames, bounds, hashes, counters and emitted-order
references before publishing immutable owning corpus state. Production runtime
APIs never retain the corpus bytes.

`StockRuntimeTransportReplay` is offline only: no sockets, timers, network
writes, generated ACKs or generated client requests. It replays the delivered
streams—not merely observed relay input—through existing direction-specific
GoldSrc codecs. Connectionless challenge/connect/accept/reject boundaries are
recognized while authentication remains opaque and unlogged.

The corpus adapter passes non-owning immutable datagram views into replay; it
does not duplicate every raw capture. Separate aggregate limits bound all
peer-delivered datagram bytes (including duplicate emissions) and all retained
replayed payload bytes (including BZip2 expansion), in addition to per-item and
count limits. Limit-plus-one fails with a typed result before partial replay
publication.

Reconnect uses the same pure replay through a bounded delivered-datagram span
overload. The strict staged transport observation supplies two nonempty,
ordered, non-overlapping observed-ordinal ranges. Each range is copied into an
independent view and replayed from a fresh netchan/signon state; no state or
candidate is duplicated from generation A into B. Before B's fresh ACCEPT,
sequenced S2C datagrams belonging to the retired-A routing tail are excluded
from B's view and counters while remaining intact in the full local journal.
The checker and PowerShell walker reconstruct both boundaries independently and
must agree on each exact geometry and the combined structural hash.

## Sequenced, fragment and compressed data

The replay adapter reuses the established low-30-bit sequence/header policy,
offset-8 transform, reliable flags, duplicate/old filtering and normal fragment
slot-0 codec. Drops, duplicates, delay and reorder therefore preserve exact
peer-visible order. Slot 1 remains `secondary_stream_pending`; it is never
silently treated as normal data.

Normal fragments are reassembled in memory under corpus limits. The existing
bounded `BZ2\0` decoder checks the exact envelope, compressed/decompressed
bounds and trailing bytes with no file I/O. Published payload metadata retains
direction, source sequence, reliable/fragment/reassembled/decompressed state,
corpus ordinal and delivery provenance.

## Sign-on replay and exact boundary

`StockCapturedSignonReplay` composes existing pure codecs for the confirmed
initial request/service batch, serverinfo, post-serverinfo control, delta
descriptions, movevars, user-info, `sendres`, resource transition/list and the
opcode-5 resource-response boundary. It matches the stock client's observed
C2S requests in order and never synthesizes a missing request or response.

Successful replay publishes the exact `PostResourceResponseBoundary`: payload
ordinal, byte offset, bit offset, source sequence, reassembly/decompression
metadata and remaining unconsumed bits. There is no opcode scanning or
resynchronization. Typed failures distinguish connection, netchan, fragment,
decompression, sign-on, resource-response, cursor, secondary-stream and
incomplete-capture boundaries.

M4.7.1.1 stops after the first unconsumed candidate. M4.7.1.2 owns evidence-
backed runtime grammar. M4.7.2 owns the stock usercmd envelope/checksum/ACK.
