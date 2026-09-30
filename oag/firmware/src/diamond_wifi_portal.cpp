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

#include "oag/config/controller_calibration.h"
#include "oag/config/diamond_config.h"
#include "oag/firmware/diamond_config_store.h"
#include "oag/firmware/output_profile_selector.h"

namespace {

using oag::firmware::DiamondWifiPortal;

dhcp_server_t gDhcpServer {};
dns_server_t gDnsServer {};
tcp_pcb* gHttpListener = nullptr;
DiamondWifiPortal* gPortal = nullptr;

constexpr std::size_t kHttpBufferBytes = 3072;
constexpr std::size_t kHttpClientSlots = 2;

struct HttpClientState {
    tcp_pcb* client = nullptr;
    std::size_t used = 0;
    bool inUse = false;
    std::array<char, kHttpBufferBytes> data {};
};
std::array<HttpClientState, kHttpClientSlots> gClients {};

constexpr char kDashboardHtml[] = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>OAG ABO GEMI</title><link rel=stylesheet href=/app.css></head><body>
<header><h1>OAG ABO GEMI</h1><span>Live Controller Lab & Profiles</span></header><main>
<section class=card><h2>Controller Tester & Anti-Drift</h2><label>Controller</label><select id=ci></select><div id=conn class=status>Waiting for controller...</div>
<div class=pads><div><h3>LEFT STICK</h3><div class=pad><i id=ldot></i></div><div id=lraw class=mono></div><label>Deadzone <b id=ldv></b></label><input id=ld type=range min=0 max=8000 step=50></div>
<div><h3>RIGHT STICK</h3><div class=pad><i id=rdot></i></div><div id=rraw class=mono></div><label>Deadzone <b id=rdv></b></label><input id=rd type=range min=0 max=8000 step=50></div></div>
<div class=trigs><span>LT <b id=lt>0</b></span><span>RT <b id=rt>0</b></span></div><div id=btns class=mono>Buttons: —</div>
<button id=auto class=accent>AUTO CENTER & DRIFT</button><p class=hint>Release both sticks, then press Auto. Keep them untouched until sampling finishes.</p>
<button id=savecal>SAVE CALIBRATION</button><button id=disable class=ghost>DISABLE ANTI-DRIFT</button><div id=calmsg class=msg></div></section>

<section class=card><h2>Games & Weapons</h2><label>Game Slot / Name</label><select id=gs></select><input id=gn maxlength=32 placeholder="Type game name, e.g. Blood Strike"><button id=sg>SAVE GAME NAME</button>
<label>Weapon in selected game</label><select id=ws></select><input id=wn maxlength=32 placeholder="Type weapon name, e.g. AKM"><button id=sw>SAVE WEAPON NAME</button><div id=nmsg class=msg></div></section>

<section class=card><h2>Combos</h2><label>Combo Slot / Name</label><select id=cs></select><input id=cn maxlength=32 placeholder="Type combo name"><button id=sc>SAVE COMBO NAME</button></section>

<section class=card><h2>Active Profile</h2><div class=two><div><label>Game</label><select id=ag></select></div><div><label>Weapon</label><select id=aw></select></div></div><button id=sp>SAVE ACTIVE PROFILE</button></section>
<section class=card><h2>System</h2><p id=gen class=hint>Loading...</p><button id=play class=play>SAVE & PLAY</button><div id=pmsg class=msg></div></section>
</main><script src=/app.js></script></body></html>)HTML";

