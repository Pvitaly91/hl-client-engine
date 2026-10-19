#include "gauss_world_visibility_control.hpp"

#include <hlclient/renderer/render_camera_math.hpp>

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::tests {

RuntimeWorldVisibility::RuntimeWorldVisibility(
    world_visibility::WorldVisibilityLimits limits,
    world_visibility::WorldVisibleDrawListLimits draw_limits) noexcept
    : limits_{limits},draw_limits_{draw_limits} {}

void RuntimeWorldVisibility::prepare_adapters(
    const std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>& package)
{
    std::vector<world_visibility::WorldVisibilityBrushInstanceInput> visibility_brushes;
    std::vector<world_visibility::WorldVisibleBrushModelInput> draw_models;
    std::vector<world_visibility::WorldVisibleBrushInstanceDrawInput> draw_brushes;
    visibility_brushes.reserve(package->brush_instances().size());
    draw_brushes.reserve(package->brush_instances().size());
    for(const auto& instance:package->brush_instances()) {
        const bool supported=instance.support_status==world_scene_render::
            BrushSubmodelRenderSupportStatus::supported_static_opaque && instance.source_model_index;
        visibility_brushes.push_back({instance.source_instance_index,instance.transformed_bounds,
            instance.touched_leaf_indices,supported});
        if(supported)draw_brushes.push_back({instance.source_instance_index,
            *instance.source_model_index,instance.model_transform});
    }
    const auto& library=package->brush_library();
    if(library.render_package()) {
        draw_models.reserve(library.models().size());
        for(const auto& model:library.models())
            draw_models.push_back({model.source_model_index(),
                library.render_package()->indices().size(),library.render_package()->materials().size(),
                model.surfaces()});
    }
    // The immutable package owns every span above. All signatures include the
    // complete original static brush adapters even though their drawing is
    // disabled below: live runtime_brushes remain the sole moving-brush source.
    visibility_brushes_=std::move(visibility_brushes);
    draw_models_=std::move(draw_models);draw_brushes_=std::move(draw_brushes);
    package_=package;
    visibility_.reset();draw_list_.reset();camera_.reset();
    ++statistics_.adapter_rebuilds;
}

void RuntimeWorldVisibility::publish(renderer::RenderStaticWorld& world) const noexcept {
    const auto identity=visibility_->scene_identity();
    const auto signature=visibility_->result_signature();
    world.visible_draw_list=draw_list_;
    world.visibility_summary=renderer::RenderStaticWorldVisibilitySummary{
        visibility_->revision(),identity.resource_id,identity.revision,
        identity.visibility_input_signature,identity.draw_input_signature,
        signature.first,signature.second,visibility_->statistics().visible_world_surface_count,
        visibility_->statistics().visible_brush_instance_count};
}

bool RuntimeWorldVisibility::apply(renderer::RenderScene& scene,
    const renderer::RenderExtent extent) noexcept
{
    if(!scene.static_world) {
        reset();statistics_.status=RuntimeWorldVisibilityStatus::no_scene;return false;
    }
    auto& world=*scene.static_world;
    // Clearing before any failure also protects reused RenderScene values.
    world.visible_draw_list.reset();world.visibility_summary.reset();
    const auto fail=[&](const RuntimeWorldVisibilityStatus status) noexcept {
        statistics_.status=status;++statistics_.failures;
        statistics_.camera_leaf.reset();statistics_.selection={};statistics_.draw_list={};
        statistics_.pvs_fallback=world_visibility::WorldPvsFallbackReason::none;
        return false;
    };
    if(!world.scene_package || !world.scene_package->world_package()) {
        reset();statistics_.status=RuntimeWorldVisibilityStatus::no_scene;return false;
    }
    if(world.package!=world.scene_package->world_package())
        return fail(RuntimeWorldVisibilityStatus::invalid_scene);
    if(extent.width<=0 || extent.height<=0 || !renderer::is_valid(scene.camera) ||
        !renderer::valid_render_screen_effects(scene.screen_effects))
        return fail(RuntimeWorldVisibilityStatus::invalid_camera_or_extent);
    // Includes nuke shake/roll and any final presentation correction: culling
    // must use the exact camera that the renderer will actually project.
    const auto final_camera=renderer::apply_screen_effects(scene.camera,scene.screen_effects);
    if(!final_camera)return fail(RuntimeWorldVisibilityStatus::invalid_camera_or_extent);
    try {
        if(package_!=world.scene_package)prepare_adapters(world.scene_package);
        if(camera_ && *camera_==*final_camera && extent_==extent && visibility_ && draw_list_) {
            publish(world);++statistics_.cache_hits;
            statistics_.camera_leaf=visibility_->camera_leaf_index();
            statistics_.pvs_fallback=visibility_->fallback_reason();
            statistics_.selection=visibility_->statistics();statistics_.draw_list=draw_list_->statistics();
            statistics_.status=RuntimeWorldVisibilityStatus::cached;return true;
        }
        if(next_revision_==0)return fail(RuntimeWorldVisibilityStatus::revision_exhausted);
        ++statistics_.resolves;
        const world_visibility::WorldVisibilityResolveInput input{
            &package_->spatial_package(),package_->world_surfaces(),visibility_brushes_,
            *final_camera,extent,world_visibility::WorldVisibilityMode::pvs_and_frustum,
            world_visibility::WorldPvsFallbackPolicy::frustum_only,next_revision_,
            package_->visibility_scene_identity(),false};
        auto resolved=world_visibility::WorldVisibilityResolver{}.resolve(input,limits_);
        if(!resolved || !resolved.visibility)return fail(RuntimeWorldVisibilityStatus::resolve_failed);
        auto visibility=std::make_shared<const world_visibility::WorldVisibilitySet>(
            std::move(*resolved.visibility));
        const world_visibility::WorldVisibleDrawListBuildInput draw_input{
            visibility.get(),package_->world_surfaces(),package_->world_package()->indices().size(),
            package_->world_package()->materials().size(),draw_models_,draw_brushes_};
        auto built=world_visibility::WorldVisibleDrawListBuilder{}.build(draw_input,draw_limits_);
        if(!built || !built.draw_list)return fail(RuntimeWorldVisibilityStatus::draw_list_failed);
        auto draws=std::make_shared<const world_visibility::WorldVisibleDrawList>(std::move(*built.draw_list));
        visibility_=std::move(visibility);draw_list_=std::move(draws);
        camera_=*final_camera;extent_=extent;
        next_revision_=next_revision_==std::numeric_limits<std::uint64_t>::max()?0U:next_revision_+1U;
        statistics_.camera_leaf=visibility_->camera_leaf_index();
        statistics_.pvs_fallback=visibility_->fallback_reason();
        statistics_.selection=visibility_->statistics();statistics_.draw_list=draw_list_->statistics();
        if(statistics_.pvs_fallback!=world_visibility::WorldPvsFallbackReason::none)++statistics_.fallbacks;
        publish(world);statistics_.status=RuntimeWorldVisibilityStatus::selected;return true;
    } catch(const std::bad_alloc&) {
        return fail(RuntimeWorldVisibilityStatus::allocation_failed);
    } catch(const std::length_error&) {
        return fail(RuntimeWorldVisibilityStatus::allocation_failed);
    }
}

void RuntimeWorldVisibility::reset() noexcept {
    package_.reset();visibility_.reset();draw_list_.reset();camera_.reset();extent_={};
    visibility_brushes_.clear();draw_models_.clear();draw_brushes_.clear();
    next_revision_=1U;statistics_={};
}

const RuntimeWorldVisibilityStatistics& RuntimeWorldVisibility::statistics() const noexcept {
    return statistics_;
}

} // namespace hlclient::tests
