#pragma once

#include "local_resource_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"

#include <hlclient/assets/asset_source.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include <memory>
#include <stdexcept>

namespace hlclient::tests {

// Goes through the real approved resolver/import operation, not a test-only
// texture generator. The returned snapshot owns its image after root teardown.
inline assets::WorldTextureSet make_missing_texture_set()
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "valid.wad", synthetic_valid_wad3("OTHER").bytes);
    SyntheticBspBuilder builder;
    constexpr std::string_view entity =
        "{\n\"classname\" \"worldspawn\"\n\"wad\" \"valid.wad\"\n}\n";
    const auto entity_bytes = std::as_bytes(std::span{entity.data(), entity.size()});
    builder.lump(SyntheticBspLumpId::entities).assign(entity_bytes.begin(), entity_bytes.end());
    auto bytes = builder.build();
    assets::AssetSourceMetadata metadata;
    metadata.content_size = bytes.size();
    auto source = assets::AssetSource::create("maps/missing.bsp", std::move(bytes), metadata);
    auto imported = goldsrc::bsp::GoldSrcBspWorldImporter{}.import(*source.source);
    auto roots = local_resources::LocalResourceSearchRoots::create(root.path(), "valve");
    auto environment = local_resources::LocalResourceEnvironment::create(std::move(*roots.roots));
    goldsrc::GoldSrcWorldTextureImportLimits limits;
    limits.missing_texture_policy = goldsrc::MissingWorldTexturePolicy::placeholder_for_absent_name;
    auto operation = goldsrc::WorldTextureImportOperation::begin(
        imported.value(), source.source->bytes(),
        std::shared_ptr<const local_resources::LocalResourceEnvironment>{std::move(environment.environment)}, limits);
    if (!operation) throw std::runtime_error{"Missing-texture fixture operation failed"};
    for (std::size_t i = 0U; i < 8192U && !operation.operation->terminal(); ++i)
        operation.operation->update(goldsrc::WorldTextureImportTimePoint{});
    auto result = operation.operation->take_result();
    if (!result || result->statistics().placeholder_material_count != 1U)
        throw std::runtime_error{"Missing-texture fixture did not retain a placeholder"};
    return std::move(*result);
}

} // namespace hlclient::tests