constexpr char kAppCss[] = R"CSS(*{box-sizing:border-box}body{margin:0;background:#070b12;color:#eef2ff;font:15px Arial,sans-serif}header{padding:20px;background:#111827;border-bottom:1px solid #263247}h1{margin:0;font-size:30px}header span,.hint{color:#94a3b8}main{max-width:820px;margin:auto;padding:14px}.card{background:#111827;border:1px solid #263247;border-radius:15px;padding:16px;margin-bottom:13px}h2{margin:0 0 12px}h3{text-align:center;font-size:13px;color:#cbd5e1}label{display:block;margin:10px 0 5px;color:#cbd5e1}input,select,button{width:100%;padding:11px;border-radius:9px;border:1px solid #334155;background:#0f172a;color:white}input[type=range]{padding:0}button{margin-top:10px;background:#e5e7eb;color:#111827;font-weight:800}.accent,.play{background:#86efac;color:#052e16}.ghost{background:#1f2937;color:#e5e7eb}.pads,.two{display:grid;grid-template-columns:1fr 1fr;gap:12px}.pad{width:min(38vw,220px);height:min(38vw,220px);margin:auto;border:2px solid #475569;border-radius:50%;position:relative;background:linear-gradient(#ffffff08,#ffffff02)}.pad:before,.pad:after{content:"";position:absolute;background:#334155}.pad:before{width:1px;height:100%;left:50%}.pad:after{height:1px;width:100%;top:50%}.pad i{position:absolute;width:18px;height:18px;border-radius:50%;background:#86efac;left:50%;top:50%;transform:translate(-50%,-50%);z-index:2}.mono{font:12px monospace;color:#94a3b8;text-align:center;margin:7px}.trigs{display:flex;justify-content:space-around;margin:12px}.status,.msg{margin:9px 0;color:#86efac}.bad{color:#fca5a5}@media(max-width:520px){.card{padding:13px}.pad{width:38vw;height:38vw}.two{grid-template-columns:1fr 1fr}})CSS";

