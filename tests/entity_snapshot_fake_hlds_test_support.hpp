#pragma once

#include <hlclient/goldsrc/entity_snapshot.hpp>

#include <cstddef>
#include <memory>

namespace hlclient::test_support {

struct EntitySnapshotHappyRouteProof {
    std::shared_ptr<const goldsrc::EntitySnapshotHistoryState> snapshot_history;
    std::size_t network_endpoint_count{0U};
    std::size_t transmitted_packet_count_at_success{0U};
    std::size_t transmitted_packet_count_after_cleanup_checks{0U};
    std::size_t semantic_entity_request_count{0U};
    std::size_t cleanup_count{0U};
    std::size_t authentication_release_count{0U};
    std::size_t consistency_release_count{0U};
};

// Thin test-only entry points backed by the single fake-HLDS route fixture.
// They keep focused stage coverage from copying the full handshake harness.
[[nodiscard]] EntitySnapshotHappyRouteProof
acquire_entity_snapshot_happy_route_proof();
[[nodiscard]] EntitySnapshotHappyRouteProof
acquire_entity_snapshot_dropped_request_route_proof(std::size_t run);
[[nodiscard]] EntitySnapshotHappyRouteProof
acquire_entity_snapshot_dropped_acknowledgement_route_proof(std::size_t run);
void require_entity_snapshot_happy_route();
void require_entity_snapshot_duplicate_and_wrong_endpoint_routes();
void require_entity_snapshot_timeout_routes();
void require_entity_snapshot_replay_route();
void require_entity_snapshot_cancellation_route();

} // namespace hlclient::test_support
