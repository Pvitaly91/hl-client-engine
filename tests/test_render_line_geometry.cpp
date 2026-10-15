#include <hlclient/renderer/render_line_geometry.hpp>
#include <hlclient/entity_render/beam_ribbon.hpp>

// The full renderer-test composition links OpenGL; the shared core/API test
// composition deliberately does not. CPU validation below runs in both.
#if HLCLIENT_HAS_GAME_HALFLIFE
#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>

namespace {
namespace renderer = hlclient::renderer;
namespace game_api = hlclient::game_api;
using V = hlclient::assets::AssetVector3;
using Catch::Approx;

// Independent double-precision measurements of emitted triangles, not a
// second mesh generator or a reference implementation of the production path.
struct Measure3 { double x{}, y{}, z{}; };
Measure3 measure(V p) { return {p.x,p.y,p.z}; }
Measure3 subtract(Measure3 a,Measure3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Measure3 scale(Measure3 a,double s) { return {a.x*s,a.y*s,a.z*s}; }
Measure3 add(Measure3 a,Measure3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
double dot(Measure3 a,Measure3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Measure3 cross(Measure3 a,Measure3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
double length(Measure3 p) { return std::sqrt(dot(p,p)); }
bool finite(V p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }

TEST_CASE("Neutral sine ribbon makes four coherent bounded turns with pinned endpoints",
          "[renderer][geometry][beam-ribbon][core-only]") {
    namespace ribbon=hlclient::entity_render;
    constexpr double pi=3.14159265358979323846;
    CHECK(ribbon::beam_ribbon_segment_count(true)==64U);
    CHECK(ribbon::beam_ribbon_segment_count(false)==32U);
    CHECK(ribbon::beam_ribbon_radius(2,true)==6);
    CHECK(ribbon::beam_ribbon_radius(2,false)==4);
    for(const V direction:std::array<V,7>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},
                                        {0,0,1},{0,0,-1},{.36F,-.48F,.8F}}}) {
        CAPTURE(direction.x,direction.y,direction.z);
        const V a{3,-7,11},b{a.x+direction.x*128,a.y+direction.y*128,a.z+direction.z*128};
        const auto points=ribbon::beam_ribbon_points(a,b,2,true,1.5,7);
        REQUIRE(points.size()==65U);
        CHECK(points.front().x==a.x);CHECK(points.front().y==a.y);CHECK(points.front().z==a.z);
        CHECK(points.back().x==b.x);CHECK(points.back().y==b.y);CHECK(points.back().z==b.z);
        const auto delta=subtract(measure(b),measure(a)),axis=scale(delta,1.0/length(delta));
        Measure3 previous{};double rotation=0;
        for(std::size_t i=1;i+1<points.size();++i) {
            REQUIRE(finite(points[i]));
            const double t=static_cast<double>(i)/64;
            const auto offset=subtract(measure(points[i]),add(measure(a),scale(delta,t)));
            CHECK(std::abs(dot(offset,axis))<2e-5);
            const auto radial=length(offset);
            REQUIRE(radial>0);
            CHECK(radial==Approx(6*std::sin(pi*t)).margin(2e-5));
            CHECK(radial<=6.00002);
            const auto radial_unit=scale(offset,1/radial);
            if(i>1) {
                const auto increment=std::atan2(dot(axis,cross(previous,radial_unit)),dot(previous,radial_unit));
                CHECK(increment==Approx(-pi/8).margin(6e-5));
                rotation+=increment;
            }
            previous=radial_unit;
        }
        // Interior samples span 62 of the full 64 steps: constant quadrature
        // winding distinguishes a helix from an irregular Lissajous wobble.
        CHECK(rotation==Approx(-62*pi/8).margin(2e-4));
    }
}

TEST_CASE("Neutral sine ribbon rotates by absolute time without changing radius or legacy noise samples",
          "[renderer][geometry][beam-ribbon][core-only]") {
    namespace ribbon=hlclient::entity_render;
    constexpr double pi=3.14159265358979323846;
    const auto points=ribbon::beam_ribbon_points({0,0,0},{128,0,0},2,true,1.5,7);
    // An unrelated sample cannot influence a later deterministic reconstruction.
    (void)ribbon::beam_ribbon_points({5,2,9},{-32,9,10},.4F,true,40,31);
    const auto repeated=ribbon::beam_ribbon_points({0,0,0},{128,0,0},2,true,1.5,7);
    const auto quarter=ribbon::beam_ribbon_points({0,0,0},{128,0,0},2,true,1.5+pi/18,7);
    for(std::size_t i=0;i<points.size();++i) {
        CHECK(repeated[i].x==points[i].x);CHECK(repeated[i].y==points[i].y);CHECK(repeated[i].z==points[i].z);
        CHECK(quarter[i].x==points[i].x);
        CHECK(quarter[i].y==Approx(points[i].z).margin(2e-5));
        CHECK(quarter[i].z==Approx(-points[i].y).margin(2e-5));
    }
    const auto noise=ribbon::beam_ribbon_points({0,0,0},{128,0,0},2,false,1.5,7);
    for(unsigned i=0;i<=32;++i) {
        const auto t=static_cast<float>(i)/32.F;
        const float phase=static_cast<float>(1.5*9+(7%997)*.173+i*1.71);
        const auto envelope=std::sin(t*3.14159265358979323846F);
        const float old_y=2*envelope*std::sin(phase),old_z=2*envelope*std::sin(phase*1.37F+1);
        CHECK(noise[i*2].x==128*t);
        CHECK(noise[i*2].y==Approx(i==32 ? 0 : old_y).margin(1e-6));
        CHECK(noise[i*2].z==Approx(i==32 ? 0 : old_z).margin(1e-6));
        CHECK(std::hypot(noise[i*2].y,noise[i*2].z)<=ribbon::beam_ribbon_radius(2,false));
    }
}

TEST_CASE("Neutral ribbon keeps invalid and degenerate inputs finite and zero amplitude straight",
          "[renderer][geometry][beam-ribbon][core-only]") {
    namespace ribbon=hlclient::entity_render;
    for(const float extent:{.01F,1.F,5.F,32.F}) {
        const auto close=ribbon::beam_ribbon_points({0,0,0},{extent,0,0},2,true,1.5,7);
        for(const auto p:close)CHECK(std::hypot(p.y,p.z)<=extent*.125F+1e-6F);
    }
    const auto safe=[](const auto& points) {for(const auto p:points) CHECK(finite(p));};
    const auto nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(const bool sine:{false,true}) {
        const auto straight=ribbon::beam_ribbon_points({3,4,5},{131,4,5},0,sine,2,8);
        for(std::size_t i=0;i<straight.size();++i) {
            CHECK(straight[i].x==3+2*i);CHECK(straight[i].y==4);CHECK(straight[i].z==5);
        }
        safe(ribbon::beam_ribbon_points({3,4,5},{3,4,5},2,sine,2,8));
        safe(ribbon::beam_ribbon_points({0,0,0},{.0001F,0,0},2,sine,2,8));
        for(const auto bad:{nan,inf,-inf,-1.F,std::numeric_limits<float>::max()}) {
            CHECK(ribbon::beam_ribbon_radius(bad,sine)==0);
            safe(ribbon::beam_ribbon_points({3,4,5},{131,4,5},bad,sine,2,8));
        }
        for(const auto bad:{nan,inf,-inf}) {
            safe(ribbon::beam_ribbon_points({bad,0,0},{131,4,5},2,sine,2,8));
            safe(ribbon::beam_ribbon_points({3,4,5},{0,bad,0},2,sine,2,8));
            safe(ribbon::beam_ribbon_points({3,4,5},{131,4,5},2,sine,bad,8));
        }
        safe(ribbon::beam_ribbon_points({-std::numeric_limits<float>::max(),0,0},
            {std::numeric_limits<float>::max(),0,0},2,sine,2,8));
        safe(ribbon::beam_ribbon_points({3,4,5},{131,4,5},2,sine,std::numeric_limits<double>::max(),8));
    }
}

renderer::RenderWorldLine line(V direction={1,0,0},float taper=.35F) {
    renderer::RenderWorldLine result;
    result.start={3,-7,11};
    result.end={result.start.x+direction.x*32,result.start.y+direction.y*32,
                result.start.z+direction.z*32};
    result.width_units=1.30F;
    result.color={1,.82F,.45F,.85F};
    result.head_taper_fraction=taper;
    result.geometry=game_api::WorldLineGeometry::solid_3d;
    return result;
}

TEST_CASE("Solid tracer geometry has a bounded camera-independent diameter rounded front and pointed rear",
          "[renderer][tracer][geometry][e15]") {
    const std::array<V,7> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},
                                    {0,0,1},{0,0,-1},{.36F,-.48F,.8F}}};
    for(const auto direction:directions) {
        CAPTURE(direction.x,direction.y,direction.z);
        const auto input=line(direction);
        const auto mesh=renderer::solid_line_mesh(input);
        REQUIRE(mesh.vertex_count==360U);
        CHECK(mesh.vertex_count<=mesh.vertices.size());
        CHECK(mesh.vertex_count%3U==0U);
        const auto start=measure(input.end); // leading rounded end; measure backwards into the unchanged segment
        const auto delta=subtract(measure(input.start),start);
        const auto extent=length(delta);
        const auto axis=scale(delta,1.0/extent);
        const double radius=static_cast<double>(input.width_units)*.5;
        const double shoulder=extent*(1.0-input.head_taper_fraction);
        std::size_t tail_vertices=0,rounded_vertices=0,shoulder_vertices=0,tip_vertices=0;
        for(std::size_t i=0;i<mesh.vertex_count;++i) {
            const auto& vertex=mesh.vertices[i];
            REQUIRE(finite(vertex.position));
            REQUIRE(finite(vertex.normal));
            CHECK(length(measure(vertex.normal))==Approx(1.0).margin(4e-5));
            const auto relative=subtract(measure(vertex.position),start);
            const auto longitudinal=dot(relative,axis);
            const auto radial=length(subtract(relative,scale(axis,longitudinal)));
            CHECK(longitudinal>=-5e-5);
            CHECK(longitudinal<=extent+5e-5);
            CHECK(radial<=radius+5e-5);
            if(std::abs(longitudinal)<5e-5) {
                ++tail_vertices;
                CHECK(radial<5e-5); // rounded leading pole, not a full-width disc
                CHECK(dot(measure(vertex.normal),axis)==Approx(-1.0).margin(4e-5));
            } else if(longitudinal<=radius+5e-5) {
                ++rounded_vertices;
                const auto from_center=subtract(relative,scale(axis,radius));
                CHECK(length(from_center)==Approx(radius).margin(5e-5));
                CHECK(dot(measure(vertex.normal),scale(from_center,1.0/radius))==Approx(1.0).margin(5e-5));
            } else if(std::abs(longitudinal-shoulder)<5e-5) {
                ++shoulder_vertices;
                CHECK(radial==Approx(radius).margin(5e-5));
            } else {
                ++tip_vertices;
                CHECK(longitudinal==Approx(extent).margin(5e-5));
                CHECK(radial==Approx(0.0).margin(5e-5));
            }
        }
        CHECK(tail_vertices>0);
        CHECK(rounded_vertices>0);
        CHECK(shoulder_vertices>0);
        CHECK(tip_vertices==12U);
        double maximum_tail_diameter=0;
        for(std::size_t i=0;i<mesh.vertex_count;++i) {
            const auto a=measure(mesh.vertices[i].position);
            if(std::abs(dot(subtract(a,start),axis)-radius)>=5e-5) continue;
            for(std::size_t j=i+1U;j<mesh.vertex_count;++j) {
                const auto b=measure(mesh.vertices[j].position);
                if(std::abs(dot(subtract(b,start),axis)-radius)<5e-5)
                    maximum_tail_diameter=std::max(maximum_tail_diameter,length(subtract(a,b)));
            }
        }
        CHECK(maximum_tail_diameter==Approx(input.width_units).margin(5e-5));

        // Every front-facing CCW triangle agrees with its independently
        // measured outward surface normal, including the rounded tail and cone.
        for(std::size_t i=0;i<mesh.vertex_count;i+=3U) {
            const auto& a=mesh.vertices[i];
            const auto& b=mesh.vertices[i+1U];
            const auto& c=mesh.vertices[i+2U];
            const auto geometric=cross(subtract(measure(b.position),measure(a.position)),
                                       subtract(measure(c.position),measure(a.position)));
            const auto average=scale(add(add(measure(a.normal),measure(b.normal)),measure(c.normal)),1.0/3.0);
            REQUIRE(length(geometric)>1e-5);
            CHECK(dot(geometric,average)>1e-6);
            const auto center=scale(add(add(measure(a.position),measure(b.position)),measure(c.position)),1.0/3.0);
            const auto relative=subtract(center,start);
            const auto longitudinal=dot(relative,axis);
            const auto outward=subtract(relative,scale(axis,longitudinal));
            CHECK(dot(geometric,outward)>0);
            if(longitudinal<radius-5e-5) CHECK(dot(geometric,axis)<0);
            else if(longitudinal>shoulder+5e-5) CHECK(dot(geometric,axis)>0);
        }

        // Repeated scene assembly cannot make a camera-facing alternative or
        // accumulate stale geometry: all owning output coordinates are stable.
        const auto repeated=renderer::solid_line_mesh(input);
        REQUIRE(repeated.vertex_count==mesh.vertex_count);
        for(std::size_t i=0;i<mesh.vertex_count;++i) {
            CHECK(repeated.vertices[i].position.x==mesh.vertices[i].position.x);
            CHECK(repeated.vertices[i].position.y==mesh.vertices[i].position.y);
            CHECK(repeated.vertices[i].position.z==mesh.vertices[i].position.z);
            CHECK(repeated.vertices[i].normal.x==mesh.vertices[i].normal.x);
            CHECK(repeated.vertices[i].normal.y==mesh.vertices[i].normal.y);
            CHECK(repeated.vertices[i].normal.z==mesh.vertices[i].normal.z);
        }
    }
}