constexpr char kAppJs[] = R"JS(const $=x=>document.getElementById(x);let cfg,games=[],weapons=[],combos=[],last=null,pcal=null;
function opts(e,n,p){e.innerHTML='';for(let i=1;i<=n;i++){let o=document.createElement('option');o.value=i;o.textContent=p+' '+i;e.appendChild(o)}}
opts($('ci'),8,'OAG ABO GEMI CONTROLLER');opts($('gs'),8,'Game Slot');opts($('ws'),24,'Weapon Slot');opts($('cs'),16,'Combo Slot');
const enc=d=>new URLSearchParams(d);async function post(p,d){let r=await fetch(p,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc(d)}),t=await r.text();if(!r.ok)throw Error(t);return t}
async function json(p){let r=await fetch(p,{cache:'no-store'});if(!r.ok)throw Error(await r.text());return r.json()}
function dot(id,x,y){let e=$(id),nx=Math.max(-1,Math.min(1,x/32767)),ny=Math.max(-1,Math.min(1,y/32767));e.style.left=(50+nx*45)+'%';e.style.top=(50+ny*45)+'%'}
function names(e,a,p){e.innerHTML='';a.forEach((n,i)=>{let o=document.createElement('option');o.value=i+1;o.textContent=n||p+' '+(i+1);e.appendChild(o)})}
async function load(){cfg=await json('/api/config');$('gen').textContent='Flash generation: '+cfg.generation;$('ld').value=cfg.controllers[0].leftDeadzone;$('rd').value=cfg.controllers[0].rightDeadzone;dz();games=(await json('/api/games')).names;combos=(await json('/api/combos')).names;names($('gs'),games,'+ Add Game Slot');names($('ag'),games,'Game Slot');names($('cs'),combos,'+ Add Combo Slot');$('ag').value=cfg.activeGame+1;await loadWeapons($('ag').value);$('aw').value=cfg.activeWeapon+1;showGameName();showComboName()}
async function loadWeapons(g){weapons=(await json('/api/weapons?game='+g)).names;names($('ws'),weapons,'+ Add Weapon Slot');names($('aw'),weapons,'Weapon Slot');showWeaponName()}
function dz(){$('ldv').textContent=$('ld').value;$('rdv').textContent=$('rd').value}
async function live(){try{last=await json('/api/live?slot='+$('ci').value);$('conn').textContent=last.connected?'CONNECTED - move the sticks':'No controller in this slot';$('conn').className=last.connected?'status':'status bad';dot('ldot',last.lx,last.ly);dot('rdot',last.rx,last.ry);$('lraw').textContent='X '+last.lx+'  Y '+last.ly;$('rraw').textContent='X '+last.rx+'  Y '+last.ry;$('lt').textContent=last.lt;$('rt').textContent=last.rt;$('btns').textContent='Buttons: 0x'+last.buttons.toString(16).toUpperCase()}catch(e){}setTimeout(live,100)}
$('ld').oninput=$('rd').oninput=dz;$('ci').onchange=()=>{pcal=null;let c=cfg.controllers[+$('ci').value-1];$('ld').value=c.leftDeadzone;$('rd').value=c.rightDeadzone;dz()};
$('auto').onclick=async()=>{let a=[];$('calmsg').textContent='Sampling... keep both sticks released';for(let i=0;i<30;i++){a.push(await json('/api/live?slot='+$('ci').value));await new Promise(r=>setTimeout(r,50))}if(!a.some(x=>x.connected)){ $('calmsg').textContent='Controller not detected';return}let av=k=>Math.round(a.reduce((s,x)=>s+x[k],0)/a.length),lx=av('lx'),ly=av('ly'),rx=av('rx'),ry=av('ry'),mx=(x,y,k1,k2)=>Math.max(...a.map(v=>Math.max(Math.abs(v[k1]-x),Math.abs(v[k2]-y)))),ld=Math.min(8000,Math.max(600,Math.ceil(mx(lx,ly,'lx','ly')+500))),rd=Math.min(8000,Math.max(600,Math.ceil(mx(rx,ry,'rx','ry')+500)));$('ld').value=ld;$('rd').value=rd;dz();pcal={lx,ly,rx,ry};$('calmsg').textContent='Center captured. Move sticks and adjust sliders if needed, then Save.'};
$('savecal').onclick=async()=>{if(!pcal){$('calmsg').textContent='Run Auto Center first';return}try{await post('/api/calibration',{slot:$('ci').value,enabled:1,lx:pcal.lx,ly:pcal.ly,rx:pcal.rx,ry:pcal.ry,ld:$('ld').value,rd:$('rd').value});$('calmsg').textContent='Calibration saved';await load()}catch(e){$('calmsg').textContent=e.message}};
$('disable').onclick=async()=>{try{await post('/api/calibration',{slot:$('ci').value,enabled:0,lx:0,ly:0,rx:0,ry:0,ld:0,rd:0});$('calmsg').textContent='Anti-drift disabled';await load()}catch(e){$('calmsg').textContent=e.message}};
function showGameName(){$('gn').value=games[+$('gs').value-1]||''}function showWeaponName(){$('wn').value=weapons[+$('ws').value-1]||''}function showComboName(){$('cn').value=combos[+$('cs').value-1]||''}
$('gs').onchange=async()=>{showGameName();await loadWeapons($('gs').value)};$('ws').onchange=showWeaponName;$('cs').onchange=showComboName;$('ag').onchange=async()=>{await loadWeapons($('ag').value)};
$('sg').onclick=async()=>{try{await post('/api/name',{type:'game',slot:$('gs').value,name:$('gn').value});games=(await json('/api/games')).names;names($('gs'),games,'+ Add Game Slot');names($('ag'),games,'Game Slot');$('nmsg').textContent='Game name saved'}catch(e){$('nmsg').textContent=e.message}};
$('sw').onclick=async()=>{try{await post('/api/name',{type:'weapon',game:$('gs').value,slot:$('ws').value,name:$('wn').value});await loadWeapons($('gs').value);$('nmsg').textContent='Weapon name saved'}catch(e){$('nmsg').textContent=e.message}};
$('sc').onclick=async()=>{try{await post('/api/name',{type:'combo',slot:$('cs').value,name:$('cn').value});combos=(await json('/api/combos')).names;names($('cs'),combos,'+ Add Combo Slot')}catch(e){alert(e.message)}};
$('sp').onclick=async()=>{try{await post('/api/profile',{game:$('ag').value,weapon:$('aw').value,enabled:1});$('nmsg').textContent='Active profile saved';await load()}catch(e){$('nmsg').textContent=e.message}};
$('play').onclick=async()=>{try{await post('/api/save-play',{});$('pmsg').textContent='Saved. Restarting into Gaming Mode...'}catch(e){$('pmsg').textContent=e.message}};
load().then(live).catch(e=>$('gen').textContent=e.message);
)JS";

