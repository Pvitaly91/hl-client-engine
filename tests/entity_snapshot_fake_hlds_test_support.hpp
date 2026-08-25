#pragma once

namespace hlclient::test_support {

// Thin test-only entry points backed by the single fake-HLDS route fixture.
// They keep focused stage coverage from copying the full handshake harness.
void require_entity_snapshot_happy_route();
void require_entity_snapshot_duplicate_and_wrong_endpoint_routes();
void require_entity_snapshot_timeout_routes();
void require_entity_snapshot_replay_route();
void require_entity_snapshot_cancellation_route();

} // namespace hlclient::test_support
