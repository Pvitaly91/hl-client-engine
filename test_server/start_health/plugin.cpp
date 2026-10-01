// Test-server-only, GPL-2.0-or-later with the Metamod HL Engine/MOD exception.
// Only the public engine-owned entvars health member is written. No private
// game object layout, offset lookup, fabricated message or game replacement.
#include <extdll.h>
#include <meta_api.h>
#include <array>
#include <cstring>
#include <string>

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals{};
meta_globals_t* gpMetaGlobals{};
gamedll_funcs_t* gpGamedllFuncs{};
mutil_funcs_t* gpMetaUtilFuncs{};
namespace {
struct Connection { edict_t* entity{}; int serial{}; bool eligible{}; };
std::array<Connection, 33> connections{};
bool consumed = false, rejected = false, configured = false;
const char* configuration_reason="not_checked";
std::string run, name;
plugin_info_t info{META_INTERFACE_VERSION, "HLClient owned start health", "1.0",
  "2026-09-27", "hl-client-engine", "https://github.com/Pvitaly91/hl-client-engine",
  "HLC50", PT_STARTUP, PT_ANYTIME};
bool hex_run(const char* value) {
  if (!value || std::strlen(value) != 32) return false;
  for (int i=0; i<32; ++i) if (!((value[i]>='0' && value[i]<='9') ||
    (value[i]>='a' && value[i]<='f'))) return false;
  return true;
}
const char* local(const char* key) {
  return g_engfuncs.pfnInfoKeyValue(g_engfuncs.pfnGetInfoKeyBuffer(nullptr), const_cast<char*>(key));
}
bool configure() {
  // Do not latch an early incomplete startup as a permanent configuration
  // failure. Only a validated configuration becomes immutable.
  if (configured) return true;
  const auto fail=[](const char* reason) { configuration_reason=reason; return false; };
  const char* value = local("hlc_test_run");
  if (!value || !*value) return fail("run_missing");
  if (!hex_run(value)) return fail("run_invalid");
  run=value; name="HLC50_"+run.substr(0,24);
  const char* profile=local("hlc_test_profile");
  if (!profile || !*profile) return fail("profile_missing");
  // The console-facing label contains hyphens, but stock stuffcmds treats
  // those as delimiters. Only the underscore startup sentinel is accepted.
  if (std::strcmp(profile,"test_server_assisted")!=0) return fail("profile_mismatch");
  if (!gpGlobals) return fail("globals_unavailable");
  if (gpGlobals->deathmatch==0) return fail("deathmatch_unavailable");
  if (gpGlobals->maxClients<1 || gpGlobals->maxClients>32) return fail("client_limit_invalid");
  if (std::strcmp(g_engfuncs.pfnSzFromIndex(gpGlobals->mapname),"crossfire")!=0)
    return fail("map_mismatch");
  configured=true; configuration_reason="ready";
  return true;
}
void log(const char* result) {
  g_engfuncs.pfnAlertMessage(at_logged,
    "[hlclient-test-health] run=%s requested_start_health=50 server_setup_applied=%s max_health=%s max_health_source=%s\n",
    run.c_str(), result, std::strcmp(result,"true")==0 ? "100" : "unavailable",
    std::strcmp(result,"true")==0 ? "server_entvars" : "unavailable");
}
qboolean connect_post(edict_t* entity, const char* player, const char* address, char*) {
  if (!consumed && !rejected && configure() && (!player || name != player))
    log("target_not_matched");
  if (!consumed && !rejected && configure() && player && name == player) {
    // Original game accepted this connection; use actual callback address,
    // never a session-assigned slot or client-authored IP field.
    if (!gpMetaGlobals->orig_ret || !*static_cast<qboolean*>(gpMetaGlobals->orig_ret) ||
        !address || std::strncmp(address,"127.0.0.1:",10)!=0) {
      rejected=true; log("identity_rejected");
    } else {
      const int index=g_engfuncs.pfnIndexOfEdict(entity);
      if (index<1 || index>32) { rejected=true; log("slot_rejected"); }
      else {
        for (const auto& c:connections) if(c.eligible && c.entity!=entity) rejected=true;
        if (rejected) log("ambiguous_identity");
        else connections[index]={entity, entity->serialnumber, true};
      }
    }
  }
  RETURN_META_VALUE(MRES_IGNORED, TRUE);
}
void put_post(edict_t* entity) {
  if (!consumed && !rejected && configure()) {
    const int index=g_engfuncs.pfnIndexOfEdict(entity);
    if (index>=1 && index<=32) {
      const auto& c=connections[index];
      if (c.eligible && c.entity==entity && c.serial==entity->serialnumber) {
        const char* actual=g_engfuncs.pfnInfoKeyValue(g_engfuncs.pfnGetInfoKeyBuffer(entity),"name");
        int matches=0;
        for(int slot=1; slot<=gpGlobals->maxClients; ++slot) {
          edict_t* other=g_engfuncs.pfnPEntityOfEntIndex(slot);
          if(other && !other->free && (other->v.flags&FL_CLIENT)) {
            const char* other_name=g_engfuncs.pfnInfoKeyValue(g_engfuncs.pfnGetInfoKeyBuffer(other),"name");
            if(other_name && name==other_name) ++matches;
          }
        }
        if (matches!=1 || !actual || name!=actual || entity->free || !(entity->v.flags&FL_CLIENT) ||
            (entity->v.flags&(FL_FAKECLIENT|FL_GODMODE)) || entity->v.deadflag!=DEAD_NO ||
            entity->v.health!=100 || entity->v.max_health!=100 ||
            entity->v.takedamage==DAMAGE_NO) {
          rejected=true; log("spawn_validation_failed");
        } else {
          // CBasePlayer::Spawn has finished in ClientPutInServer. Let the
          // unmodified game emit normal clientdata/Health on subsequent update.
          entity->v.health=50; consumed=true; log("true");
        }
      } else log("connection_binding_missing");
    }
  }
  RETURN_META(MRES_IGNORED);
}
void server_activate_post(edict_t*, int, int) {
  g_engfuncs.pfnServerPrint(configure()
    ? "[hlclient-test-health-stage] stage=server_activated configured=true\n"
    : "[hlclient-test-health-stage] stage=server_activated configured=false\n");
  // A fixed enum only: no raw localinfo, names, paths or authentication data.
  const auto diagnostic=std::string{"[hlclient-test-health-config] reason="}+configuration_reason+"\n";
  g_engfuncs.pfnServerPrint(diagnostic.c_str());
  RETURN_META(MRES_IGNORED);
}
void disconnect_post(edict_t* entity) {
  const int i=g_engfuncs.pfnIndexOfEdict(entity);
  if(i>=1 && i<=32) connections[i]={};
  RETURN_META(MRES_IGNORED);
}
int post_api(DLL_FUNCTIONS* table, int* version) {
  if (!table || !version || *version!=INTERFACE_VERSION) return FALSE;
  *table={}; table->pfnClientConnect=connect_post;
  table->pfnClientPutInServer=put_post; table->pfnClientDisconnect=disconnect_post;
  table->pfnServerActivate=server_activate_post;
  return TRUE;
}
}
extern "C" __declspec(dllexport) void WINAPI GiveFnptrsToDll(enginefuncs_t* functions, globalvars_t* globals) {
  g_engfuncs=*functions; gpGlobals=globals;
}
C_DLLEXPORT int Meta_Query(char* version, plugin_info_t** output, mutil_funcs_t* utils) {
  if(!version || std::strcmp(version,META_INTERFACE_VERSION)!=0 || !output) return FALSE;
  *output=&info; gpMetaUtilFuncs=utils; return TRUE;
}
C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS* table,
  meta_globals_t* globals, gamedll_funcs_t* game) {
  if(now!=PT_STARTUP || !table || !globals || !game) return FALSE;
  gpMetaGlobals=globals; gpGamedllFuncs=game; *table={};
  // Readiness must be visible with developer=0, including early attach before
  // multiplayer startup. AlertMessage's at_console/at_logged filters cannot
  // provide that contract; ServerPrint uses the owned server console directly.
  g_engfuncs.pfnServerPrint("[hlclient-test-health-stage] stage=attached\n");
  table->pfnGetEntityAPI2_Post=post_api; return TRUE;
}
C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME, PL_UNLOAD_REASON) { return TRUE; }