static_assert(sizeof(kDashboardHtml) + 640u < TCP_SND_BUF);
static_assert(sizeof(kAppCss) + 640u < TCP_SND_BUF);
static_assert(sizeof(kAppJs) + 640u < TCP_SND_BUF);

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

std::size_t contentLength(const char* request) {
    const char* h=std::strstr(request,"Content-Length:"); if(!h)return 0;
    h+=15; while(*h==' ')++h; return static_cast<std::size_t>(std::strtoul(h,nullptr,10));
}

bool formValue(const char* body,const char* key,char* out,std::size_t cap) {
    if(!body||!key||!out||cap==0)return false; out[0]='\0'; const std::size_t kl=std::strlen(key);
    const char* c=body;
    while(*c){
        if((c==body||c[-1]=='&')&&std::strncmp(c,key,kl)==0&&c[kl]=='='){
            c+=kl+1; std::size_t w=0;
            while(*c&&*c!='&'){
                char ch=*c++;
                if(ch=='+')ch=' ';
                if(ch=='%'&&c[0]&&c[1]){
                    auto hx=[](char h)->int{if(h>='0'&&h<='9')return h-'0';if(h>='a'&&h<='f')return h-'a'+10;if(h>='A'&&h<='F')return h-'A'+10;return -1;};
                    int hi=hx(c[0]),lo=hx(c[1]); if(hi<0||lo<0)return false; ch=static_cast<char>((hi<<4)|lo);c+=2;
                }
                if(ch=='\0'||ch=='\r'||ch=='\n'||w+1>=cap)return false; out[w++]=ch;
            }
            out[w]='\0';return true;
        }
        const char* n=std::strchr(c,'&');if(!n)break;c=n+1;
    }
    return false;
}

bool parseUnsigned(const char* b,const char* k,std::uint32_t lo,std::uint32_t hi,std::uint32_t& v){
    char x[16]{};if(!formValue(b,k,x,sizeof(x))||!x[0])return false;char* e=nullptr;unsigned long p=std::strtoul(x,&e,10);
    if(e==x||*e||p<lo||p>hi)return false;v=static_cast<std::uint32_t>(p);return true;
}
bool parseSigned(const char* b,const char* k,std::int32_t lo,std::int32_t hi,std::int32_t& v){
    char x[16]{};if(!formValue(b,k,x,sizeof(x))||!x[0])return false;char* e=nullptr;long p=std::strtol(x,&e,10);
    if(e==x||*e||p<lo||p>hi)return false;v=static_cast<std::int32_t>(p);return true;
}

bool safeName(const char* s){if(!s)return false;std::size_t n=std::strlen(s);if(n>=oag::kDiamondDisplayNameBytes)return false;for(std::size_t i=0;i<n;i++)if(static_cast<unsigned char>(s[i])<0x20)return false;return true;}
void jsonString(char*& out,std::size_t& left,const char* s){
    if(left<3)return;*out++='"';--left;
    while(*s&&left>3){unsigned char c=static_cast<unsigned char>(*s++);if(c=='"'||c=='\\'){*out++='\\';--left;}*out++=static_cast<char>(c);--left;}
    *out++='"';*out='\0';--left;
}
void sendNames(tcp_pcb* client,const auto& names){
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
    if(packet->tot_len>avail){tcp_recved(client,packet->tot_len);pbuf_free(packet);sendResponse(client,"413 Payload Too Large","text/plain","Request too large");releaseClient(s);return ERR_OK;}
    pbuf_copy_partial(packet,s->data.data()+s->used,packet->tot_len,0);s->used+=packet->tot_len;s->data[s->used]='\0';tcp_recved(client,packet->tot_len);pbuf_free(packet);
    const char* he=std::strstr(s->data.data(),"\r\n\r\n");if(!he)return ERR_OK;std::size_t hb=static_cast<std::size_t>(he-s->data.data())+4u;
    if(s->used<hb+contentLength(s->data.data()))return ERR_OK;tcp_arg(client,nullptr);
    if(gPortal)gPortal->handleHttpRequest(client,s->data.data(),s->used);else sendResponse(client,"503 Service Unavailable","text/plain","Portal unavailable");releaseClient(s);return ERR_OK;
}
err_t httpAccept(void*,tcp_pcb* client,err_t error){
    if(error!=ERR_OK||!client)return error;for(auto& s:gClients)if(!s.inUse){s.inUse=true;s.client=client;s.used=0;s.data.fill('\0');tcp_arg(client,&s);tcp_recv(client,httpReceive);return ERR_OK;}
    sendResponse(client,"503 Service Unavailable","text/plain","Portal busy");return ERR_OK;
}

} // namespace

