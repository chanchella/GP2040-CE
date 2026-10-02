#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "oag/firmware/diamond_wifi_portal.h"
#include "oag/config/diamond_persistent_config.h"
struct tcp_pcb { std::string status,type,body; };
static void sendResponse(tcp_pcb* p,const char* s,const char* t,const char* b) { p->status=s; p->type=t; p->body=b; }
static bool staticPath(const char* p,const char* a) {const auto n=std::strlen(a);return !std::strncmp(p,a,n)&&(p[n]==0||p[n]=='?');}
#include "../../firmware/src/portal_form_parser.h"
#include "../../firmware/src/oag_smart_assets.h"
namespace oag::firmware {
class DiamondConfigStore {
public:
    DiamondPersistentConfig value {};unsigned saves=0;
    auto& config() { return value; }
    std::uint32_t generation() const { return saves; }
    bool save() { ++saves;return true; }
};
class DiamondGameLibraryStore {
public:
    std::vector<DiamondGameContent> games {kDiamondLibraryGameSlots}; unsigned saves=0;
    bool loadGame(std::size_t game,const DiamondPersistentConfig&,DiamondGameContent& out) {if(game>=games.size())return false;out=games[game];return true;}
    bool saveGame(std::size_t game,const DiamondGameContent& out) {games[game]=out;++saves;return true;}
    bool activate(std::size_t game,const DiamondPersistentConfig&) {return game<games.size();}
};
bool DiamondWifiPortal::start(DiamondConfigStore& store,DiamondGameLibraryStore& games,bool live) {store_=&store;games_=&games;liveMode_=live;return true;}
void DiamondWifiPortal::schedulePlayReboot(std::uint32_t) {}
}
#include "smart_portal_under_test.inc"
struct Memory final:oag::OagSmartStorage {
    std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(40*32768,255);unsigned writes=0;
    bool ready() const override {return true;}
    const oag::OagSmartRecord* record(std::size_t g,std::uint8_t copy) const override {return reinterpret_cast<const oag::OagSmartRecord*>(bytes.data()+(g*2+copy)*32768);}
    bool write(std::size_t g,std::uint8_t copy,const std::uint8_t* data,std::size_t size) override {std::memcpy(bytes.data()+(g*2+copy)*32768,data,size);++writes;return true;}
};
int main() {
    oag::firmware::DiamondConfigStore config;oag::firmware::DiamondGameLibraryStore games;oag::firmware::DiamondWifiPortal portal;
    Memory memory;oag::OagSmartStore store(memory);store.activate(0);oag::OagSmartApi api(store);portal.attachSmart(api);portal.start(config,games);
    const auto request=[&](const char* method,const char* path,const char* body="") {tcp_pcb r;const auto raw=std::string(method)+" "+path+" HTTP/1.1\r\n\r\n"+body;portal.handleHttpRequest(&r,raw.c_str(),raw.size());return r;};
    auto r=request("GET","/");assert(r.status=="200 OK");assert(r.body.find("OAG SMART COMBO LOGIC")!=std::string::npos);assert(r.body.find("OAG WEAPON TUNING")!=std::string::npos);assert(r.body.find("2400 DPI")!=std::string::npos);
    assert(r.body.find("id=pro-lab")==std::string::npos&&r.body.find("id=csteps")==std::string::npos&&r.body.find("id=rh")==std::string::npos);
    for (auto old:{"/api/recoil?game=1&weapon=1","/api/combo?game=1&slot=1","/api/combo-program","/api/combos?game=1","/api/pro-input","/api/pro-reset","/api/pro-calibrate/start"})
        for (auto method:{"GET","POST"}) assert(request(method,old,"game=1&slot=1").status=="410 Gone");
    for (auto old:{"/app.js","/gwc.js","/gwc-editor.js","/gwc-save.js","/pro-form.js"}) assert(request("GET",old).status=="404 Not Found");
    assert(memory.writes==0&&config.saves==0&&games.saves==0);
    r=request("GET","/api/oag-input-auto");assert(r.body.find("\"processingHz\":1000")!=std::string::npos&&r.body.find("\"hardwareDpiChanged\":false")!=std::string::npos);
    for (const auto& asset:kOagSmartAssets) {const auto uri=std::string(asset.path)+"?v=2";assert(request("GET",uri.c_str()).status=="200 OK");assert(std::strlen(asset.body)+640<5840);}
    assert(request("GET","/oag-smart").status=="200 OK");assert(request("GET","/oag-weapons").status=="200 OK");
    assert(request("POST","/api/name","type=game&slot=1&name=OAG").status=="200 OK");assert(games.saves==1);
    assert(request("POST","/api/name","type=combo&game=1&slot=1&name=OAG").status=="400 Bad Request");assert(games.saves==1);
    portal.start(config,games,true);assert(request("POST","/api/name","type=game&slot=1&name=OAG").status=="409 Conflict");
    assert(request("POST","/api/oag/cancel").status=="202 Accepted");api.task();assert(store.takeCancel());assert(memory.writes==0);
    std::cout<<"OAG actual firmware portal: new home, retired routes blocked, Arabic assets, optional live API, no accidental Flash writes PASS\n";
}
