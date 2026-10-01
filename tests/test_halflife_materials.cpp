#include <hlclient/games/halflife/materials.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/assets/wav_importer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
namespace hl=hlclient::games::halflife;
namespace api=hlclient::game_api;
}
TEST_CASE("E7 Half-Life material table is bounded, owning and typed", "[e7][materials]") {
    hl::HalfLifeMaterials table;
    CHECK(table.configure("")==hl::MaterialTableStatus::missing);
    CHECK(table.lookup("WALL").source==hl::MaterialSource::missing_table_concrete_fallback);
    std::string bytes="// fixture\r\n\r\n C CONCRETE\r\nM WALL\nW WOOD\nT TILE\nY GLASS\nN SNOW\nF FLESH\nW WALL\n";
    CHECK(table.configure(bytes)==hl::MaterialTableStatus::ready);
    CHECK(table.count()==7U); // duplicate first normalized key wins
    bytes.assign(bytes.size(),'x');
    CHECK(table.lookup("wall").kind==hl::MaterialKind::metal); // no borrowed bytes
    CHECK(table.lookup("WALL").source==hl::MaterialSource::table_match);
    CHECK(table.lookup("OTHER").source==hl::MaterialSource::unknown_concrete_fallback);
    CHECK(table.lookup("CONCRETE").kind==hl::MaterialKind::concrete);
    CHECK(table.lookup("SNOW").kind==hl::MaterialKind::snow);
    CHECK(table.lookup("FLESH").kind==hl::MaterialKind::flesh);
    CHECK(table.configure("Q BAD\n")==hl::MaterialTableStatus::malformed);
    CHECK(table.lookup("WALL").source==hl::MaterialSource::malformed_table_concrete_fallback);
    CHECK(table.configure("M\n")==hl::MaterialTableStatus::malformed);
    CHECK(table.configure(std::string(512,'x'))==hl::MaterialTableStatus::limit_exceeded);
    CHECK(table.configure(std::string(128U*1024U+1U,'x'))==hl::MaterialTableStatus::limit_exceeded);
    std::string full;
    for(unsigned i=0;i<512;++i) full+="M T"+std::to_string(i)+"\n";
    CHECK(table.configure(full)==hl::MaterialTableStatus::ready);
    CHECK(table.count()==512U);
    full+="M OVERFLOW\n";
    CHECK(table.configure(full)==hl::MaterialTableStatus::limit_exceeded);
    CHECK(table.count()==0U);
    CHECK(table.configure("M WALL")==hl::MaterialTableStatus::ready); // no trailing LF
    CHECK(table.lookup("WALL").kind==hl::MaterialKind::metal);
}

TEST_CASE("E7 traced texture normalization follows the pinned twelve-character lookup", "[e7][materials]") {
    hl::HalfLifeMaterials table;
    CHECK(table.configure("M WALL\nW ABCDEFGHIJKL\n")==hl::MaterialTableStatus::ready);
    for(const auto name:{"WALL","wall","+0WALL","-0WALL","{WALL","!WALL","~WALL","+0{WALL","-0!WALL","+0~WALL"}) {
        const auto found=table.lookup(name);
        CHECK(found.kind==hl::MaterialKind::metal);
        CHECK(found.source==hl::MaterialSource::table_match);
    }
    CHECK(table.lookup("ABCDEFGHIJKLZZZ").kind==hl::MaterialKind::wood);
    CHECK(table.lookup(".WALL").source==hl::MaterialSource::unknown_concrete_fallback);
    CHECK_FALSE(hl::HalfLifeMaterials::normalize("+"));
    CHECK_FALSE(hl::HalfLifeMaterials::normalize(""));
}

TEST_CASE("E7 pinned material impact families and crowbar balance remain deterministic", "[e7][materials][audio]") {
    api::LocalWeaponActionIdentity action{};
    action.command_sequence=1;
    const auto sample=[&](hl::MaterialKind k) { return hl::material_impact_sound(k,action,false); };
    CHECK(sample(hl::MaterialKind::concrete).reference.name()=="player/pl_step1.wav");
    CHECK(sample(hl::MaterialKind::metal).reference.name()=="player/pl_metal1.wav");
    CHECK(sample(hl::MaterialKind::dirt).reference.name()=="player/pl_dirt1.wav");
    CHECK(sample(hl::MaterialKind::vent).reference.name()=="player/pl_duct1.wav");
    CHECK(sample(hl::MaterialKind::grate).reference.name()=="player/pl_grate1.wav");
    CHECK(sample(hl::MaterialKind::tile).reference.name()=="player/pl_tile1.wav");
    CHECK(sample(hl::MaterialKind::slosh).reference.name()=="player/pl_slosh1.wav");
    CHECK(sample(hl::MaterialKind::wood).reference.name()=="debris/wood1.wav");
    CHECK(sample(hl::MaterialKind::glass).reference.name()=="debris/glass1.wav");
    CHECK(sample(hl::MaterialKind::computer).reference.name()=="debris/glass1.wav");
    CHECK(sample(hl::MaterialKind::snow).reference.name()=="player/pl_step1.wav");
    CHECK(sample(hl::MaterialKind::flesh).reference.name()=="player/pl_step1.wav");
    CHECK(hl::crowbar_strike_volume(hl::MaterialKind::concrete)==0.6F);
    CHECK(hl::crowbar_strike_volume(hl::MaterialKind::metal)==0.3F);
    CHECK(hl::crowbar_strike_volume(hl::MaterialKind::slosh)==0.0F);
    ++action.command_sequence;
    CHECK(sample(hl::MaterialKind::metal).reference.name()=="player/pl_metal2.wav");
    CHECK(sample(hl::MaterialKind::wood).reference.name()=="debris/wood2.wav");
    for(const auto& sound:hl::material_impact_preload()) {
        CHECK_FALSE(sound.name().empty());
        CHECK(hlclient::goldsrc::valid_local_sound_reference(sound));
    }
}