namespace oag::firmware {

bool DiamondWifiPortal::start(DiamondConfigStore& store,const oag::UniversalGamepadState* states,std::size_t count){
    if(started_)return true;store_=&store;liveStates_=states;liveStateCount_=count;if(!store_->load())return false;if(cyw43_arch_init()!=0)return false;
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
void DiamondWifiPortal::task(){if(playRebootPending_&&time_us_64()>=playRebootAtUs_){playRebootPending_=false;requestOutputProfile(OutputProfileId::Pc);}}

void DiamondWifiPortal::handleHttpRequest(void* rawClient,const char* request,std::size_t){
    auto* client=static_cast<tcp_pcb*>(rawClient);if(!client||!request||!store_)return;char method[8]{},path[96]{};
    if(std::sscanf(request,"%7s %95s",method,path)!=2){sendResponse(client,"400 Bad Request","text/plain","Bad request");return;}
    const char* body=std::strstr(request,"\r\n\r\n");body=body?body+4:"";auto& pc=store_->config();auto& runtime=pc.runtime;

    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/")){sendResponse(client,"200 OK","text/html; charset=utf-8",kDashboardHtml);return;}
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/app.css")){sendResponse(client,"200 OK","text/css; charset=utf-8",kAppCss);return;}
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/app.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kAppJs);return;}

    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/live?slot=",15)){
        std::uint32_t slot=static_cast<std::uint32_t>(std::strtoul(path+15,nullptr,10));if(slot<1||slot>liveStateCount_){sendResponse(client,"400 Bad Request","text/plain","Invalid slot");return;}
        const auto& s=liveStates_[slot-1];char j[384]{};std::snprintf(j,sizeof(j),"{\"connected\":%s,\"lx\":%ld,\"ly\":%ld,\"rx\":%ld,\"ry\":%ld,\"lt\":%lu,\"rt\":%lu,\"buttons\":%llu}",s.connected?"true":"false",static_cast<long>(s.lx),static_cast<long>(s.ly),static_cast<long>(s.rx),static_cast<long>(s.ry),static_cast<unsigned long>(s.leftTrigger),static_cast<unsigned long>(s.rightTrigger),static_cast<unsigned long long>(s.buttons));sendResponse(client,"200 OK","application/json",j);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/games")){sendNames(client,pc.names.games);return;}
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/combos")){sendNames(client,pc.names.combos);return;}
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/weapons?game=",18)){
        std::uint32_t g=static_cast<std::uint32_t>(std::strtoul(path+18,nullptr,10));if(g<1||g>oag::kDiamondGameSlots){sendResponse(client,"400 Bad Request","text/plain","Invalid game");return;}sendNames(client,pc.names.weapons[g-1]);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/config")){
        char j[2048]{};int used=std::snprintf(j,sizeof(j),"{\"generation\":%lu,\"activeGame\":%u,\"activeWeapon\":%u,\"controllers\":[",static_cast<unsigned long>(store_->generation()),runtime.activeGame,runtime.activeWeapon);
        for(std::size_t i=0;i<runtime.controllers.size();++i){const auto& c=runtime.controllers[i];used+=std::snprintf(j+used,sizeof(j)-static_cast<std::size_t>(used),"%s{\"enabled\":%s,\"leftDeadzone\":%lu,\"rightDeadzone\":%lu}",i?",":"",c.enabled?"true":"false",static_cast<unsigned long>(c.left.deadzone),static_cast<unsigned long>(c.right.deadzone));}
        std::snprintf(j+used,sizeof(j)-static_cast<std::size_t>(used),"]}");sendResponse(client,"200 OK","application/json",j);return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/calibration")){
        std::uint32_t slot=0,en=0,ld=0,rd=0;std::int32_t lx=0,ly=0,rx=0,ry=0;
        if(!parseUnsigned(body,"slot",1,oag::kDiamondControllerSlots,slot)||!parseUnsigned(body,"enabled",0,1,en)||!parseSigned(body,"lx",-32768,32767,lx)||!parseSigned(body,"ly",-32768,32767,ly)||!parseSigned(body,"rx",-32768,32767,rx)||!parseSigned(body,"ry",-32768,32767,ry)||!parseUnsigned(body,"ld",0,32767,ld)||!parseUnsigned(body,"rd",0,32767,rd)){sendResponse(client,"400 Bad Request","text/plain","Invalid calibration");return;}
        auto old=runtime.controllers[slot-1];auto& c=runtime.controllers[slot-1];c.enabled=en!=0;c.left.centerX=lx;c.left.centerY=ly;c.right.centerX=rx;c.right.centerY=ry;c.left.deadzone=ld;c.right.deadzone=rd;
        if(!store_->save()){c=old;sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}sendResponse(client,"200 OK","text/plain","Calibration saved");return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/name")){
        char type[12]{},name[oag::kDiamondDisplayNameBytes]{};std::uint32_t slot=0,game=0;
        if(!formValue(body,"type",type,sizeof(type))||!formValue(body,"name",name,sizeof(name))||!safeName(name)||!parseUnsigned(body,"slot",1,24,slot)){sendResponse(client,"400 Bad Request","text/plain","Invalid name");return;}
        char* target=nullptr;
        if(!std::strcmp(type,"game")&&slot<=oag::kDiamondGameSlots)target=pc.names.games[slot-1].data();
        else if(!std::strcmp(type,"combo")&&slot<=oag::kDiamondComboSlots)target=pc.names.combos[slot-1].data();
        else if(!std::strcmp(type,"weapon")&&parseUnsigned(body,"game",1,oag::kDiamondGameSlots,game)&&slot<=oag::kDiamondWeaponSlotsPerGame)target=pc.names.weapons[game-1][slot-1].data();
        if(!target){sendResponse(client,"400 Bad Request","text/plain","Invalid name target");return;}std::memset(target,0,oag::kDiamondDisplayNameBytes);std::memcpy(target,name,std::strlen(name));
        if(!store_->save()){sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}sendResponse(client,"200 OK","text/plain","Name saved");return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/profile")){
        std::uint32_t g=0,w=0,en=0;if(!parseUnsigned(body,"game",1,oag::kDiamondGameSlots,g)||!parseUnsigned(body,"weapon",1,oag::kDiamondWeaponSlotsPerGame,w)||!parseUnsigned(body,"enabled",0,1,en)){sendResponse(client,"400 Bad Request","text/plain","Invalid profile");return;}
        auto og=runtime.activeGame,ow=runtime.activeWeapon;bool oe=runtime.games[g-1].enabled;runtime.activeGame=static_cast<std::uint16_t>(g-1);runtime.activeWeapon=static_cast<std::uint16_t>(w-1);runtime.games[g-1].enabled=en!=0;
        if(!store_->save()){runtime.activeGame=og;runtime.activeWeapon=ow;runtime.games[g-1].enabled=oe;sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}sendResponse(client,"200 OK","text/plain","Profile saved");return;
    }
    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/save-play")){if(!store_->save()){sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}sendResponse(client,"200 OK","text/plain","Saved");schedulePlayReboot(1200);return;}
    sendResponse(client,"404 Not Found","text/plain","Not found");
}

} // namespace oag::firmware