TEST_CASE("Solid tracer orientation puts the rounded pole ahead and cone tip behind without changing the owning line",
          "[renderer][tracer][geometry][tracer-reversed][e15]") {
    for(const V direction:std::array<V,7>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{.36F,-.48F,.8F}}}) {
        const auto input=line(direction);const auto before=input;
        const auto travel=subtract(measure(input.end),measure(input.start));const auto axis=scale(travel,1.0/length(travel));
        const auto mesh=renderer::solid_line_mesh(input);REQUIRE(mesh.vertex_count==360U);
        std::size_t rear_tip=0,rounded_front=0;
        for(std::size_t i=0;i<mesh.vertex_count;++i) {
            const auto& vertex=mesh.vertices[i];
            if(length(subtract(measure(vertex.position),measure(input.start)))<5e-5) {
                ++rear_tip;CHECK(dot(measure(vertex.normal),axis)<0); // cone points backwards relative to travel
            }
            if(length(subtract(measure(vertex.position),measure(input.end)))<5e-5) {
                ++rounded_front;CHECK(dot(measure(vertex.normal),axis)==Approx(1.0).margin(5e-5));
            }
        }
        CHECK(rear_tip==12U);CHECK(rounded_front==12U);
        CHECK(input.start.x==before.start.x);CHECK(input.start.y==before.start.y);CHECK(input.start.z==before.start.z);
        CHECK(input.end.x==before.end.x);CHECK(input.end.y==before.end.y);CHECK(input.end.z==before.end.z);
        CHECK(input.width_units==before.width_units);CHECK(input.color==before.color);
        CHECK(input.head_taper_fraction==before.head_taper_fraction);CHECK(input.geometry==before.geometry);
    }
}