TEST_CASE("E7.1 pinned Glock ricochet family is an independent optional hit layer",
          "[e7][ricochet][audio]") {
    api::LocalWeaponActionIdentity action{};
    action.generation=1;
    action.command_sequence=1;
    CHECK_FALSE(hl::glock_ricochet_sound(action));
    action.command_sequence=11;
    const auto first=hl::glock_ricochet_sound(action);
    REQUIRE(first);
    CHECK(first->name()=="weapons/ric4.wav");
    CHECK(hl::glock_ricochet_sound(action)==first); // replay-stable
    action.command_sequence=13;
    REQUIRE(hl::glock_ricochet_sound(action));
    CHECK(hl::glock_ricochet_sound(action)->name()=="weapons/ric3.wav");
    action.command_sequence=17;
    CHECK_FALSE(hl::glock_ricochet_sound(action));
    unsigned selected=0;
    for(unsigned i=1;i<=100;++i) {
        action.command_sequence=i;
        if(hl::glock_ricochet_sound(action)) ++selected;
    }
    CHECK(selected==57U); // fixed local hash, approximately half of hits
    const auto preload=hl::glock_ricochet_preload();
    for(std::size_t i=0;i<preload.size();++i) {
        CHECK(preload[i].name()=="weapons/ric"+std::to_string(i+1)+".wav");
        CHECK(hlclient::goldsrc::valid_local_sound_reference(preload[i]));
    }
}

TEST_CASE("E7 opt-in installed materials table is a read-only parser control", "[e7][installed-materials]") {
    const char* root=SDL_getenv("HLCLIENT_LOCAL_GAME_ROOT");
    if(!root || !*root) SKIP("No opt-in read-only local game root");
    const auto file=std::filesystem::path{root}/"valve/sound/materials.txt";
    if(!std::filesystem::is_regular_file(file)) SKIP("Installed materials table unavailable");
    const auto size=std::filesystem::file_size(file);
    REQUIRE(size>0U);REQUIRE(size<=128U*1024U);
    std::ifstream source(file,std::ios::binary);REQUIRE(source.is_open());
    std::string bytes(static_cast<std::size_t>(size),'\0');
    source.read(bytes.data(),static_cast<std::streamsize>(size));
    REQUIRE(source.gcount()==static_cast<std::streamsize>(size));
    hl::HalfLifeMaterials table;
    REQUIRE(table.configure(bytes)==hl::MaterialTableStatus::ready);
    CHECK(table.count()>0U);
    CHECK(table.lookup("DUCT_FLR01").kind==hl::MaterialKind::vent);
}

TEST_CASE("E7 opt-in installed material samples decode as nonempty PCM", "[e7][installed-materials][audio]") {
    const char* root=SDL_getenv("HLCLIENT_LOCAL_GAME_ROOT");
    if(!root || !*root) SKIP("No opt-in read-only local game root");
    for(const auto& reference:hl::material_impact_preload()) {
        const auto file=std::filesystem::path{root}/"valve/sound"/reference.name();
        REQUIRE(std::filesystem::is_regular_file(file));
        std::ifstream source(file,std::ios::binary);REQUIRE(source.is_open());
        std::vector<char> raw{std::istreambuf_iterator<char>{source},{}};
        REQUIRE_FALSE(raw.empty());
        std::vector<std::byte> bytes;
        bytes.reserve(raw.size());
        for(char c:raw) bytes.push_back(static_cast<std::byte>(c));
        auto owned=hlclient::assets::AssetSource::create(
            "sound/"+std::string{reference.name()},std::move(bytes));
        REQUIRE(owned);
        auto pcm=hlclient::assets::WavImporter{}.import(*owned.source);
        REQUIRE(pcm);
        CHECK_FALSE(pcm.value().interleaved_samples.empty());
    }
    for(const auto& reference:hl::glock_ricochet_preload()) {
        const auto file=std::filesystem::path{root}/"valve/sound"/reference.name();
        REQUIRE(std::filesystem::is_regular_file(file));
        std::ifstream source(file,std::ios::binary);REQUIRE(source.is_open());
        std::vector<char> raw{std::istreambuf_iterator<char>{source},{}};
        REQUIRE_FALSE(raw.empty());
        std::vector<std::byte> bytes;
        bytes.reserve(raw.size());
        for(char c:raw) bytes.push_back(static_cast<std::byte>(c));
        auto owned=hlclient::assets::AssetSource::create(
            "sound/"+std::string{reference.name()},std::move(bytes));
        REQUIRE(owned);
        auto pcm=hlclient::assets::WavImporter{}.import(*owned.source);
        REQUIRE(pcm);
        CHECK_FALSE(pcm.value().interleaved_samples.empty());
    }
}
