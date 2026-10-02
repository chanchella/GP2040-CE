#include "oag/firmware/diamond_wifi_portal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "dhcpserver.h"
#include "dnsserver.h"
}

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "oag/config/diamond_config.h"
#include "oag/firmware/diamond_config_store.h"
#include "oag/firmware/diamond_game_library_store.h"
#include "oag/firmware/output_profile_selector.h"

namespace {

using oag::firmware::DiamondWifiPortal;

dhcp_server_t gDhcpServer {};
dns_server_t gDnsServer {};
tcp_pcb* gHttpListener = nullptr;
DiamondWifiPortal* gPortal = nullptr;

constexpr std::size_t kHttpBufferBytes = 3072;
// Mobile browsers may preload assets in parallel. OAG V11 also loads the
// dependent combo modules sequentially with retry. Static asset routing also\n// tolerates optional query strings, preventing cache-busting URLs from 404ing.
// Config Mode is isolated from Gaming Mode, so reserve enough short-lived
// request slots here without touching the frozen controller path.
constexpr std::size_t kHttpClientSlots = 12;
static_assert(kHttpClientSlots >= 12, "OAG portal keeps headroom while combo JS is loaded sequentially with retry");

struct HttpClientState {
    tcp_pcb* client = nullptr;
    std::size_t used = 0;
    bool inUse = false;
    std::array<char, kHttpBufferBytes> data {};
};
std::array<HttpClientState, kHttpClientSlots> gClients {};

#include "oag_smart_assets.h"

void sendResponse(tcp_pcb* client,const char* status,const char* contentType,const char* body) {
    if (!client || !body) return;
    char header[640] {};
    const std::size_t bodyLength = std::strlen(body);
    const int headerLength = std::snprintf(header,sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nCache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n"
        "Content-Security-Policy: default-src 'self'; style-src 'self'; script-src 'self'; frame-ancestors 'none'\r\n"
        "Connection: close\r\nContent-Length: %u\r\n\r\n",
        status,contentType,static_cast<unsigned>(bodyLength));
    if (headerLength>0 && static_cast<std::size_t>(headerLength)<sizeof(header)) {
        if (tcp_write(client,header,static_cast<u16_t>(headerLength),TCP_WRITE_FLAG_COPY)==ERR_OK &&
            tcp_write(client,body,static_cast<u16_t>(bodyLength),TCP_WRITE_FLAG_COPY)==ERR_OK) tcp_output(client);
    }
    tcp_close(client);
}

bool staticPath(const char* requested,const char* asset){
    if(!requested||!asset)return false;
    const std::size_t n=std::strlen(asset);
    return std::strncmp(requested,asset,n)==0&&(requested[n]=='\0'||requested[n]=='?');
}

std::size_t contentLength(const char* request) {
    const char* h=std::strstr(request,"Content-Length:"); if(!h)return 0;
    h+=15; while(*h==' ')++h; return static_cast<std::size_t>(std::strtoul(h,nullptr,10));
}

#include "portal_form_parser.h"

bool safeName(const char* s){if(!s)return false;std::size_t n=std::strlen(s);if(n>=oag::kDiamondDisplayNameBytes)return false;for(std::size_t i=0;i<n;i++)if(static_cast<unsigned char>(s[i])<0x20)return false;return true;}
void jsonString(char*& out,std::size_t& left,const char* s){
    if(left<3)return;*out++='"';--left;
    while(*s&&left>3){unsigned char c=static_cast<unsigned char>(*s++);if(c=='"'||c=='\\'){*out++='\\';--left;}*out++=static_cast<char>(c);--left;}
    *out++='"';*out='\0';--left;
}
template <typename T>
void sendNames(tcp_pcb* client,const T& names){
    char json[4096]{};char* p=json;std::size_t left=sizeof(json);int n=std::snprintf(p,left,"{\"names\":[");p+=n;left-=static_cast<std::size_t>(n);
    for(std::size_t i=0;i<names.size();++i){if(i&&left>1){*p++=',';--left;}jsonString(p,left,names[i].data());}
    if(left>3){std::snprintf(p,left,"]}");}sendResponse(client,"200 OK","application/json; charset=utf-8",json);
}

void releaseClient(HttpClientState* s){if(!s)return;s->client=nullptr;s->used=0;s->inUse=false;s->data.fill('\0');}
err_t httpReceive(void* raw,tcp_pcb* client,pbuf* packet,err_t error){
    auto* s=static_cast<HttpClientState*>(raw);
    if(error!=ERR_OK||!s){if(packet)pbuf_free(packet);if(client)tcp_close(client);releaseClient(s);return ERR_OK;}
    if(!packet){tcp_close(client);releaseClient(s);return ERR_OK;}
    std::size_t avail=s->data.size()-s->used-1u;
    if(packet->tot_len>avail){tcp_recved(client,packet->tot_len);pbuf_free(packet);sendResponse(client,"413 Payload Too Large","text/plain; charset=utf-8","Request too large");releaseClient(s);return ERR_OK;}
    pbuf_copy_partial(packet,s->data.data()+s->used,packet->tot_len,0);s->used+=packet->tot_len;s->data[s->used]='\0';tcp_recved(client,packet->tot_len);pbuf_free(packet);
    const char* he=std::strstr(s->data.data(),"\r\n\r\n");if(!he)return ERR_OK;std::size_t hb=static_cast<std::size_t>(he-s->data.data())+4u;
    if(s->used<hb+contentLength(s->data.data()))return ERR_OK;tcp_arg(client,nullptr);
    if(gPortal)gPortal->handleHttpRequest(client,s->data.data(),s->used);else sendResponse(client,"503 Service Unavailable","text/plain; charset=utf-8","Portal unavailable");releaseClient(s);return ERR_OK;
}
err_t httpAccept(void*,tcp_pcb* client,err_t error){
    if(error!=ERR_OK||!client)return error;for(auto& s:gClients)if(!s.inUse){s.inUse=true;s.client=client;s.used=0;s.data.fill('\0');tcp_arg(client,&s);tcp_recv(client,httpReceive);return ERR_OK;}
    sendResponse(client,"503 Service Unavailable","text/plain; charset=utf-8","Portal busy");return ERR_OK;
}

} // namespace