TEST_CASE("Solid 48-unit tracer begins its rear taper ten percent behind the rounded leading end",
          "[renderer][tracer][geometry][tracer-long-tail][e15]") {
    for(const float width:{.65F,1.30F}) {
        CAPTURE(width);
        auto input=line({1,0,0},.90F);input.start={0,0,0};input.end={48,0,0};input.width_units=width;
        const auto mesh=renderer::solid_line_mesh(input);REQUIRE(mesh.vertex_count==360U);
        const double radius=width*.5,shoulder=4.8;
        std::size_t shoulder_vertices=0,rear_tip=0,front_pole=0,cone_faces=0;
        for(std::size_t i=0;i<mesh.vertex_count;++i) {
            const auto& v=mesh.vertices[i];
            const double from_front=48.0-v.position.x;
            const double radial=std::hypot(v.position.y,v.position.z);
            CHECK(from_front>=-5e-5);CHECK(from_front<=48.0+5e-5);
            CHECK(radial<=radius+5e-5);
            CHECK(length(measure(v.normal))==Approx(1.0).margin(4e-5));
            if(std::abs(from_front-shoulder)<5e-5) {
                ++shoulder_vertices;CHECK(radial==Approx(radius).margin(5e-5));
            } else if(from_front>shoulder+5e-5) {
                ++rear_tip;CHECK(from_front==Approx(48.0).margin(5e-5));
                CHECK(radial==Approx(0.0).margin(5e-5));CHECK(v.normal.x<0);
            } else if(from_front<5e-5) {
                ++front_pole;CHECK(radial<5e-5);CHECK(v.normal.x==Approx(1.0).margin(5e-5));
            } else if(from_front>radius+5e-5) {
                CHECK(radial==Approx(radius).margin(5e-5)); // no premature taper in the 10% body
            }
        }
        // Five shoulder vertices per wedge: three in the body, two in the cone.
        CHECK(shoulder_vertices==60U);CHECK(rear_tip==12U);CHECK(front_pole==12U);
        for(std::size_t i=0;i<mesh.vertex_count;i+=3U) {
            const auto& a=mesh.vertices[i];const auto& b=mesh.vertices[i+1U];const auto& c=mesh.vertices[i+2U];
            const auto geometric=cross(subtract(measure(b.position),measure(a.position)),
                subtract(measure(c.position),measure(a.position)));
            REQUIRE(length(geometric)>1e-6);
            CHECK(dot(geometric,add(add(measure(a.normal),measure(b.normal)),measure(c.normal)))>0);
            const double center_from_front=48.0-(a.position.x+b.position.x+c.position.x)/3.0;
            if(center_from_front>shoulder+5e-5) {
                ++cone_faces;
                // Slice an actual emitted cone face, independently of the generator:
                // radius is still full at 10%, and linearly shrinks thereafter.
                for(const double fraction:{.15,.55,.90}) {
                    const double along=fraction*48.0;
                    const auto p=scale(add(measure(a.position),measure(b.position)),.5);
                    const auto slice=add(scale(p,(48.0-along)/(48.0-shoulder)),
                        scale(measure(c.position),(along-shoulder)/(48.0-shoulder)));
                    CHECK(48.0-slice.x==Approx(along).margin(5e-5));
                    const double expected=radius*std::cos(3.14159265358979323846/12.0)*
                        (1.0-fraction)/.90;
                    CHECK(std::hypot(slice.y,slice.z)==Approx(expected).margin(5e-5));
                }
            }
        }
        CHECK(cone_faces==12U);
    }
}

