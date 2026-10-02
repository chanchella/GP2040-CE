#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include "oag/firmware/diamond_wifi_portal.h"
struct tcp_pcb {std::string status,type,body;};
static std::uint64_t now=2000000;
static std::uint64_t time_us_64(){return now;}
static void sendResponse(tcp_pcb* p,const char* s,const char* t,const char* b){p->status=s;p->type=t;p->body=b;}
static bool staticPath(const char* p,const char* asset){const auto n=std::strlen(asset);return !std::strncmp(p,asset,n)&&(p[n]==0||p[n]=='?');}
#include "../../firmware/src/portal_form_parser.h"
#define TCP_SND_BUF 5840
#include "../../firmware/src/pro_input_portal_assets.h"
namespace oag::firmware {
class DiamondConfigStore {
public:
    oag::DiamondPersistentConfig value{},persisted{};
    bool failSave=false;
    oag::DiamondPersistentConfig& config(){return value;}
    std::uint32_t generation()const{return generation_;}
    bool save(){if(failSave)return false;persisted=value;++generation_;return true;}
private:std::uint32_t generation_=0;
};
class DiamondGameLibraryStore {};
bool DiamondWifiPortal::start(DiamondConfigStore& s,DiamondGameLibraryStore& g,bool){store_=&s;games_=&g;return true;}
#include "../../firmware/src/pro_input_portal.inc"
void DiamondWifiPortal::handleHttpRequest(void* client,const char* request,std::size_t){
 char method[8]{},path[96]{};assert(std::sscanf(request,"%7s %95s",method,path)==2);
 const char* body=std::strstr(request,"\r\n\r\n");body=body?body+4:"";
 if(!handleProInputRequest(client,method,path,body))sendResponse(static_cast<tcp_pcb*>(client),"404 Not Found","text/plain","Not found");
}
}
using namespace oag;
static std::string settings(const std::string& key="kind=0&scope=0"){
 return key+"&sourceDpi=800&targetDpi=1600&multiplier=1000&gainX=1000&gainY=1000&processingHz=1000&curve=1000&inner=0&outer=0&fullScale=48&auto=1&fractional=1&dpiSource=1";
}
int main(int argc,char** argv){
 firmware::DiamondConfigStore store;firmware::DiamondGameLibraryStore games;firmware::DiamondWifiPortal portal;DeviceRegistry registry;ProInputProcessor processor;
 auto mouse=registry.connectUsb({1,0},0x046d,0xc077,ProtocolKind::HidMouse);auto pad=registry.connectUsb({2,0},0x2563,0x0575,ProtocolKind::HidGamepad);assert(mouse&&pad);
 processor.setCapabilities(*mouse,1);processor.setCapabilities(*pad,4);processor.noteReport(*mouse,now);processor.noteReport(*pad,now);
 portal.attachProInput(processor,registry);portal.start(store,games);
 auto request=[&](const std::string& method,const std::string& path,const std::string& body=""){tcp_pcb response;std::string http=method+" "+path+" HTTP/1.1\r\n\r\n"+body;portal.handleHttpRequest(&response,http.c_str(),http.size());return response;};
 if(argc>1&&std::string(argv[1])=="serve"){
  std::string line;while(std::getline(std::cin,line)) {const auto a=line.find('\t'),b=line.find('\t',a+1);assert(a!=line.npos&&b!=line.npos);const auto r=request(line.substr(0,a),line.substr(a+1,b-a-1),line.substr(b+1));std::cout<<r.status.substr(0,3)<<'\t'<<r.body<<std::endl;}return 0;
 }
 auto r=request("GET","/api/pro-input?kind=0&scope=0");assert(r.status=="200 OK"&&r.body.find("\"targetDpi\":1600")!=r.body.npos);
 assert(request("GET","/api/pro-input?kind=3&scope=0").status=="400 Bad Request");
 const std::string key="kind=0&scope=1&vid=1133&pid=49271&transport=0";
 store.value.runtime.activeGame=3;std::strcpy(store.value.names.games[0].data(),"existing");
 assert(request("POST","/api/pro-input",settings(key)).status=="200 OK");assert(store.value.runtime.activeGame==3&&!std::strcmp(store.value.names.games[0].data(),"existing"));
 r=request("GET","/api/pro-input?"+key);assert(r.body.find("\"effectiveQ16\":131072")!=r.body.npos&&r.body.find("\"fullScale\":48")!=r.body.npos);
 assert(request("GET","/api/pro-input?kind=0&scope=1&vid=1133&pid=49271&transport=2").body.find("\"sourceDpi\":0")!=std::string::npos);
 store.failSave=true;assert(request("POST","/api/pro-reset",key).status=="500 Internal Server Error");assert(store.value.proInput.devices[0].enabled);
 assert(request("POST","/api/pro-game-context","enabled=1").status=="500 Internal Server Error"&&!store.value.proInput.gameContextInactive);store.failSave=false;
 assert(request("POST","/api/pro-game-context","enabled=1").status=="200 OK"&&store.persisted.proInput.gameContextInactive);
 assert(request("GET","/api/pro-profiles").body.find("\"gameContextInactive\":1")!=std::string::npos);
 assert(request("POST","/api/pro-desktop","enabled=0").status=="200 OK"&&!store.persisted.proInput.gameContextInactive);
 assert(request("POST","/api/pro-game-context","enabled=2").status=="400 Bad Request");
 auto invalid=settings();invalid.replace(invalid.find("processingHz=1000"),17,"processingHz=8000");assert(request("POST","/api/pro-input",invalid).status=="400 Bad Request");
 assert(request("POST","/api/pro-input",settings()+"%00").status=="400 Bad Request");
 const auto identity="index="+std::to_string(mouse->index)+"&generation="+std::to_string(mouse->generation);
 assert(request("POST","/api/pro-calibrate/start",identity).status=="200 OK");processor.processMouse(*mouse,{3150,0},{});
 r=request("POST","/api/pro-calibrate/finish",identity+"&mm=100");assert(r.status=="200 OK"&&r.body.find("\"sourceDpi\":800")!=r.body.npos);assert(store.value.proInput.defaults[0].sourceDpi==0);
 assert(request("POST","/api/pro-calibrate/finish",identity+"&mm=100").status=="409 Conflict");request("POST","/api/pro-calibrate/start",identity);registry.disconnect(*mouse);auto replacement=registry.connectUsb({1,0},0x046d,0xc077,ProtocolKind::HidMouse);assert(replacement&&*replacement!=*mouse);assert(request("POST","/api/pro-calibrate/finish",identity+"&mm=100").status=="409 Conflict");
 assert(request("POST","/api/pro-reset",key).status=="200 OK"&&!store.value.proInput.devices[0].enabled);
 for(unsigned i=0;i<16;++i)assert(request("POST","/api/pro-input",settings("kind=0&scope=1&vid="+std::to_string(i)+"&pid=1&transport=0")).status=="200 OK");
 assert(request("POST","/api/pro-input",settings("kind=0&scope=1&vid=17&pid=1&transport=0")).status=="409 Conflict");assert(request("GET","/api/pro-profiles").body.size()<1800);
 for(unsigned i=3;i<=12;++i)assert(registry.connectUsb({static_cast<std::uint8_t>(i),0},65535,65535,ProtocolKind::HidMouse));
 for(unsigned i=0;i<12;++i){const auto* d=registry.at(i);assert(d);processor.setCapabilities(d->id,7);auto& s=const_cast<ProInputStats&>(processor.stats(i));s.reports=UINT64_MAX;s.reportHz=s.lastProcessUs=s.maxProcessUs=s.lastShapeUs=s.maxShapeUs=UINT32_MAX;s.lastReportUs=now;s.rawX=s.rawY=s.outputX=s.outputY=INT32_MIN;s.remainderX=s.remainderY=-65535;}
 r=request("GET","/api/pro-devices");assert(r.status=="200 OK"&&r.body.size()<5100&&r.body.size()+640<5840);assert(r.body.substr(r.body.size()-2)=="]}");
}
