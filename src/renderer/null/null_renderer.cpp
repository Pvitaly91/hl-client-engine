#include <hlclient/renderer/null/null_renderer.hpp>

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_visible_draw_list.hpp>

#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace hlclient::renderer::null {

NullRenderer::~NullRenderer() noexcept
{
    shutdown();
}

const RendererInfo& NullRenderer::information() const noexcept
{
    return information_;
}

void NullRenderer::render(const RenderScene& scene, const RenderExtent extent)
{
    if (!core::valid_image_adjustments(scene.image_adjustments))
        throw std::invalid_argument{"Image adjustments are outside the finite bounded contract"};
    if(scene.requested_msaa_samples!=0 && scene.requested_msaa_samples!=2 &&
       scene.requested_msaa_samples!=4 && scene.requested_msaa_samples!=8)
        throw std::invalid_argument{"Scene MSAA request must be Off, 2, 4 or 8"};
    if (!statistics_.initialized) {
        throw std::logic_error{"Null renderer must be initialized before rendering"};
    }
    if (scene.dynamic_entities &&
        (!scene.dynamic_entities->package ||
            !scene.dynamic_entities->frame)) {
        throw std::invalid_argument{
            "Dynamic entity scene requires both a package and a frame"};
    }

    statistics_.last_clear_color = scene.clear_color;
    statistics_.last_decal_submitted=statistics_.last_decal_valid=statistics_.last_decal_rejected=0;
    const auto validate_decal=[&](const RenderWorldDecals& decal) {
        ++statistics_.decal_submitted;++statistics_.last_decal_submitted;
        const auto reject=[&](std::uint64_t& reason) {
            ++reason;++statistics_.decal_rejected;++statistics_.last_decal_rejected;
        };
        if(!decal.texture || !decal.vertices) {reject(statistics_.decal_missing_resource);return;}
        const auto& pixels=decal.texture->mip_levels[0];
        if((decal.texture->alpha_mode!=assets::WorldTextureAlphaMode::masked_index_255 &&
            decal.texture->alpha_mode!=assets::WorldTextureAlphaMode::straight_alpha) ||
            !pixels.width || !pixels.height || pixels.width>256U || pixels.height>256U ||
            pixels.width!=decal.texture->width || pixels.height!=decal.texture->height ||
            pixels.rgba_pixels.size()!=std::size_t(pixels.width)*pixels.height*4U) {
            reject(statistics_.decal_invalid_texture);return;
        }
        if(decal.material_mode!=game_api::LocalDecalMaterialMode::straight_alpha &&
            decal.material_mode!=game_api::LocalDecalMaterialMode::white_neutral_modulate &&
            decal.material_mode!=game_api::LocalDecalMaterialMode::lit_straight_alpha) {
            reject(statistics_.decal_unsupported_material);return;
        }
        if(!is_finite(decal.model_transform)) {reject(statistics_.decal_invalid_transform);return;}
        if(decal.vertices->empty() || decal.vertices->size()>64U*96U || decal.vertices->size()%3U ||
            std::any_of(decal.vertices->begin(),decal.vertices->end(),[](const auto& v) {
                return !is_finite(v.position) || !is_finite(v.uv) || v.uv.x<-.001F || v.uv.x>1.001F ||
                    v.uv.y<-.001F || v.uv.y>1.001F ||
                    std::any_of(v.surface_light_rgb.begin(),v.surface_light_rgb.end(),[](float c){return !std::isfinite(c)||c<0||c>1;});
            })) {reject(statistics_.decal_invalid_geometry);return;}
        ++statistics_.decal_valid;++statistics_.last_decal_valid;
    };
    if(scene.world_decals)validate_decal(*scene.world_decals);
    if(scene.secondary_world_decals)validate_decal(*scene.secondary_world_decals);
    const auto decal_batches=std::min<std::size_t>(scene.additional_world_decals.size(),64U);
    for(std::size_t i=0;i<decal_batches;++i)validate_decal(scene.additional_world_decals[i]);
    if(scene.additional_world_decals.size()>decal_batches) {
        const auto rejected=scene.additional_world_decals.size()-decal_batches;
        statistics_.decal_batch_limit+=rejected;statistics_.decal_submitted+=rejected;
        statistics_.decal_rejected+=rejected;statistics_.last_decal_submitted+=rejected;
        statistics_.last_decal_rejected+=rejected;
    }
    if(scene.transient_sprite_entities &&
       (!scene.transient_sprite_entities->package || !scene.transient_sprite_entities->frame ||
        scene.transient_sprite_entities->frame->scene_package_identity()!=
          entity_render::EntityRenderResourceIdentity{scene.transient_sprite_entities->package->resource_id(),
              scene.transient_sprite_entities->package->resource_revision()}))
        throw std::invalid_argument{"Transient sprite package/frame identity is invalid"};
    for(const auto* sprites:{&scene.first_person_muzzle_entities,&scene.world_muzzle_entities})
        if(*sprites && (!(*sprites)->package || !(*sprites)->frame ||
            (*sprites)->frame->scene_package_identity()!=entity_render::EntityRenderResourceIdentity{
                (*sprites)->package->resource_id(),(*sprites)->package->resource_revision()}))
            throw std::invalid_argument{"Muzzle sprite package/frame identity is invalid"};
    statistics_.camera_valid = is_valid(scene.camera);
    statistics_.last_camera = scene.camera;
    statistics_.static_world_present =
        scene.static_world.has_value() && scene.static_world->package != nullptr;
    if (statistics_.static_world_present) {
        statistics_.package_resource_id = scene.static_world->package->resource_id();
        statistics_.package_revision = scene.static_world->package->resource_revision();
    } else {
        statistics_.package_resource_id.reset();
        statistics_.package_revision.reset();
    }
    statistics_.scene_package_present = scene.static_world.has_value() &&
        scene.static_world->scene_package != nullptr;
    if (statistics_.scene_package_present) {
        statistics_.scene_resource_id =
            scene.static_world->scene_package->resource_id();
        statistics_.scene_revision =
            scene.static_world->scene_package->resource_revision();
    } else {
        statistics_.scene_resource_id.reset();
        statistics_.scene_revision.reset();
    }
    statistics_.visible_draw_list_present = scene.static_world.has_value() &&
        scene.static_world->visible_draw_list != nullptr;
    statistics_.visibility_present = scene.static_world.has_value() &&
        scene.static_world->visibility_summary.has_value();
    if (statistics_.visibility_present) {
        statistics_.visibility_revision =
            scene.static_world->visibility_summary->revision;
        statistics_.visible_world_surface_count =
            scene.static_world->visibility_summary
                ->visible_world_surface_count;
        statistics_.visible_brush_instance_count =
            scene.static_world->visibility_summary
                ->visible_brush_instance_count;
    } else {
        statistics_.visibility_revision.reset();
        statistics_.visible_world_surface_count = 0U;
        statistics_.visible_brush_instance_count = 0U;
    }
    statistics_.visible_draw_command_count =
        statistics_.visible_draw_list_present
        ? scene.static_world->visible_draw_list->statistics().command_count
        : 0U;
    statistics_.dynamic_entity_package_present =
        scene.dynamic_entities.has_value();
    if (statistics_.dynamic_entity_package_present) {
        const auto expected_identity =
            entity_render::EntityRenderResourceIdentity{
                scene.dynamic_entities->package->resource_id(),
                scene.dynamic_entities->package->resource_revision()};
        if (scene.dynamic_entities->frame->scene_package_identity() !=
            expected_identity) {
            throw std::invalid_argument{
                "Dynamic entity frame does not belong to its scene package"};
        }
        const auto world_association =
            scene.dynamic_entities->package->world_scene_association();
        if (world_association &&
            (!scene.static_world || !scene.static_world->scene_package ||
                world_association->resource_id !=
                    scene.static_world->scene_package->resource_id() ||
                world_association->revision !=
                    scene.static_world->scene_package->resource_revision())) {
            throw std::invalid_argument{
                "Dynamic entity scene does not match its associated world scene"};
        }
        statistics_.entity_scene_resource_id =
            scene.dynamic_entities->package->resource_id();
        statistics_.entity_scene_revision =
            scene.dynamic_entities->package->resource_revision();
        statistics_.entity_frame_revision =
            scene.dynamic_entities->frame->resource_revision();
        statistics_.studio_entity_instance_count =
            scene.dynamic_entities->frame->studio_instances().size();
        statistics_.sprite_entity_instance_count =
            scene.dynamic_entities->frame->sprite_instances().size();
        statistics_.visible_entity_count =
            scene.dynamic_entities->visibility_summary.visible_count;
        statistics_.unsupported_entity_count =
            scene.dynamic_entities->visibility_summary
                .unsupported_instance_count;
    } else {
        statistics_.entity_scene_resource_id.reset();
        statistics_.entity_scene_revision.reset();
        statistics_.entity_frame_revision.reset();
        statistics_.studio_entity_instance_count = 0U;
        statistics_.sprite_entity_instance_count = 0U;
        statistics_.visible_entity_count = 0U;
        statistics_.unsupported_entity_count = 0U;
    }
    statistics_.last_extent = extent;
    ++statistics_.rendered_frames;
}

void NullRenderer::initialize() noexcept
{
    statistics_.initialized = true;
    statistics_.shutdown = false;
}

void NullRenderer::shutdown() noexcept
{
    statistics_.initialized = false;
    statistics_.shutdown = true;
}

NullRendererStatistics NullRenderer::statistics() const noexcept
{
    return statistics_;
}

} // namespace hlclient::renderer::null