TEST_CASE("Solid tracer zero and full taper are front-rounded cylinder and rear-pointing cone without degenerate triangles",
          "[renderer][tracer][geometry][e15]") {
    for(const auto taper:std::array<float,2>{0,1}) {
        CAPTURE(taper);
        const auto input=line({1,0,0},taper);
        const auto mesh=renderer::solid_line_mesh(input);
        REQUIRE(mesh.vertex_count==(taper==0 ? 360U : 72U));
        std::size_t tail_caps=0,rounded_faces=0,front_caps=0,cone_faces=0,body_faces=0;
        for(std::size_t i=0;i<mesh.vertex_count;i+=3U) {
            const auto& a=mesh.vertices[i];
            const auto& b=mesh.vertices[i+1U];
            const auto& c=mesh.vertices[i+2U];
            const auto geometric=cross(subtract(measure(b.position),measure(a.position)),
                                       subtract(measure(c.position),measure(a.position)));
            const auto average=scale(add(add(measure(a.normal),measure(b.normal)),measure(c.normal)),1.0/3.0);
            REQUIRE(length(geometric)>1e-5);
            CHECK(dot(geometric,average)>1e-6);
            for(const auto* vertex:std::array<const renderer::RenderLineVertex*,3>{&a,&b,&c}) {
                CHECK(finite(vertex->position));
                CHECK(finite(vertex->normal));
                CHECK(length(measure(vertex->normal))==Approx(1.0).margin(4e-5));
            }
            if(a.position.x==input.end.x && b.position.x==input.end.x && c.position.x==input.end.x) {
                ++tail_caps;
                CHECK(geometric.x>0);
            } else if(a.position.x==input.start.x && b.position.x==input.start.x && c.position.x==input.start.x) {
                ++front_caps;
                CHECK(geometric.x<0);
            } else if(taper==1) {
                ++cone_faces;
                CHECK(geometric.x<0);
            } else if((a.position.x+b.position.x+c.position.x)/3.0F>input.end.x-input.width_units*.5F) {
                ++rounded_faces;
                CHECK(geometric.x>0);
            } else {
                ++body_faces;
                CHECK(geometric.x==Approx(0.0).margin(1e-6));
            }
        }
        CHECK(tail_caps==(taper==0 ? 0U : 12U));
        CHECK(rounded_faces==(taper==0 ? 84U : 0U));
        CHECK(front_caps==(taper==0 ? 12U : 0U));
        CHECK(cone_faces==(taper==1 ? 12U : 0U));
        CHECK(body_faces==(taper==0 ? 24U : 0U));
    }
}

