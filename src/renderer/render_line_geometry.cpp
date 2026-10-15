#include <hlclient/renderer/render_line_geometry.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace hlclient::renderer {
namespace {
using V=assets::AssetVector3;
V add(V a,V b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
V mul(V a,float s) noexcept {return {a.x*s,a.y*s,a.z*s};}
}
RenderLineMesh solid_line_mesh(const RenderWorldLine& line) noexcept {
    RenderLineMesh mesh;
    if(line.geometry!=game_api::WorldLineGeometry::solid_3d ||
       !is_finite(line.start) || !is_finite(line.end) ||
       !std::isfinite(line.width_units) || line.width_units<=0 || line.width_units>6 ||
       !std::isfinite(line.emission_gain) || line.emission_gain<0 || line.emission_gain>4 ||
       !std::isfinite(line.head_taper_fraction) || line.head_taper_fraction<0 || line.head_taper_fraction>1 ||
       !std::isfinite(line.taper_end_diameter_fraction) || line.taper_end_diameter_fraction<0 || line.taper_end_diameter_fraction>1 ||
       std::any_of(line.color.begin(),line.color.end(),[](float c){return !std::isfinite(c)||c<0||c>1;})) return mesh;
    // Reverse only the private mesh-generation axis: the owning line still
    // flies start -> end, with unchanged endpoints, clipping and front glow.
    // The rounded generator cap becomes the leading end, its cone the rear.
    const auto shape_start=line.end,shape_end=line.start;
    const V delta{shape_end.x-shape_start.x,shape_end.y-shape_start.y,shape_end.z-shape_start.z};
    const float length=std::hypot(delta.x,delta.y,delta.z);
    if(!std::isfinite(length) || length<0.002F || length>10000) return mesh;
    const auto axis=mul(delta,1.0F/length);
    const auto side=normalize(cross(axis,std::abs(axis.z)<0.9F ? V{0,0,1}:V{0,1,0}));
    if(!side) return mesh;
    const auto up=cross(axis,*side);
    const float radius=line.width_units*0.5F;
    const float head_length=length*line.head_taper_fraction;
    const auto shoulder=add(shape_start,mul(axis,length-head_length));
    const float rear_radius=radius*line.taper_end_diameter_fraction;
    const float rear_cap_length=std::min(rear_radius,head_length*0.5F);
    const auto rear_equator=add(shape_end,mul(axis,-rear_cap_length));
    // A hemisphere inside the existing segment. Very short clipped bodies use
    // an axial half-ellipsoid, leaving at least half the body before the head.
    // A full-taper cone has no body to round and retains its explicit flat base.
    const float tail_length=std::min(radius,(length-head_length)*0.5F);
    const auto tail_equator=add(shape_start,mul(axis,tail_length));
    const auto radial=[&](std::size_t i) {
        const float angle=2.0F*std::numbers::pi_v<float>*static_cast<float>(i%12U)/12.0F;
        return add(mul(*side,std::cos(angle)),mul(up,std::sin(angle)));
    };
    const auto emit=[&](V p,V n) {mesh.vertices[mesh.vertex_count++]={p,n};};
    for(std::size_t i=0;i<12U;++i) {
        const auto a=radial(i),b=radial(i+1U);
        const auto tail_a=add(tail_equator,mul(a,radius)),tail_b=add(tail_equator,mul(b,radius));
        const auto front_a=add(shoulder,mul(a,radius)),front_b=add(shoulder,mul(b,radius));
        if(line.head_taper_fraction<1) {
            emit(tail_a,a);emit(tail_b,b);emit(front_b,b);
            emit(tail_a,a);emit(front_b,b);emit(front_a,a);
        }
        if(tail_length>0) {
            const auto ring=[&](V direction,std::size_t latitude) {
                const float theta=static_cast<float>(latitude)*std::numbers::pi_v<float>/8.0F;
                const float sine=latitude==4U ? 1.0F : std::sin(theta);
                const float cosine=latitude==4U ? 0.0F : std::cos(theta);
                const auto position=add(add(shape_start,mul(axis,tail_length*(1-cosine))),mul(direction,radius*sine));
                // Ellipsoid gradient, smooth at pole and tangent to the body
                // at the equator. Normals are independent of camera and time.
                const auto normal=normalize(add(mul(direction,sine),mul(axis,-(radius/tail_length)*cosine)));
                return RenderLineVertex{position,normal.value_or(mul(axis,-1))};
            };
            auto previous_a=ring(a,1U),previous_b=ring(b,1U);
            emit(shape_start,mul(axis,-1));emit(previous_b.position,previous_b.normal);emit(previous_a.position,previous_a.normal);
            for(std::size_t latitude=2U;latitude<=4U;++latitude) {
                const auto next_a=ring(a,latitude),next_b=ring(b,latitude);
                emit(previous_a.position,previous_a.normal);emit(previous_b.position,previous_b.normal);emit(next_b.position,next_b.normal);
                emit(previous_a.position,previous_a.normal);emit(next_b.position,next_b.normal);emit(next_a.position,next_a.normal);
                previous_a=next_a;previous_b=next_b;
            }
        } else {
            emit(shape_start,mul(axis,-1));emit(tail_b,mul(axis,-1));emit(tail_a,mul(axis,-1));
        }
        if(rear_cap_length>0) {
            const float frustum_length=head_length-rear_cap_length;
            const auto rear_a=add(rear_equator,mul(a,rear_radius)),rear_b=add(rear_equator,mul(b,rear_radius));
            const auto na=normalize(add(mul(a,frustum_length),mul(axis,radius-rear_radius)));
            const auto nb=normalize(add(mul(b,frustum_length),mul(axis,radius-rear_radius)));
            if(!na || !nb) return {};
            emit(front_a,*na);emit(front_b,*nb);emit(rear_b,*nb);
            emit(front_a,*na);emit(rear_b,*nb);emit(rear_a,*na);
            // Narrow hemisphere/half-ellipsoid, entirely inside the existing
            // interval. Its equator, not a cone point, is the selected diameter.
            const auto ring=[&](V direction,std::size_t latitude) {
                const float theta=static_cast<float>(latitude)*std::numbers::pi_v<float>/8.0F;
                const float sine=latitude==4U ? 1.0F : std::sin(theta);
                const float cosine=latitude==4U ? 0.0F : std::cos(theta);
                const auto position=add(add(shape_end,mul(axis,-rear_cap_length*(1-cosine))),mul(direction,rear_radius*sine));
                const auto normal=normalize(add(mul(direction,sine),mul(axis,(rear_radius/rear_cap_length)*cosine)));
                return RenderLineVertex{position,normal.value_or(axis)};
            };
            auto previous_a=ring(a,1U),previous_b=ring(b,1U);
            emit(shape_end,axis);emit(previous_a.position,previous_a.normal);emit(previous_b.position,previous_b.normal);
            for(std::size_t latitude=2U;latitude<=4U;++latitude) {
                const auto next_a=ring(a,latitude),next_b=ring(b,latitude);
                emit(previous_a.position,previous_a.normal);emit(next_a.position,next_a.normal);emit(next_b.position,next_b.normal);
                emit(previous_a.position,previous_a.normal);emit(next_b.position,next_b.normal);emit(previous_b.position,previous_b.normal);
                previous_a=next_a;previous_b=next_b;
            }
        } else if(head_length>0) {
            // Cone normals are tilted forward by radius/head-length, while
            // the tip's per-wedge normal avoids a singular zero-vector normal.
            const auto na=normalize(add(mul(a,head_length),mul(axis,radius)));
            const auto nb=normalize(add(mul(b,head_length),mul(axis,radius)));
            const auto nt=normalize(add(add(mul(a,head_length),mul(b,head_length)),mul(axis,2*radius)));
            if(!na || !nb || !nt) return {};
            emit(front_a,*na);emit(front_b,*nb);emit(shape_end,*nt);
        } else {
            emit(shape_end,axis);emit(front_a,axis);emit(front_b,axis);
        }
    }
    for(std::size_t i=0;i<mesh.vertex_count;++i)
        if(!is_finite(mesh.vertices[i].position) || !is_finite(mesh.vertices[i].normal)) return {};
    return mesh;
}
} // namespace hlclient::renderer
