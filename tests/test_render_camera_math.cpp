#include <hlclient/renderer/render_camera_math.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace {

namespace assets = hlclient::assets;
namespace renderer = hlclient::renderer;

TEST_CASE("Right-handed look-at uses documented column-major OpenGL convention",
          "[renderer][camera][math]")
{
    SECTION("canonical minus-Z camera is identity")
    {
        const auto view = renderer::right_handed_look_at(
            {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F});
        REQUIRE(view);
        REQUIRE(view.matrix);
        CHECK(*view.matrix == renderer::RenderMatrix4{});
    }

    SECTION("camera position transforms to view origin")
    {
        const auto view = renderer::right_handed_look_at(
            {3.0F, -4.0F, 5.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
        REQUIRE(view);
        const auto transformed = renderer::transform(
            *view.matrix, {3.0F, -4.0F, 5.0F, 1.0F});
        CHECK(transformed.x == Catch::Approx(0.0F).margin(1.0e-5F));
        CHECK(transformed.y == Catch::Approx(0.0F).margin(1.0e-5F));
        CHECK(transformed.z == Catch::Approx(0.0F).margin(1.0e-5F));
        CHECK(transformed.w == Catch::Approx(1.0F));
    }

    SECTION("Z-up camera remains finite without an axis swap")
    {
        const auto view = renderer::right_handed_look_at(
            {0.0F, -10.0F, 4.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
        REQUIRE(view);
        CHECK(renderer::is_finite(*view.matrix));
    }
}

TEST_CASE("OpenGL perspective maps canonical near and far planes",
          "[renderer][camera][math]")
{
    const auto projection = renderer::opengl_perspective(
        1.5707963268F, 1.0F, 1.0F, 11.0F);
    REQUIRE(projection);
    const auto near_point = renderer::transform(
        *projection.matrix, {0.0F, 0.0F, -1.0F, 1.0F});
    const auto far_point = renderer::transform(
        *projection.matrix, {0.0F, 0.0F, -11.0F, 1.0F});
    REQUIRE(near_point.w != 0.0F);
    REQUIRE(far_point.w != 0.0F);
    CHECK(near_point.z / near_point.w == Catch::Approx(-1.0F));
    CHECK(far_point.z / far_point.w == Catch::Approx(1.0F));

    const auto wide = renderer::opengl_perspective(
        1.5707963268F, 2.0F, 1.0F, 11.0F);
    REQUIRE(wide);
    CHECK(wide.matrix->values[0U] ==
        Catch::Approx(projection.matrix->values[0U] * 0.5F));
    CHECK(wide.matrix->values[5U] ==
        Catch::Approx(projection.matrix->values[5U]));
}

TEST_CASE("Camera math rejects every degenerate projection prerequisite",
          "[renderer][camera][math]")
{
    const auto zero_forward = renderer::right_handed_look_at(
        {1.0F, 2.0F, 3.0F}, {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 1.0F});
    REQUIRE_FALSE(zero_forward);
    CHECK(zero_forward.error == renderer::RenderCameraMathErrorCode::zero_forward);

    const auto zero_up = renderer::right_handed_look_at(
        {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}, {});
    REQUIRE_FALSE(zero_up);
    CHECK(zero_up.error == renderer::RenderCameraMathErrorCode::zero_up);

    const auto parallel = renderer::right_handed_look_at(
        {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    REQUIRE_FALSE(parallel);
    CHECK(parallel.error ==
        renderer::RenderCameraMathErrorCode::parallel_forward_and_up);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto non_finite = renderer::right_handed_look_at(
        {nan, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    REQUIRE_FALSE(non_finite);
    CHECK(non_finite.error ==
        renderer::RenderCameraMathErrorCode::non_finite_input);

    CHECK_FALSE(renderer::opengl_perspective(0.0F, 1.0F, 0.1F, 10.0F));
    CHECK_FALSE(renderer::opengl_perspective(3.1415926535F, 1.0F, 0.1F, 10.0F));
    CHECK_FALSE(renderer::opengl_perspective(1.0F, 0.0F, 0.1F, 10.0F));
    CHECK_FALSE(renderer::opengl_perspective(1.0F, 1.0F, 0.0F, 10.0F));
    CHECK_FALSE(renderer::opengl_perspective(1.0F, 1.0F, 10.0F, 10.0F));
    CHECK_FALSE(renderer::opengl_perspective(1.0F, 1.0F, 11.0F, 10.0F));
}

TEST_CASE("Camera view-projection is finite and projects its target to center",
          "[renderer][camera][math]")
{
    renderer::RenderCamera camera;
    camera.position = {0.0F, -8.0F, 4.0F};
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 0.0F, 1.0F};
    camera.near_plane = 0.1F;
    camera.far_plane = 100.0F;
    const auto matrix = renderer::camera_view_projection(
        camera, renderer::RenderExtent{1'280, 720});
    REQUIRE(matrix);
    CHECK(renderer::is_finite(*matrix.matrix));
    const auto projected_target = renderer::transform(
        *matrix.matrix, {camera.target.x, camera.target.y, camera.target.z, 1.0F});
    REQUIRE(projected_target.w != 0.0F);
    CHECK(projected_target.x / projected_target.w ==
        Catch::Approx(0.0F).margin(1.0e-5F));
    CHECK(projected_target.y / projected_target.w ==
        Catch::Approx(0.0F).margin(1.0e-5F));

    CHECK_FALSE(renderer::camera_view_projection(camera, {0, 720}));
    CHECK_FALSE(renderer::camera_view_projection(camera, {1'280, 0}));
}

TEST_CASE("Matrix multiplication follows column-vector composition order",
          "[renderer][camera][math]")
{
    renderer::RenderMatrix4 translation;
    translation.values[12U] = 2.0F;
    translation.values[13U] = 3.0F;
    translation.values[14U] = 4.0F;
    const auto composed = renderer::multiply(renderer::RenderMatrix4{}, translation);
    CHECK(composed == translation);
    const auto transformed = renderer::transform(
        composed, {1.0F, 2.0F, 3.0F, 1.0F});
    CHECK(transformed.x == 3.0F);
    CHECK(transformed.y == 5.0F);
    CHECK(transformed.z == 7.0F);
    CHECK(transformed.w == 1.0F);
}

TEST_CASE("First-person camera clips camera-origin geometry without changing the world camera",
          "[renderer][camera][math][first-person][viewmodel-clipping]")
{
    for (const float world_near : {0.05F, 0.1F, 4.0F}) {
        CAPTURE(world_near);
        renderer::RenderCamera world;
        world.position = {23.0F, -77.0F, 90.0F};
        world.target = {24.0F, -76.0F, 89.0F};
        world.near_plane = world_near;
        world.far_plane = 128.0F;
        world.vertical_field_of_view_radians = 1.2F;
        const auto saved = world;
        const auto local = renderer::camera_local_first_person_camera(world);
        CHECK(world == saved);
        CHECK(local.near_plane == 1.0F);
        CHECK(local.far_plane == world.far_plane);
        CHECK(local.vertical_field_of_view_radians == world.vertical_field_of_view_radians);
        CHECK(local.position.x == 0.0F);
        CHECK(local.position.y == 0.0F);
        CHECK(local.position.z == 0.0F);
        CHECK(local.target.x == 1.0F);
        CHECK(local.target.y == 0.0F);
        CHECK(local.target.z == 0.0F);
        const auto matrix = renderer::camera_view_projection(local, {640, 480});
        REQUIRE(matrix);
        const auto near_geometry = renderer::transform(*matrix.matrix, {0.403F, 0, 0, 1});
        CHECK(near_geometry.z < -near_geometry.w);
        const auto ordinary_geometry = renderer::transform(*matrix.matrix, {12, 0, 0, 1});
        CHECK(ordinary_geometry.z >= -ordinary_geometry.w);
        CHECK(ordinary_geometry.z <= ordinary_geometry.w);
    }
    renderer::RenderCamera invalid_local_range;
    invalid_local_range.far_plane = 0.5F;
    CHECK(renderer::is_valid(invalid_local_range));
    CHECK_FALSE(renderer::is_valid(renderer::camera_local_first_person_camera(invalid_local_range)));
}

TEST_CASE("Screen effects transform only a private bounded camera value",
          "[renderer][camera][math][screen-effects][no-stock]")
{
    renderer::RenderCamera camera;
    camera.position={1,2,3};camera.target={2,2,3};camera.up={0,0,1};
    const auto original=camera;
    hlclient::game_api::ScreenEffectsFrame effects;
    CHECK(renderer::valid_render_screen_effects(effects));
    REQUIRE(renderer::apply_screen_effects(camera,effects));
    CHECK(*renderer::apply_screen_effects(camera,effects)==original);
    effects.translation={2,-1,.5F};effects.roll_degrees=10;
    const auto moved=renderer::apply_screen_effects(camera,effects);REQUIRE(moved);
    CHECK(camera==original);
    CHECK(moved->position.x==3);CHECK(moved->position.y==1);CHECK(moved->position.z==3.5F);
    CHECK(moved->target.x-moved->position.x==1);
    CHECK(moved->target.y-moved->position.y==0);
    CHECK(moved->target.z-moved->position.z==0);
    CHECK(moved->up.y==Catch::Approx(-std::sin(10.0*0.017453292519943295)));
    CHECK(moved->up.z==Catch::Approx(std::cos(10.0*0.017453292519943295)));
    CHECK(moved->near_plane==original.near_plane);CHECK(moved->far_plane==original.far_plane);
    CHECK(moved->vertical_field_of_view_radians==original.vertical_field_of_view_radians);
    CHECK(*renderer::apply_screen_effects(camera,effects)==*moved);
    effects={};CHECK(*renderer::apply_screen_effects(camera,effects)==original);
    for(const float bad:{std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),17.0F,-17.0F}) {
        effects={};effects.translation[0]=bad;
        CHECK_FALSE(renderer::valid_render_screen_effects(effects));
        CHECK_FALSE(renderer::apply_screen_effects(camera,effects));
        effects={};effects.roll_degrees=bad;
        CHECK_FALSE(renderer::valid_render_screen_effects(effects));
        CHECK_FALSE(renderer::apply_screen_effects(camera,effects));
    }
    for(const float bad:{std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),1.01F,-.01F}) {
        for(std::size_t channel=0;channel<4U;++channel) {
            effects={};effects.overlay_rgba[channel]=bad;
            CHECK_FALSE(renderer::valid_render_screen_effects(effects));
        }
    }
    effects={};camera.target=camera.position;
    CHECK_FALSE(renderer::apply_screen_effects(camera,effects));
}

} // namespace