TEST_CASE("Solid narrowed rear ends at twenty percent diameter with a closed rounded cap inside its original bounds",
          "[renderer][tracer][geometry][tracer-round-rear][e15]") {
    for(const float extent:{48.0F,.01F}) for(const float width:{.65F,1.30F}) {
        CAPTURE(extent,width);
        auto input=line({1,0,0},.90F);input.start={0,0,0};input.end={extent,0,0};input.width_units=width;
        input.taper_end_diameter_fraction=.20F;
        const auto mesh=renderer::solid_line_mesh(input);REQUIRE(mesh.vertex_count==648U);
        CHECK(mesh.vertex_count<=mesh.vertices.size());
        const double radius=width*.5,rear_radius=radius*.2;
        const double shoulder=extent*(1.0-input.head_taper_fraction);
        const double cap_length=std::min(rear_radius,extent*input.head_taper_fraction*.5);
        const double equator=extent-cap_length;
        std::size_t rear_poles=0,rounded=0,equator_vertices=0,frustum_faces=0;
        using Point=std::array<long long,3>;
        std::map<std::array<Point,2>,std::size_t> edges;
        const auto key=[](V p) {return Point{std::llround(p.x*100000.0),std::llround(p.y*100000.0),std::llround(p.z*100000.0)};};
        for(std::size_t i=0;i<mesh.vertex_count;++i) {
            const auto& v=mesh.vertices[i];const double along=extent-v.position.x;
            const double radial=std::hypot(v.position.y,v.position.z);
            CHECK(finite(v.position));CHECK(finite(v.normal));
            CHECK(length(measure(v.normal))==Approx(1.0).margin(5e-5));
            CHECK(along>=-5e-6);CHECK(along<=extent+5e-6);CHECK(radial<=radius+5e-6);
            if(std::abs(along-equator)<5e-6) {
                ++equator_vertices;CHECK(radial*2==Approx(width*.2).margin(5e-6));
            }
            if(along>equator+5e-6) {
                ++rounded;const double back=extent-along;
                const double ellipse=(back-cap_length)*(back-cap_length)/(cap_length*cap_length)+
                    radial*radial/(rear_radius*rear_radius);
                CHECK(ellipse==Approx(1.0).margin(.001));CHECK(v.normal.x<0);
            }
            if(along>extent-5e-6) {
                ++rear_poles;CHECK(radial<5e-6);CHECK(v.normal.x==Approx(-1.0).margin(5e-5));
            }
        }
        CHECK(rear_poles==12U);CHECK(rounded>12U);CHECK(equator_vertices==72U);
        for(std::size_t i=0;i<mesh.vertex_count;i+=3U) {
            const auto& a=mesh.vertices[i];const auto& b=mesh.vertices[i+1U];const auto& c=mesh.vertices[i+2U];
            const auto geometric=cross(subtract(measure(b.position),measure(a.position)),subtract(measure(c.position),measure(a.position)));
            REQUIRE(length(geometric)>1e-10);
            CHECK(dot(geometric,add(add(measure(a.normal),measure(b.normal)),measure(c.normal)))>0);
            CHECK_FALSE((a.position.x==0 && b.position.x==0 && c.position.x==0)); // no flat rear disc
            for(std::size_t j=0;j<3;++j) {
                auto p=key(mesh.vertices[i+j].position),q=key(mesh.vertices[i+(j+1U)%3U].position);
                REQUIRE(p!=q);if(q<p) std::swap(p,q);++edges[{p,q}];
            }
            const double center=extent-(a.position.x+b.position.x+c.position.x)/3.0;
            if(center>shoulder+5e-6 && center<equator-5e-6) {
                ++frustum_faces;
                for(const auto* v:{&a,&b,&c}) {
                    const double along=extent-v->position.x;
                    const double expected=radius+(rear_radius-radius)*(along-shoulder)/(equator-shoulder);
                    CHECK(std::hypot(v->position.y,v->position.z)==Approx(expected).margin(5e-6));
                }
            }
        }
        CHECK(frustum_faces==24U);
        for(const auto& edge:edges) CHECK(edge.second==2U);
        CHECK(input.start.x==0);CHECK(input.end.x==extent);CHECK(input.width_units==width);
    }
}

