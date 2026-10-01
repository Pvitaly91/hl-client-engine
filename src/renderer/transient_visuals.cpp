#include <hlclient/renderer/transient_visuals.hpp>
#include <algorithm>
#include <cmath>

namespace hlclient::renderer {
namespace {
using V=assets::AssetVector3;
V add(V a,V b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V a,float k) noexcept {return {a.x*k,a.y*k,a.z*k};}
float dot(V a,V b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
bool finite(V a) noexcept {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
}
void TransientVisuals::reset() noexcept {
    entries_.fill(std::nullopt); visible_={}; contacts_={}; stats_={}; world_.reset();
}
void TransientVisuals::set_collision_world(std::shared_ptr<const collision::CollisionWorldPackage> world) noexcept {
    world_=std::move(world);
}
bool TransientVisuals::spawn(const game_api::LocalShellEjection& cue,double now) noexcept {
    if (!std::isfinite(now)||!std::isfinite(cue.starts_at_seconds)||
        !std::isfinite(cue.expires_at_seconds)||!finite(cue.origin)||!finite(cue.velocity)||
        !std::isfinite(cue.yaw_degrees)||cue.expires_at_seconds<=now||
        now-cue.starts_at_seconds>0.25) {++stats_.shells_dropped;return false;}
    for(const auto& item:entries_) if(item && item->action==cue.action) {
        ++stats_.duplicate_spawns; return false;
    }
    const auto slot=std::find_if(entries_.begin(),entries_.end(),[](const auto& item){return !item;});
    if(slot==entries_.end()) {++stats_.shells_dropped;return false;}
    *slot=Entry{{cue.origin,cue.velocity,cue.yaw_degrees,cue.starts_at_seconds,
        cue.expires_at_seconds,false},cue.action,cue.starts_at_seconds};
    ++stats_.shells_created;
    return true;
}
void TransientVisuals::update(double now) noexcept {
    if(!std::isfinite(now)) return;
    std::size_t count=0, contact_count=0;
    contacts_={};
    const collision::CollisionWorldQuery query{world_};
    for(auto& item:entries_) {
        if(!item) continue;
        auto& shell=item->shell;
        if(now>=shell.expires_at_seconds || now<item->simulated_at ||
            now-item->simulated_at>0.25) {item.reset();++stats_.shells_expired;continue;}
        constexpr double step=1.0/120.0;
        const auto steps=std::min(30,static_cast<int>(std::floor((now-item->simulated_at)/step+1e-8)));
        for(int n=0;n<steps && !shell.at_rest;++n) {
            V next=add(shell.position,mul(shell.velocity,static_cast<float>(step)));
            next.z-=static_cast<float>(0.5*800.0*step*step);
            shell.velocity.z-=static_cast<float>(800.0*step);
            if(world_) {
                collision::CollisionTraceRequest request;
                request.start=shell.position;request.end=next;
                const auto trace=query.trace_line(request,scratch_);
                if(!trace) {++stats_.trace_failures;item.reset();break;}
                if(trace.result->start_solid || trace.result->all_solid) {
                    shell.at_rest=true;shell.velocity={};break;
                }
                // A BSP entry exactly at the segment endpoint can report
                // fraction 1 with a valid collision plane. The plane, not
                // fraction < 1, is the trace API's hit predicate.
                if(trace.result->collision_plane) {
                    ++stats_.collision_contacts;
                    const auto normal=trace.result->collision_plane->normal;
                    const auto inward=dot(shell.velocity,normal);
                    const auto contact_at=item->simulated_at+(n+1)*step;
                    // Multiple sweeps against the same plane are one physical
                    // contact, not a new bounce or a new sound opportunity.
                    if(inward<0.0F && (item->last_contact_at<0.0 ||
                        contact_at-item->last_contact_at>=0.045)) {
                        item->last_contact_at=contact_at;
                        ++item->contact_ordinal;
                        if(contact_count<contact_buffer_.size()) {
                            contact_buffer_[contact_count++]={item->action,item->contact_ordinal,
                                trace.result->end_position,-inward,contact_at};
                            ++stats_.contact_events;
                        } else ++stats_.contact_events_dropped;
                    } else ++stats_.contact_repeat_suppressed;
                    shell.position=add(trace.result->end_position,mul(normal,0.125F));
                    shell.velocity=mul(sub(shell.velocity,mul(normal,1.28F*inward)),0.62F);
                    if(dot(shell.velocity,shell.velocity)<144.0F) {shell.velocity={};shell.at_rest=true;}
                    continue;
                }
            }
            shell.position=next;
            if(!finite(shell.position)||!finite(shell.velocity)) {item.reset();++stats_.trace_failures;break;}
        }
        if(!item) continue;
        item->simulated_at+=steps*step;
        buffer_[count++]=shell;
    }
    visible_={buffer_.data(),count};contacts_={contact_buffer_.data(),contact_count};stats_.active=count;
}
TransientVisualStatistics TransientVisuals::statistics() const noexcept {return stats_;}
} // namespace hlclient::renderer
