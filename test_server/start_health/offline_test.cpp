#include <extdll.h>
#include <meta_api.h>
#include "../../include/hlclient/app/test_start_health_gate.hpp"
#include <array>
#include <string>
#include <iostream>
#include <stdexcept>
#include <cstdarg>
namespace {
std::array<edict_t,33> entities{};
std::string token="0123456789abcdef0123456789abcdef", profile="test_server_assisted";
std::string target="HLC50_0123456789abcdef01234567", current_name, map="crossfire", logs;
int checks=0;
int developer=0;
globalvars_t* engine_globals=nullptr;
void check(bool yes,const char* text) { ++checks; if(!yes) throw std::runtime_error(text); }
char* buffer(edict_t* e) { return reinterpret_cast<char*>(e); }
char* value(char* b,char* key) {
  if(!b) {
    if(std::strcmp(key,"hlc_test_run")==0) return token.data();
    if(std::strcmp(key,"hlc_test_profile")==0) return profile.data();
    return const_cast<char*>("");
  }
  return current_name.data();
}
int index(const edict_t* e) { return static_cast<int>(e-entities.data()); }
edict_t* entity(int i) { return &entities[i]; }
const char* string_at(int) { return map.c_str(); }
void alert(ALERT_TYPE type,char* format,...) {
  // Match engine channel filtering: attach can precede multiplayer startup,
  // and normal dedicated runs do not enable developer console diagnostics.
  if(type==at_console && developer==0) return;
  if(type==at_logged && engine_globals->deathmatch==0) return;
  char out[512]{}; va_list args; va_start(args,format); vsnprintf(out,sizeof(out),format,args); va_end(args); logs+=out;
}
void server_print(const char* message) { logs+=message; }
}
int main(int argc,char** argv) try {
  if(argc!=2) return 2;
  // DLL/ABI test only. The fake engine contains no game/server/Steam API.
  for(developer=0; developer<=1; ++developer) {
  for(int scenario=0; scenario<24; ++scenario) {
    entities={}; logs.clear(); profile="test_server_assisted"; map="";
    token="0123456789abcdef0123456789abcdef";
    current_name=target;
    HMODULE dll=LoadLibraryA(argv[1]); check(dll!=nullptr,"load project addon");
    auto give=reinterpret_cast<void(WINAPI*)(enginefuncs_t*,globalvars_t*)>(GetProcAddress(dll,"GiveFnptrsToDll"));
    auto query=reinterpret_cast<META_QUERY_FN>(GetProcAddress(dll,"Meta_Query"));
    auto attach=reinterpret_cast<META_ATTACH_FN>(GetProcAddress(dll,"Meta_Attach"));
    auto detach=reinterpret_cast<META_DETACH_FN>(GetProcAddress(dll,"Meta_Detach"));
    check(give && query && attach && detach,"exact exports");
    enginefuncs_t engine{}; engine.pfnGetInfoKeyBuffer=buffer; engine.pfnInfoKeyValue=value;
    engine.pfnIndexOfEdict=index; engine.pfnPEntityOfEntIndex=entity;
    engine.pfnSzFromIndex=string_at; engine.pfnAlertMessage=alert;
    engine.pfnServerPrint=server_print;
    globalvars_t globals{}; engine_globals=&globals;
    give(&engine,&globals);
    plugin_info_t* info{}; char version[]=META_INTERFACE_VERSION;
    check(query(version,&info,nullptr)!=0,"API version");
    META_FUNCTIONS hooks{}; meta_globals_t meta{}; qboolean accepted=TRUE; meta.orig_ret=&accepted;
    gamedll_funcs_t game{};
    check(attach(PT_STARTUP,&hooks,&meta,&game)!=0,"attach");
    check(logs=="[hlclient-test-health-stage] stage=attached\n","startup attach evidence before multiplayer with developer filtering");
    DLL_FUNCTIONS post{}; int api=INTERFACE_VERSION;
    check(hooks.pfnGetEntityAPI2_Post(&post,&api)!=0,"post hooks");
    check(post.pfnStartFrame==nullptr && post.pfnPlayerPostThink==nullptr && post.pfnSpawn==nullptr,"no continuous/respawn health writer");
    check(post.pfnServerActivate!=nullptr,"server activation hook");
    globals.deathmatch=1; globals.maxClients=8; map="crossfire";
    if(scenario==13) {
      profile=""; post.pfnServerActivate(entities.data(),33,8);
      check(logs.find("configured=false")!=std::string::npos,"early startup not ready");
      check(logs.find("configured=true")==std::string::npos,"incomplete startup cannot confirm readiness");
      check(logs.find("[hlclient-test-health-config] reason=profile_missing\n")!=std::string::npos,"incomplete startup reason");
      profile="test_server_assisted";
    }
    auto& p=entities[7]; p.serialnumber=42; p.v.flags=FL_CLIENT;
    p.v.health=100; p.v.max_health=100; p.v.armorvalue=23;
    p.v.takedamage=DAMAGE_AIM; p.v.weapons=9;
    const char* expected_configuration="ready";
    if(scenario==1) { profile="stock"; expected_configuration="profile_mismatch"; }
    if(scenario==2) current_name="somebody_else";
    if(scenario==3) accepted=FALSE;
    if(scenario==4) p.v.max_health=75;
    if(scenario==5) { entities[6].v.flags=FL_CLIENT; }
    if(scenario==6) p.v.flags|=FL_FAKECLIENT;
    if(scenario==7) { map="boot_camp"; expected_configuration="map_mismatch"; }
    if(scenario==8) p.v.deadflag=DEAD_DEAD;
    if(scenario==9) p.free=TRUE;
    if(scenario==14) { token=""; expected_configuration="run_missing"; }
    if(scenario==15) { token="0123"; expected_configuration="run_invalid"; }
    if(scenario==16) { profile="test"; expected_configuration="profile_mismatch"; }
    if(scenario==17) { globals.deathmatch=0; expected_configuration="deathmatch_unavailable"; }
    if(scenario==18) { globals.maxClients=0; expected_configuration="client_limit_invalid"; }
    if(scenario==19) { globals.maxClients=33; expected_configuration="client_limit_invalid"; }
    if(scenario==20) { give(&engine,nullptr); expected_configuration="globals_unavailable"; }
    if(scenario==21) { profile=""; expected_configuration="profile_missing"; }
    if(scenario==22) { token[0]='g'; expected_configuration="run_invalid"; }
    if(scenario==23) { profile="test-server-assisted"; expected_configuration="profile_mismatch"; }
    post.pfnServerActivate(entities.data(),33,8);
    check((logs.find("[hlclient-test-health-stage] stage=server_activated configured=true\n")!=std::string::npos)==
      (std::strcmp(expected_configuration,"ready")==0),"configured readiness matches validated inputs with developer filtering");
    check(logs.find(std::string{"[hlclient-test-health-config] reason="}+expected_configuration+"\n")!=std::string::npos,
      "exact configuration diagnostic without raw values");
    if(std::strcmp(expected_configuration,"ready")!=0)
      check(logs.find("[hlclient-test-health-stage] stage=server_activated configured=false\n")!=std::string::npos,
        "invalid configuration remains explicitly unavailable");
    char reason[128]{};
    if(scenario!=12) post.pfnClientConnect(&p,current_name.c_str(),scenario==10 ? "192.0.2.1:30001" : "127.0.0.1:30001",reason);
    if(scenario==11) ++p.serialnumber;
    post.pfnClientPutInServer(&p);
    check(p.v.health==(scenario==0 || scenario==13 ? 50 : 100),"only exact first eligible spawn changed");
    check(p.v.max_health==(scenario==4 ? 75 : 100),"maximum never reduced");
    check(p.v.armorvalue==23 && p.v.weapons==9 && p.v.takedamage==DAMAGE_AIM,"unrelated fields unchanged");
    if(scenario==0 || scenario==13) {
      check(logs.find("server_setup_applied=true")!=std::string::npos,"server evidence");
      p.v.health=80; post.pfnClientPutInServer(&p); check(p.v.health==80,"duplicate doesn't undo healing");
      p.v.health=100; post.pfnClientPutInServer(&p); check(p.v.health==100,"respawn ordinary");
      post.pfnClientDisconnect(&p); ++p.serialnumber;
      post.pfnClientConnect(&p,target.c_str(),"127.0.0.1:30002",reason);
      post.pfnClientPutInServer(&p); check(p.v.health==100,"reconnect no second grant");
      const auto applied=logs.find("server_setup_applied=true");
      check(logs.find("server_setup_applied=true",applied+1)==std::string::npos,"exactly one application record");
    } else check(logs.find("server_setup_applied=true")==std::string::npos,"failure not success");
    check(detach(PT_ANYTIME,PNL_COMMAND)!=0,"detach"); FreeLibrary(dll);
  }
  }
  hlclient::app::TestStartHealthGate gate(true);
  gate.observe(50,false,1); check(!gate.ready(),"retained not ready");
  gate.observe(std::nullopt,true,2); check(!gate.ready(),"missing not ready");
  gate.observe(100,true,3); check(!gate.ready() && gate.expired(15),"100 deadline failure");
  gate.observe(50,true,3); check(!gate.ready(),"same source cannot count twice");
  gate.observe(50,true,4); check(gate.ready(),"fresh server 50 ready");
  gate.observe(80,true,5); check(gate.ready(),"healing doesn't rearm gate");
  check(hlclient::app::TestStartHealthGate(false).ready(),"ordinary mode unaffected");
  std::cout<<"PASS checks="<<checks<<" live=not_run\n"; return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 2; }