TEST_CASE("Solid tracer mesh rejects invalid input and keeps omitted geometry flat",
          "[renderer][tracer][geometry][e15]") {
    const auto valid=line();
    REQUIRE(renderer::solid_line_mesh(valid).vertex_count==360U);
    const auto rejected=[](const auto& input) {CHECK(renderer::solid_line_mesh(input).vertex_count==0U);};
    auto input=valid;
    input.geometry=game_api::WorldLineGeometry::flat_billboard;rejected(input);
    input.geometry=static_cast<game_api::WorldLineGeometry>(std::uint8_t{255});rejected(input);
    renderer::RenderWorldLine legacy{{0,0,0},{32,0,0},1.30F,{1,.82F,.45F,.85F},.35F};
    CHECK(legacy.geometry==game_api::WorldLineGeometry::flat_billboard);
    rejected(legacy);

    const auto nan=std::numeric_limits<float>::quiet_NaN();
    const auto infinity=std::numeric_limits<float>::infinity();
    for(const auto invalid:std::array<float,6>{0,-1,6.01F,nan,infinity,-infinity}) {
        input=valid;input.width_units=invalid;rejected(input);
    }
    for(const auto invalid:std::array<float,5>{-.01F,1.01F,nan,infinity,-infinity}) {
        input=valid;input.head_taper_fraction=invalid;rejected(input);
        input=valid;input.taper_end_diameter_fraction=invalid;rejected(input);
    }
    for(const auto invalid:std::array<float,5>{-.01F,1.01F,nan,infinity,-infinity}) {
        for(std::size_t channel=0;channel<valid.color.size();++channel) {
            input=valid;input.color[channel]=invalid;rejected(input);
        }
    }
    for(const auto invalid:std::array<float,3>{nan,infinity,-infinity}) {
        for(std::size_t component=0;component<3U;++component) {
            input=valid;
            if(component==0) input.start.x=invalid;
            else if(component==1) input.start.y=invalid;
            else input.start.z=invalid;
            rejected(input);
            input=valid;
            if(component==0) input.end.x=invalid;
            else if(component==1) input.end.y=invalid;
            else input.end.z=invalid;
            rejected(input);
        }
    }
    input=valid;input.end=input.start;rejected(input);
    input=valid;input.start={0,0,0};input.end={.001F,0,0};rejected(input);
    input=valid;input.start={0,0,0};input.end={10001,0,0};rejected(input);
    input=valid;
    input.start={-std::numeric_limits<float>::max(),0,0};
    input.end={std::numeric_limits<float>::max(),0,0};rejected(input);

    // Documented inclusive maximums are valid, rather than an accidental
    // reject-everything implementation satisfying only negative cases.
    input=valid;input.width_units=6;input.start={0,0,0};input.end={10000,0,0};
    CHECK(renderer::solid_line_mesh(input).vertex_count==360U);
}

