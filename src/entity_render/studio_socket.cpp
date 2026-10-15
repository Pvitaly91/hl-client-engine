#include <hlclient/entity_render/studio_socket.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::entity_render {
namespace {
using V=assets::AssetVector3;
bool finite(V v) noexcept {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
}
StudioSocketResult evaluate_studio_socket(const goldsrc::studio::StudioPoseModelIdentity& id,
    const assets::SkeletalModelAssetData& model,const goldsrc::studio::StudioPoseState& pose,
    const assets::ModelSocketDefinition& definition,const EntityRenderTransform& transform) noexcept {
    if(id!=pose.model_identity()) return {{},StudioSocketError::identity};
    if(pose.world_bones().size()!=model.bones.size() || definition.bone_index>=model.bones.size())
        return {{},StudioSocketError::bone};
    if(!finite_entity_render_transform(transform)) return {{},StudioSocketError::invalid_transform};
    if(!finite(definition.local_origin)||!finite(definition.forward)||!finite(definition.right)||!finite(definition.up))
        return {{},StudioSocketError::non_finite};
    const auto& bone=pose.world_bones()[definition.bone_index].transform.values;
    const auto matrix=entity_render_model_matrix(transform);
    if(!std::all_of(bone.begin(),bone.end(),[](float v){return std::isfinite(v);}) ||
        !std::all_of(matrix.begin(),matrix.end(),[](float v){return std::isfinite(v);}))
        return {{},StudioSocketError::non_finite};
    const auto convert=[&](V p,bool direction) {
        const float w=direction ? 0.0F : 1.0F;
        const V local{bone[0]*p.x+bone[1]*p.y+bone[2]*p.z+bone[3]*w,
            bone[4]*p.x+bone[5]*p.y+bone[6]*p.z+bone[7]*w,
            bone[8]*p.x+bone[9]*p.y+bone[10]*p.z+bone[11]*w};
        return V{matrix[0]*local.x+matrix[4]*local.y+matrix[8]*local.z+matrix[12]*w,
            matrix[1]*local.x+matrix[5]*local.y+matrix[9]*local.z+matrix[13]*w,
            matrix[2]*local.x+matrix[6]*local.y+matrix[10]*local.z+matrix[14]*w};
    };
    assets::EvaluatedModelSocket result{convert(definition.local_origin,false),
        convert(definition.forward,true),convert(definition.right,true),convert(definition.up,true)};
    if(!finite(result.origin)) return {{},StudioSocketError::non_finite};
    for(auto* axis:{&result.forward,&result.right,&result.up}) {
        const float length=std::sqrt(axis->x*axis->x+axis->y*axis->y+axis->z*axis->z);
        if(!finite(*axis)||!std::isfinite(length)||length<0.0001F) return {{},StudioSocketError::invalid_basis};
        axis->x/=length;axis->y/=length;axis->z/=length;
    }
    const auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    if(std::abs(dot(result.forward,result.right))>0.01F || std::abs(dot(result.forward,result.up))>0.01F ||
        std::abs(dot(result.right,result.up))>0.01F) return {{},StudioSocketError::invalid_basis};
    return {result,{}};
}
StudioSocketResult evaluate_studio_attachment(const goldsrc::studio::StudioPoseModelIdentity& id,
    const assets::SkeletalModelAssetData& model,const goldsrc::studio::StudioPoseState& pose,
    std::uint32_t index,const EntityRenderTransform& transform) noexcept {
    if(index>=model.attachments.size()) return {{},StudioSocketError::attachment};
    const auto& a=model.attachments[index];
    // Stock attachments may retain zero basis vectors. Position is valid;
    // use the owning bone's axes instead of fabricating orientation from zero.
    assets::ModelSocketDefinition definition{a.bone_index,a.local_origin};
    const auto usable=[](V v){return finite(v) && v.x*v.x+v.y*v.y+v.z*v.z>0.0001F;};
    if(usable(a.local_vectors[0])&&usable(a.local_vectors[1])&&usable(a.local_vectors[2])) {
        definition.forward=a.local_vectors[0];definition.right=a.local_vectors[1];definition.up=a.local_vectors[2];
    } else if(!finite(a.local_vectors[0])||!finite(a.local_vectors[1])||!finite(a.local_vectors[2]))
        return {{},StudioSocketError::non_finite};
    else if(usable(a.local_vectors[0])||usable(a.local_vectors[1])||usable(a.local_vectors[2]))
        return {{},StudioSocketError::invalid_basis}; // partial basis is not a zero-basis stock attachment
    return evaluate_studio_socket(id,model,pose,definition,transform);
}
}
