#include "oag/firmware/smart_combo_portal.h"
#include "hardware/flash.h"
#include "pico/btstack_flash_bank.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
extern "C" { alignas(4096) std::uint8_t oagTestFlash[4u * 1024u * 1024u]; }
asm(".global __flash_binary_end\n.set __flash_binary_end, oagTestFlash");
namespace {
unsigned writes = 0, erases = 0; bool partial = false;
constexpr unsigned legacyStart = PICO_FLASH_BANK_STORAGE_OFFSET - 2u*4u*4096u - 2u*6u*4096u - 20u*2u*4u*4096u;
constexpr unsigned smartStart = legacyStart - oag::kOagSmartGames*2u*oag::kOagSmartSlotBytes;
std::string status, type, response;
oag::firmware::OagSmartComboStore store, reboot;
oag::firmware::OagSmartComboPortal portal;
oag::OagSmartProgram p; oag::OagSmartComboEngine engine;
std::uint64_t now = 0;
void reply(void*, const char* s, const char* t, const char* body) {status=s;type=t;response=body;}
std::string hex(const void* ptr, std::size_t len) {std::string out;const auto* b=static_cast<const unsigned char*>(ptr);const char* digits="0123456789abcdef";for(std::size_t i=0;i<len;i++){out+=digits[b[i]>>4];out+=digits[b[i]&15];}return out;}
void call(const char* method, const char* path, const std::string& body="") {assert(portal.handle(nullptr,method,path,body.c_str(),now+=1000,reply));}
unsigned jsonNumber(const char* name) {auto at=response.find(std::string("\"")+name+"\":");assert(at!=std::string::npos);return std::stoul(response.substr(at+std::strlen(name)+3));}
void initialize() {std::memset(oagTestFlash,0xff,sizeof(oagTestFlash));std::memset(oagTestFlash+legacyStart,0xa5,sizeof(oagTestFlash)-legacyStart);assert(store.ready());assert(store.activate(0));portal.attach(store,engine);p.enabled=1;std::strcpy(p.name.data(),"OAG test");p.branches[0].conditions[0].targets[0]={6,oag::OagSmartTargetKind::Pad,100};p.branches[0].actions[0].targets[0]={4,oag::OagSmartTargetKind::Pad,100};}
unsigned begin() {call("POST","/api/oag-smart/begin","game=1&slot=1&revision="+std::to_string(store.revision())+"&header="+hex(&p,36));assert(status=="200 OK");return jsonNumber("token");}
void stage(unsigned token) {call("POST","/api/oag-smart/part","token="+std::to_string(token)+"&branch=0&hex="+hex(&p.branches[0],792));assert(status=="200 OK");}
void tests() {
    assert(writes==0&&erases==0);call("GET","/api/oag-smart/program?game=1&slot=1");assert(status=="200 OK"&&response.size()<1800);
    auto token=begin();call("POST","/api/oag-smart/preview","token="+std::to_string(token));assert(status=="400 Bad Request"&&!store.dirty());
    call("POST","/api/oag-smart/part","token="+std::to_string(token)+"&branch=0&hex=zz");assert(status=="400 Bad Request"&&!store.dirty());
    stage(token);call("POST","/api/oag-smart/preview","token="+std::to_string(token));assert(status=="200 OK"&&store.dirty()&&writes==0&&erases==0);
    call("POST","/api/oag-smart/preview","token="+std::to_string(token));assert(status=="409 Conflict");
    call("POST","/api/oag-smart/save","game=1&revision=0");assert(status=="409 Conflict"&&writes==0);
    call("GET","/api/oag-smart/program?game=2&slot=1");assert(status=="409 Conflict"&&store.editorGame()==0);
    call("POST","/api/oag-smart/test","slot=1&branch=0&revision="+std::to_string(store.revision()));assert(status=="200 OK"&&response.find("simulation")!=std::string::npos);
    call("GET","/api/oag-smart/test-state?t=1000");assert(status=="400 Bad Request");call("GET","/api/oag-smart/test-state?t=20");assert(status=="200 OK");
    call("POST","/api/oag-smart/save","game=1&revision="+std::to_string(store.revision()));assert(status=="200 OK"&&writes==2&&erases==1&&!store.dirty());
    assert(reboot.activate(0)&&reboot.active().programs[0].enabled);assert(reboot.activate(1)&&!reboot.active().programs[0].enabled);assert(reboot.activate(0)&&reboot.active().programs[0].name[0]=='O');
    call("POST","/api/oag-smart/save","game=1&revision="+std::to_string(store.revision()));assert(writes==2);
    std::strcpy(p.name.data(),"OAG new");token=begin();stage(token);call("POST","/api/oag-smart/preview","token="+std::to_string(token));partial=true;
    call("POST","/api/oag-smart/save","game=1&revision="+std::to_string(store.revision()));assert(status=="500 Internal Server Error"&&store.dirty());partial=false;
    assert(reboot.activate(1)&&reboot.activate(0));assert(std::string(reboot.active().programs[0].name.data())=="OAG test");
    call("POST","/api/oag-smart/save","game=1&revision="+std::to_string(store.revision()));assert(status=="200 OK");
    assert(reboot.activate(1)&&reboot.activate(0));assert(std::string(reboot.active().programs[0].name.data())=="OAG new");
    auto before=writes;assert(store.preview(0,1,p));assert(store.discard(0)&&!store.editor().programs[1].enabled&&writes==before);
    assert(store.activate(19)&&!store.active().programs[0].enabled);assert(store.preview(19,0,p)&&store.save(19));assert(reboot.activate(19)&&reboot.active().programs[0].enabled);assert(store.activate(0)&&std::string(store.active().programs[0].name.data())=="OAG new");
    assert(std::all_of(oagTestFlash+legacyStart,oagTestFlash+sizeof(oagTestFlash),[](auto v){return v==0xa5;}));
    std::cout<<"OAG_SMART_FLASH_PORTAL=PASS writes="<<writes<<" legacy_untouched=true smart_start="<<smartStart<<"\n";
}
}
void flash_range_erase(std::uint32_t at,std::size_t len) {assert(at>=smartStart&&at+len<=legacyStart&&at%4096==0);++erases;std::memset(oagTestFlash+at,0xff,len);}
void flash_range_program(std::uint32_t at,const std::uint8_t* data,std::size_t len) {assert(at>=smartStart&&at+len<=legacyStart&&at%256==0);++writes;if(partial)len=4096;for(std::size_t i=0;i<len;i++){assert((oagTestFlash[at+i]&data[i])==data[i]);oagTestFlash[at+i]&=data[i];}}
int flash_safe_execute(void(*fn)(void*),void* data,std::uint32_t) {fn(data);return 0;}
int main(int argc,char**) {initialize();if(argc==1){tests();return 0;}std::string line;while(std::getline(std::cin,line)){std::istringstream in(line);std::string method,path,body;in>>method>>path;std::getline(in,body);if(!body.empty())body.erase(0,1);if(path=="/test/flash-count"){status="200 OK";type="application/json";response="{\"writes\":"+std::to_string(writes)+"}";}else call(method.c_str(),path.c_str(),body);std::cout<<status<<'\t'<<type<<'\t'<<response<<std::endl;}}