TEST_CASE("Neutral solid-line emission gain has inclusive finite bounds without changing geometry",
          "[renderer][tracer][geometry][e21][gauss-beam-gain]") {
    auto input=line({1,0,0},.90F);
    input.taper_end_diameter_fraction=.20F;
    CHECK(input.emission_gain==1.0F); // every existing MP5 aggregate/default keeps the old intensity
    const auto baseline=renderer::solid_line_mesh(input);
    REQUIRE(baseline.vertex_count==648U);
    for(const float gain:{0.0F,1.0F,3.0F,4.0F}) {
        CAPTURE(gain);input.emission_gain=gain;
        const auto mesh=renderer::solid_line_mesh(input);
        REQUIRE(mesh.vertex_count==baseline.vertex_count);
        for(std::size_t i=0;i<mesh.vertex_count;++i) {
            CHECK(mesh.vertices[i].position.x==baseline.vertices[i].position.x);
            CHECK(mesh.vertices[i].position.y==baseline.vertices[i].position.y);
            CHECK(mesh.vertices[i].position.z==baseline.vertices[i].position.z);
            CHECK(mesh.vertices[i].normal.x==baseline.vertices[i].normal.x);
            CHECK(mesh.vertices[i].normal.y==baseline.vertices[i].normal.y);
            CHECK(mesh.vertices[i].normal.z==baseline.vertices[i].normal.z);
        }
    }
    for(const float gain:{-.01F,4.01F,std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
        CAPTURE(gain);input.emission_gain=gain;
        CHECK(renderer::solid_line_mesh(input).vertex_count==0U);
    }
}

#if HLCLIENT_HAS_GAME_HALFLIFE
TEST_CASE("Production GL solid-line emission distinguishes bright yellow and white without changing MP5 depth or later passes",
          "[renderer][tracer][e21][gauss-beam-gain][opengl][actual-context]") {
    namespace fixture=hlclient::tests::entity_opengl_fixture;
    namespace world_fixture=hlclient::tests::world_render_fixture;
    auto context=fixture::try_context(320,240);
    if(!context || !fixture::capable_context()) SKIP("OpenGL 3.3 Core context unavailable");
    context->initialize_renderer();
    renderer::RenderScene scene;
    scene.clear_color={0,0,0,1};
    scene.camera.position={25,8,8};scene.camera.target={0,8,8};
    const auto capture=[&] {
        context->renderer().render(scene,{320,240});
        auto pixels=context->renderer().observe_framebuffer({320,240},scene.clear_color,true);
        REQUIRE(pixels);return pixels;
    };
    const auto channels=[](const auto& pixels) {
        std::array<std::uint64_t,3> sum{};
        for(std::size_t i=0;i<pixels.rgba8.size();i+=4)
            for(std::size_t c=0;c<sum.size();++c)
                sum[c]+=pixels.rgba8[i+c];
        return sum;
    };
    const auto baseline=capture();
    auto beam=line({0,1,0},.90F);
    beam.start={5,4,8};beam.end={5,12,8};beam.width_units=2;
    beam.taper_end_diameter_fraction=.20F;
    // A deliberately low tint isolates real GPU gain, not framebuffer clipping
    // or a brighter color/flash/light making an unrelated request look correct.
    beam.color={.12F,.08F,.03F,.75F};
    scene.world_lines[0]=beam;scene.world_line_count=1;
    const auto implicit_one=capture();
    scene.world_lines[0].emission_gain=1;
    CHECK(capture().rgba8==implicit_one.rgba8);
    scene.world_lines[0].emission_gain=3;
    const auto bright=capture();
    const auto ordinary_signal=channels(implicit_one),bright_signal=channels(bright);
    for(std::size_t c=0;c<ordinary_signal.size();++c) {
        CAPTURE(c,ordinary_signal[c],bright_signal[c]);
        REQUIRE(ordinary_signal[c]>100U);
        CHECK(bright_signal[c]>ordinary_signal[c]*2.7);
        CHECK(bright_signal[c]<ordinary_signal[c]*3.3);
    }
    std::size_t ordinary_coverage=0,bright_coverage=0;
    bool alpha_unchanged=true;
    for(std::size_t i=0;i<baseline.rgba8.size();i+=4) {
        if(!std::equal(implicit_one.rgba8.begin()+i,implicit_one.rgba8.begin()+i+3,baseline.rgba8.begin()+i)) ++ordinary_coverage;
        if(!std::equal(bright.rgba8.begin()+i,bright.rgba8.begin()+i+3,baseline.rgba8.begin()+i)) ++bright_coverage;
        alpha_unchanged=alpha_unchanged && bright.rgba8[i+3]==implicit_one.rgba8[i+3];
    }
    REQUIRE(ordinary_coverage>30U);CHECK(bright_coverage==ordinary_coverage);
    CHECK(alpha_unchanged); // RGB-only gain preserves alpha
    scene.world_lines[0].color={1,1,0,1};const auto yellow=channels(capture());
    scene.world_lines[0].color={1,1,1,1};const auto white=channels(capture());
    REQUIRE(yellow[0]>1000U);CHECK(yellow[0]==yellow[1]);CHECK(yellow[2]==0U);
    CHECK(white[0]==yellow[0]);CHECK(white[1]==yellow[1]);CHECK(white[2]==white[0]);
    // Invalid gains must be rejected at actual drawing, not only by the mesh
    // helper. Test both branches so the legacy flat path cannot bypass bounds.
    for(const auto geometry:{game_api::WorldLineGeometry::solid_3d,
                             game_api::WorldLineGeometry::flat_billboard}) {
        scene.world_lines[0].geometry=geometry;
        for(const float gain:{-.01F,4.01F,std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()}) {
            CAPTURE(geometry,gain);scene.world_lines[0].emission_gain=gain;
            CHECK(capture().rgba8==baseline.rgba8);
        }
    }
    scene.world_line_count=0;CHECK(capture().rgba8==baseline.rgba8);

    // Use the existing project-owned world fixture for an actual depth-buffer
    // wall. Bright emission does not allow a volume behind it to shine through.
    auto wall=world_fixture::make_world();
    for(auto& vertex:wall.vertices) {
        const auto p=vertex.position;vertex.position={0,p.x,p.y};vertex.normal={1,0,0};
    }
    wall.bounds={{0,0,0},{0,16,16}};wall.surfaces[0].bounds=wall.bounds;
    auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
        {std::move(wall),world_fixture::make_texture_set(false)},world_fixture::make_lightmap_set({}));
    REQUIRE(built);
    scene.static_world=renderer::RenderStaticWorld{
        std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package))};
    scene.basic_hud=renderer::RenderBasicHud{
        {{8,8,48,24,{.10F,.25F,.40F,.8F}}},{{"GAIN",12,12,1,6,8,{.25F,.50F,.75F,1}}}};
    // Record another existing pass before a gain-three draw. Both its own pixels
    // and the subsequent frame must retain the original gain/state.
    auto flat=beam;flat.geometry=game_api::WorldLineGeometry::flat_billboard;
    flat.start.z=flat.end.z=11;flat.color={.05F,.08F,.12F,.75F};
    scene.world_lines[0]=flat;scene.world_line_count=1;
    const auto afterpass_baseline=capture();
    scene.world_lines[1]=beam;scene.world_lines[1].emission_gain=3;
    scene.world_lines[1].start.x=scene.world_lines[1].end.x=-3;scene.world_line_count=2;
    CHECK(capture().rgba8==afterpass_baseline.rgba8); // full volume depth-rejected
    scene.world_lines[1].start.x=scene.world_lines[1].end.x=5;
    const auto together=capture();CHECK(together.rgba8!=afterpass_baseline.rgba8);
    // Top-left HUD's framebuffer rows are bottom-origin after glReadPixels.
    for(std::size_t y=208;y<232;++y) for(std::size_t x=8;x<56;++x) {
        const auto p=(y*320+x)*4;
        for(std::size_t c=0;c<4;++c) CHECK(together.rgba8[p+c]==afterpass_baseline.rgba8[p+c]);
    }
    scene.world_line_count=1;CHECK(capture().rgba8==afterpass_baseline.rgba8);
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}
#endif

