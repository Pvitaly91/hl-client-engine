#include <hlclient/games/halflife/player_appearance.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/player_model_assets.hpp>
#include <hlclient/goldsrc/runtime_control_decoder.hpp>
#include <hlclient/goldsrc/user_info_update.hpp>
#include "user_info_test_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <array>

namespace {
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace gs=hlclient::goldsrc;
namespace assets=hlclient::assets;
api::PlayerAppearanceFields projected(std::string_view value) {
  const auto bytes=hlclient::test::user_info_fixture::make_message(1U,42U,value);
  const auto decoded=gs::UserInfoUpdateParser{}.parse(bytes);REQUIRE(decoded);
  return decoded.state->player_appearance();
}
}
TEST_CASE("E31 appearance is a strict owning projection not an untrusted asset path","[e31][appearance][userinfo]") {
  const auto fields=projected("\\name\\Alice\\model\\GoRDon\\topcolor\\120\\bottomcolor\\360");
  CHECK(fields.model=="gordon");CHECK(fields.model_advertised);CHECK_FALSE(fields.model_token_rejected);
  CHECK(fields.top_color==120U);CHECK(fields.bottom_color==360U);
  for(const auto token:{"../barney","gordon.mdl","a/b","a:b","a b","a%2fb"}) {
    const auto unsafe=projected(std::string("\\model\\")+token);
    CHECK(unsafe.model_token_rejected);CHECK(unsafe.model.empty());
  }
  for(const auto value:{"-1","361","1.5","01x"," 120","9999"}) {
    const auto invalid=projected(std::string("\\model\\gordon\\topcolor\\")+value);
    CHECK_FALSE(invalid.top_color);CHECK(invalid.model=="gordon");
  }
  const auto absent=projected("\\name\\Bob");CHECK_FALSE(absent.model_advertised);
  const auto duplicate=hlclient::test::user_info_fixture::make_message(1U,42U,"\\model\\gordon\\MODEL\\barney");
  CHECK_FALSE(gs::UserInfoUpdateParser{}.parse(duplicate));
  CHECK(gs::valid_player_model_virtual_name("models/player/gordon/gordon.mdl"));
  for(const auto name:{"models/player/../gordon.mdl","models/player/a/b.mdl","models/player/a/a.mdl/extra",
      "C:/models/player/a/a.mdl","models/player/A/A.mdl","models/player/a/../a.mdl"})
    CHECK_FALSE(gs::valid_player_model_virtual_name(name));
}
TEST_CASE("E31 committed per occupant appearance does not rewrite canonical model slots","[e31][appearance][game-api]") {
  api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
  hlclient::client::RuntimeClientObservationState observation;observation.generation=1;
  std::uint64_t ordinal=1;
  bool committed=false;
  const auto apply=[&](std::uint32_t slot,std::string_view info,bool replacement,bool present=true) {
    api::PlayerDisplayUpdate update{slot,present,replacement,"Fixture",{}};
    if(present) update.appearance=projected(info);
    else {
      // Empty info is a runtime departure, not a valid standalone signon
      // identity. Exercise the E28 stream owner without relaxing its parser.
      gs::OwnedServicePayload payload;
      payload.bytes=hlclient::test::user_info_fixture::make_message(static_cast<std::uint8_t>(slot-1U),42U,info);
      CHECK_FALSE(gs::UserInfoUpdateParser{}.parse(payload.bytes));
      payload.source_sequence=91U;payload.source_acknowledgement=73U;
      payload.source_reliable=true;payload.reassembled=true;payload.decompressed=true;
      payload.acknowledgement_reliable=true;payload.direction=gs::NetchanDirection::server_to_client;
      const auto cursor=gs::StockRuntimeSourceCursor::create(0U,0U,payload.bytes.size());REQUIRE(cursor);
      gs::RuntimeControlState control{1U};
      const auto decoded=gs::RuntimeControlDecoder{}.decode_and_apply(
          gs::RuntimeControlDecodeInput{payload,*cursor,1U,static_cast<std::size_t>(ordinal),{}},control);
      REQUIRE(decoded);REQUIRE(decoded.batch->events.size()==1U);
      const auto& departure=std::get<gs::RuntimeControlUserInfoUpdate>(decoded.batch->events[0].body);
      REQUIRE_FALSE(departure.present);REQUIRE_FALSE(departure.identity);
      CHECK(departure.client_index+1U==slot);CHECK(departure.info_string_length==0U);
      update.present=departure.present;
    }
    api::GameMessageView message{api::GameMessageKind::player_display,{}, {},
        {ordinal,static_cast<std::size_t>(ordinal),static_cast<std::uint32_t>(ordinal),0,512},std::nullopt,update};
    ++ordinal;
    auto staged=host.stage_record({committed ? &observation : nullptr,observation,std::span{&message,1U},1U,4U});REQUIRE(staged);
    host.commit_record(std::move(*staged.state));committed=true;
  };
  apply(2,"\\model\\gordon\\topcolor\\30",true);apply(3,"\\model\\barney\\topcolor\\200",true);
  const auto a=host.player_appearance(2,"models/player.mdl"),b=host.player_appearance(3,"models/player.mdl");
  CHECK(a.model_name=="models/player/gordon/gordon.mdl");CHECK(b.model_name=="models/player/barney/barney.mdl");
  CHECK(a.fields.top_color==30U);CHECK(b.fields.top_color==200U);CHECK(a.occupant_epoch>0U);
  {
    api::PlayerDisplayUpdate uncommitted{2,true,false,"Fixture",projected("\\model\\gina")};
    api::GameMessageView message{api::GameMessageKind::player_display,{}, {},
        {ordinal,static_cast<std::size_t>(ordinal),static_cast<std::uint32_t>(ordinal),0,512},std::nullopt,uncommitted};
    const auto staged=host.stage_record({&observation,observation,std::span{&message,1U},1U,4U});REQUIRE(staged);
    CHECK(host.player_appearance(2,"models/player.mdl").fields==a.fields);
  }
  apply(2,"\\model\\gina\\topcolor\\90",false);
  const auto changed=host.player_appearance(2,"models/player.mdl");
  CHECK(changed.occupant_epoch==a.occupant_epoch);CHECK(changed.appearance_revision>a.appearance_revision);
  CHECK(changed.model_name=="models/player/gina/gina.mdl");CHECK(host.player_appearance(3,"models/player.mdl").fields==b.fields);
  const auto explicit_model=host.player_appearance(2,"models/special_player.mdl");
  CHECK(explicit_model.status==api::PlayerAppearanceStatus::explicit_server_model);CHECK(explicit_model.model_name.empty());
  apply(2,"",false,false);CHECK(host.player_appearance(2,"models/player.mdl").model_name.empty());
  apply(2,"\\model\\barney",true);CHECK(host.player_appearance(2,"models/player.mdl").occupant_epoch!=a.occupant_epoch);
  host.teardown();CHECK(host.player_appearance(3,"models/player.mdl").model_name.empty());
}
TEST_CASE("E31 palette variants preserve untouched indexed pixels alpha and original shared source","[e31][appearance][palette]") {
  auto skeleton=std::make_shared<assets::SkeletalModelAssetData>();
  assets::ModelTextureAsset texture;texture.source_name="DM_Base.bmp";texture.width=5;texture.height=1;
  texture.indexed_pixels={159,160,191,192,255};
  texture.rgba8_level_zero.resize(20);
  for(std::size_t i=0;i<256;++i)texture.palette_rgb[i]={200,100,20};
  for(std::size_t p=0;p<5;++p)for(std::size_t c=0;c<4;++c)
    texture.rgba8_level_zero[p*4+c]=c==3 ? std::byte{static_cast<unsigned char>(p==4 ? 0 : 255)} :
        std::byte{texture.palette_rgb[texture.indexed_pixels[p]][c]};
  skeleton->textures.push_back(texture);texture.source_name="face.bmp";skeleton->textures.push_back(texture);
  assets::ModelAsset source;source.skeletal_data=skeleton;
  api::PlayerAppearanceFields fields;fields.top_color=120;fields.bottom_color=240;
  const auto ranges=hl::player_palette_ranges(fields,*skeleton);REQUIRE(ranges.size()==2U);
  const auto mapped=gs::remap_player_model_palette(source,ranges);REQUIRE(mapped);
  const auto& result=mapped->skeletal_data->textures[0];
  CHECK(result.palette_rgb[160]==(std::array<std::uint8_t,3>{20,200,20}));
  CHECK(result.palette_rgb[192]==(std::array<std::uint8_t,3>{20,20,200}));
  for(const auto p:{0U,4U})for(std::size_t c=0;c<4;++c)CHECK(result.rgba8_level_zero[p*4+c]==texture.rgba8_level_zero[p*4+c]);
  CHECK(mapped->skeletal_data->textures[1].rgba8_level_zero==texture.rgba8_level_zero);
  CHECK(source.skeletal_data->textures[0].rgba8_level_zero==texture.rgba8_level_zero);
  CHECK(result.indexed_pixels==texture.indexed_pixels);CHECK(result.rgba8_level_zero[19]==std::byte{0});
  fields.top_color=30;const auto second=gs::remap_player_model_palette(source,hl::player_palette_ranges(fields,*skeleton));REQUIRE(second);
  CHECK(second->skeletal_data->textures[0].rgba8_level_zero!=result.rgba8_level_zero);
}
TEST_CASE("71b97 palette matching preserves exact ASCII names without owning string temporaries",
    "[gauss-71b97][e31][appearance][palette]") {
  assets::SkeletalModelAssetData model;
  model.textures.resize(1);
  api::PlayerAppearanceFields fields;fields.top_color=30;fields.bottom_color=240;
  // Exhaust every ASCII upper/lower spelling, including the extension.
  for(const std::string_view base:{"dm_base","dm_base.bmp"}) {
    for(unsigned mask=0;mask<(1U<<base.size());++mask) {
      std::string name{base};
      for(std::size_t i=0;i<name.size();++i)
        if((mask&(1U<<i))&&name[i]>='a'&&name[i]<='z')name[i]=static_cast<char>(name[i]-'a'+'A');
      model.textures[0].source_name=name;
      const auto ranges=hl::player_palette_ranges(fields,model);
      REQUIRE(ranges.size()==2U);
      CHECK(ranges[0].texture==0U);CHECK(ranges[0].first==160U);CHECK(ranges[0].last==191U);CHECK(ranges[0].hue_degrees==30U);
      CHECK(ranges[1].texture==0U);CHECK(ranges[1].first==192U);CHECK(ranges[1].last==223U);CHECK(ranges[1].hue_degrees==240U);
      CHECK(model.textures[0].source_name==name);
    }
  }
  for(const std::string_view name:{"","dm_bas","dm_base.","dm_base.bmpx","xDM_Base.bmp","DM_Base.bmp "," dm_base",
      "DM_Base/bmp","PLAYER_Shldr-Joint.bmp","dm_base.bmP/","dm_base.BMP.bak"}) {
    model.textures[0].source_name=name;
    CHECK(hl::player_palette_ranges(fields,model).empty());
  }
  // High-bit bytes and embedded NULs are not locale-folded or truncated.
  for(const unsigned byte:{0U,0x80U,0xc4U,0xffU}) {
    model.textures[0].source_name="DM_Base.bmp";
    model.textures[0].source_name[0]=static_cast<char>(byte);
    CHECK(hl::player_palette_ranges(fields,model).empty());
    model.textures[0].source_name="DM_Base.bmp";
    model.textures[0].source_name.push_back(static_cast<char>(byte));
    CHECK(hl::player_palette_ranges(fields,model).empty());
  }
  model.textures[0].source_name="DM_Base.bmp";
  fields.top_color.reset();fields.bottom_color.reset();
  CHECK(hl::player_palette_ranges(fields,model).empty());
  fields.top_color=0;
  REQUIRE(hl::player_palette_ranges(fields,model).size()==1U);
  model.textures.resize(129);
  CHECK(hl::player_palette_ranges(fields,model).empty());
}
TEST_CASE("E31 retained model budget includes animation objects even without quantized values","[e31][appearance][bounds]") {
  assets::ModelAsset model;auto skeleton=std::make_shared<assets::SkeletalModelAssetData>();model.skeletal_data=skeleton;
  const auto before=gs::retained_player_model_bytes(model);
  skeleton->sequences.resize(16);
  for(auto& sequence:skeleton->sequences) {
    sequence.animation_blends.resize(4);
    for(auto& blend:sequence.animation_blends) {
      blend.bone_tracks.resize(32);
      for(auto& track:blend.bone_tracks)for(auto& channel:track.channels)channel.runs.resize(8);
    }
  }
  CHECK(skeleton->statistics.animation_value_bytes==0U);
  CHECK(gs::retained_player_model_bytes(model)>before+16U*4U*32U*6U*8U*sizeof(assets::ModelAnimationRun));
}