namespace oag::firmware {

bool DiamondWifiPortal::start(DiamondConfigStore& store, DiamondGameLibraryStore& games,bool radioReady){
    if(started_)return true;store_=&store;games_=&games;liveMode_=radioReady;if(!radioReady && cyw43_arch_init()!=0)return false;
    cyw43_arch_enable_ap_mode("OAG ABO GEMI",nullptr,CYW43_AUTH_OPEN);
    ip_addr_t gateway{},mask{};
#if LWIP_IPV6
    gateway.u_addr.ip4.addr=PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);mask.u_addr.ip4.addr=PP_HTONL(CYW43_DEFAULT_IP_MASK);
#else
    gateway.addr=PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);mask.addr=PP_HTONL(CYW43_DEFAULT_IP_MASK);
#endif
    cyw43_arch_lwip_begin();dhcp_server_init(&gDhcpServer,&cyw43_state.netif[CYW43_ITF_AP],&gateway,&mask);dns_server_init(&gDnsServer,&cyw43_state.netif[CYW43_ITF_AP],&gateway);
    gHttpListener=tcp_new_ip_type(IPADDR_TYPE_V4);if(!gHttpListener){cyw43_arch_lwip_end();return false;}
    if(tcp_bind(gHttpListener,IP_ANY_TYPE,80)!=ERR_OK){tcp_close(gHttpListener);gHttpListener=nullptr;cyw43_arch_lwip_end();return false;}
    gHttpListener=tcp_listen(gHttpListener);if(!gHttpListener){cyw43_arch_lwip_end();return false;}gPortal=this;tcp_accept(gHttpListener,httpAccept);cyw43_arch_lwip_end();started_=true;return true;
}
void DiamondWifiPortal::schedulePlayReboot(std::uint32_t ms){playRebootAtUs_=time_us_64()+static_cast<std::uint64_t>(ms)*1000ull;playRebootPending_=true;}
void DiamondWifiPortal::task(){if(smart_){cyw43_arch_lwip_begin();smart_->task();cyw43_arch_lwip_end();}if(playRebootPending_&&time_us_64()>=playRebootAtUs_){playRebootPending_=false;requestOutputProfile(OutputProfileId::Pc);}}