TEST_CASE("Solid tracer rounded front has no flat leading cut and is watertight even when clipped short",
          "[renderer][tracer][geometry][tracer-round-tail][e15]") {
    for(const float extent:{32.0F,.01F}) for(const float width:{.65F,1.30F,1.75F,5.5F,6.0F}) {
        CAPTURE(extent,width);
        auto input=line();input.width_units=width;input.end={input.start.x+extent,input.start.y,input.start.z};
        const auto mesh=renderer::solid_line_mesh(input);REQUIRE(mesh.vertex_count==360U);
        const double actual_extent=input.end.x-input.start.x;
        const double radius=width*.5,cap_length=std::min(radius,actual_extent*(1-input.head_taper_fraction)*.5);
        const auto start=measure(input.end);std::size_t curved=0;
        using Point=std::array<long long,3>;
        std::map<std::array<Point,2>,std::size_t> edges;
        const auto key=[](V p) {return Point{std::llround(p.x*100000.0),std::llround(p.y*100000.0),std::llround(p.z*100000.0)};};
        for(std::size_t i=0;i<mesh.vertex_count;i+=3U) {
            const auto& a=mesh.vertices[i];const auto& b=mesh.vertices[i+1U];const auto& c=mesh.vertices[i+2U];
            const auto geometric=cross(subtract(measure(b.position),measure(a.position)),subtract(measure(c.position),measure(a.position)));
            REQUIRE(length(geometric)>1e-9);
            const auto average=add(add(measure(a.normal),measure(b.normal)),measure(c.normal));
            CHECK(dot(geometric,average)>0);
            CHECK_FALSE((a.position.x==input.end.x && b.position.x==input.end.x && c.position.x==input.end.x));
            for(std::size_t j=0;j<3;++j) {
                auto p=key(mesh.vertices[i+j].position),q=key(mesh.vertices[i+(j+1U)%3U].position);
                REQUIRE(p!=q);if(q<p) std::swap(p,q);++edges[{p,q}];
                const auto& vertex=mesh.vertices[i+j];const auto relative=subtract(start,measure(vertex.position));
                CHECK(relative.x>=-1e-6);CHECK(relative.x<=actual_extent+1e-6);
                CHECK(length(measure(vertex.normal))==Approx(1.0).margin(4e-5));
                if(relative.x<cap_length-1e-6) {
                    ++curved;
                    const double ellipse=(relative.x-cap_length)*(relative.x-cap_length)/(cap_length*cap_length)+
                        (relative.y*relative.y+relative.z*relative.z)/(radius*radius);
                    CHECK(ellipse==Approx(1.0).margin(.001));
                    CHECK(vertex.normal.x>0);
                }
            }
        }
        CHECK(curved>12); // more than a duplicated tail pole, actual curved rings
        for(const auto& edge:edges) CHECK(edge.second==2U); // closed surfaces / welded pole, equator, shoulder
    }
}
} // namespace