void DiamondWifiPortal::handleHttpRequest(void* rawClient,const char* request,std::size_t){
    auto* client=static_cast<tcp_pcb*>(rawClient);if(!client||!request||!store_)return;char method[8]{},path[96]{};
    if(std::sscanf(request,"%7s %95s",method,path)!=2){sendResponse(client,"400 Bad Request","text/plain; charset=utf-8","الطلب مش مفهوم");return;}
    const char* body=std::strstr(request,"\r\n\r\n");body=body?body+4:"";auto& pc=store_->config();auto& runtime=pc.runtime;
    if(smart_ && smart_->handle(method,path,body)){
        const auto status=smart_->status();
        const char* reason=status==200?"200 OK":status==202?"202 Accepted":status==400?"400 Bad Request":status==409?"409 Conflict":status==404?"404 Not Found":"500 Internal Server Error";
        sendResponse(client,reason,"application/json; charset=utf-8",smart_->response());return;
    }
    if(!std::strncmp(path,"/api/pro-",9) || !std::strncmp(path,"/api/combo",10) || !std::strncmp(path,"/api/recoil",11)) {
        sendResponse(client,"410 Gone","text/plain; charset=utf-8","الإعدادات القديمة اتلغت. افتح أدوات OAG الجديدة من الصفحة الرئيسية"); return;
    }
    if(!std::strcmp(method,"GET") && !std::strcmp(path,"/api/oag-input-auto")) {
        sendResponse(client,"200 OK","application/json; charset=utf-8","{\"automatic\":true,\"referenceDpi\":2400,\"processingHz\":1000,\"tickUs\":1000,\"hardwareDpiChanged\":false}"); return;
    }
    if(liveMode_ && !std::strcmp(method,"POST")){
        sendResponse(client,"409 Conflict","text/plain; charset=utf-8","أثناء اللعب عدّل الكومبو والسلاح من أدوات OAG. تسمية الألعاب وحفظ إعدادات البداية من وضع الإعدادات");return;
    }

    if(!games_){sendResponse(client,"503 Service Unavailable","text/plain; charset=utf-8","مكتبة ألعاب OAG مش متاحة");return;}
    const auto loadGame=[&](std::uint32_t oneBased)->bool{
        return oneBased>=1&&oneBased<=oag::kDiamondLibraryGameSlots&&
            games_->loadGame(oneBased-1,pc,scratchGame_);
    };

    if(!std::strcmp(method,"GET")) {
        const char* assetPath=!std::strcmp(path,"/")?"/oag-home.html":(!std::strcmp(path,"/oag-smart") || !std::strcmp(path,"/oag-weapons"))?"/oag.html":path;
        for(const auto& asset:kOagSmartAssets) if(staticPath(assetPath,asset.path)) {
            sendResponse(client,"200 OK",asset.mime,asset.body);return;
        }
    }

    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/games")){
        static std::array<std::array<char,oag::kDiamondDisplayNameBytes>,oag::kDiamondLibraryGameSlots> gameNames{};
        for(std::size_t i=0;i<gameNames.size();++i){
            if(!games_->loadGame(i,pc,scratchGame_)){sendResponse(client,"500 Internal Server Error","text/plain; charset=utf-8","قراءة مكتبة الألعاب ما نجحتش");return;}
            gameNames[i]=scratchGame_.gameName;
        }
        sendNames(client,gameNames);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/weapons?game=",18)){
        const auto g=static_cast<std::uint32_t>(std::strtoul(path+18,nullptr,10));
        if(!loadGame(g)){sendResponse(client,"400 Bad Request","text/plain; charset=utf-8","اختار لعبة صح");return;}
        sendNames(client,scratchGame_.weaponNames);return;
    }

    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/config")){
        char j[256]{};
        std::snprintf(j,sizeof(j),"{\"generation\":%lu,\"activeGame\":%u,\"activeWeapon\":%d}",
            static_cast<unsigned long>(store_->generation()),runtime.activeGame,
            runtime.activeWeapon==oag::kDiamondNoActiveWeapon?-1:static_cast<int>(runtime.activeWeapon));
        sendResponse(client,"200 OK","application/json",j);return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/name")){
        char type[12]{},name[oag::kDiamondDisplayNameBytes]{};std::uint32_t slot=0,game=0;
        if(!formValue(body,"type",type,sizeof(type))||!formValue(body,"name",name,sizeof(name))||!safeName(name)||!parseUnsigned(body,"slot",1,24,slot)){sendResponse(client,"400 Bad Request","text/plain; charset=utf-8","راجع الاسم ورقم الخانة");return;}

        if(!std::strcmp(type,"game")){
            game=slot;
            if(game<1||game>oag::kDiamondLibraryGameSlots||!loadGame(game)){sendResponse(client,"400 Bad Request","text/plain; charset=utf-8","رقم اللعبة مش صح");return;}
            std::memset(scratchGame_.gameName.data(),0,oag::kDiamondDisplayNameBytes);
            std::memcpy(scratchGame_.gameName.data(),name,std::strlen(name));
        }else{
            if(!parseUnsigned(body,"game",1,oag::kDiamondLibraryGameSlots,game)||!loadGame(game)){sendResponse(client,"400 Bad Request","text/plain; charset=utf-8","رقم اللعبة مش صح");return;}
            char* target=nullptr;
            if(!std::strcmp(type,"weapon")&&slot<=oag::kDiamondWeaponSlotsPerGame)target=scratchGame_.weaponNames[slot-1].data();
            if(!target){sendResponse(client,"400 Bad Request","text/plain; charset=utf-8","اختار اسم لعبة أو سلاح");return;}
            std::memset(target,0,oag::kDiamondDisplayNameBytes);std::memcpy(target,name,std::strlen(name));
        }
        if(!games_->saveGame(game-1,scratchGame_)){sendResponse(client,"500 Internal Server Error","text/plain; charset=utf-8","حفظ بيانات اللعبة ما نجحش");return;}
        sendResponse(client,"200 OK","text/plain; charset=utf-8","الاسم اتحفظ");return;
    }



    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/profile")){
        std::uint32_t g=0,w=0,en=0;
        if(!parseUnsigned(body,"game",1,oag::kDiamondLibraryGameSlots,g)||!parseUnsigned(body,"weapon",1,oag::kDiamondWeaponSlotsPerGame,w)||!parseUnsigned(body,"enabled",0,1,en)){
            sendResponse(client,"400 Bad Request","text/plain; charset=utf-8","راجع اللعبة والسلاح");return;
        }
        if(!games_->activate(g-1,pc)){sendResponse(client,"500 Internal Server Error","text/plain; charset=utf-8","تحميل اللعبة ما نجحش");return;}
        const auto oldGame=runtime.activeGame,oldWeapon=runtime.activeWeapon;
        const bool oldGameContextInactive=pc.proInput.gameContextInactive;
        pc.proInput.gameContextInactive=false;
        runtime.activeGame=static_cast<std::uint16_t>(g-1);
        runtime.activeWeapon=static_cast<std::uint16_t>(w-1);
        if(!store_->save()){
            pc.proInput.gameContextInactive=oldGameContextInactive;
            runtime.activeGame=oldGame;runtime.activeWeapon=oldWeapon;
            games_->activate(oldGame<oag::kDiamondLibraryGameSlots?oldGame:0,pc);
            sendResponse(client,"500 Internal Server Error","text/plain; charset=utf-8","الحفظ ما نجحش");return;
        }
        sendResponse(client,"200 OK","text/plain; charset=utf-8","اختيار اللعبة والسلاح اتحفظ");return;
    }
    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/save-play")){if(!store_->save()){sendResponse(client,"500 Internal Server Error","text/plain; charset=utf-8","الحفظ ما نجحش");return;}sendResponse(client,"200 OK","text/plain; charset=utf-8","اتحفظت");schedulePlayReboot(1200);return;}
    sendResponse(client,"404 Not Found","text/plain; charset=utf-8","الصفحة أو الطلب مش موجود");
}

} // namespace oag::firmware
