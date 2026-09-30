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
<header><h1>OAG ABO GEMI</h1><span>Controller Lab • Smart Anti-Drift • Profiles</span></header><main>
<section class=card><h2>OAG Games & Weapons</h2><label>OAG Game</label><select id=gs></select><input id=gn maxlength=32 placeholder="Type your OAG game name"><button id=sg>ADD OAG GAME 1</button>
<label>OAG Weapon for selected game</label><select id=ws></select><input id=wn maxlength=32 placeholder="Type your OAG weapon name"><button id=sw>ADD OAG WEAPON 1</button>
<h3>OAG WEAPON RECOIL</h3><p class=hint>Horizontal: -2.00 Left / +2.00 Right • Vertical: -2.00 Up / +2.00 Down</p><div class=two><div><label>Horizontal (-2.00 to +2.00)</label><input id=rh type=number min=-2 max=2 step=.01 value=0></div><div><label>Vertical (-2.00 to +2.00)</label><input id=rv type=number min=-2 max=2 step=.01 value=0></div></div>
<label>Recoil Tick (ms)</label><input id=rt type=number min=1 max=1000 step=1 value=40><button id=sr>SAVE OAG WEAPON RECOIL</button><div id=nmsg class=msg></div></section>

<section class=card><h2>OAG Combos</h2><label>OAG Combo</label><select id=cs></select><input id=cn maxlength=32 placeholder="Type your OAG combo name"><button id=sc>ADD OAG COMBO 1</button>
<h3>OAG COMBO PROGRAM</h3><div class=two><div><label>Activation</label><select id=ca><option value=0>While Trigger Held</option><option value=1>Press Once</option><option value=2>Toggle On / Off</option></select></div><div><label>Sequence Repeat</label><select id=car><option value=0>Run Once</option><option value=1>Auto Repeat</option></select></div></div>
<label><input id=cpass type=checkbox checked> Keep original trigger working in game</label><label><input id=crel type=checkbox checked> Cancel when trigger is released</label><label><input id=cagain type=checkbox> Cancel when trigger is pressed again</label>
<h3>TRIGGERS</h3><div class=two><div><label>Controller Action</label><select id=ctl></select></div><div><label>Keyboard Key (example: J)</label><input id=ctk maxlength=12 placeholder="None"></div></div><label>Mouse Trigger</label><select id=ctm><option value=0>None</option><option value=1>Left</option><option value=2>Right</option><option value=4>Middle</option><option value=8>Back</option><option value=16>Forward</option></select>
<h3>CANCEL</h3><label>Extra Cancel Action</label><select id=cc></select>
<h3>SEQUENCE STEPS</h3><div id=csteps></div><button id=cadd class=ghost>+ ADD OAG COMBO STEP</button><button id=sct>SAVE OAG COMBO PROGRAM</button><div id=cmsg class=msg></div></section>

<section class=card><h2>Active Profile</h2><div class=two><div><label>Game</label><select id=ag></select></div><div><label>Weapon</label><select id=aw></select></div></div><button id=sp>SAVE ACTIVE PROFILE</button></section>
<section class=card><h2>System</h2><p id=gen class=hint>Loading...</p><button id=play class=play>SAVE & PLAY</button><div id=pmsg class=msg></div></section>
</main><script src=/app.js></script><script src=/gwc.js></script><script src=/names.js></script></body></html>)HTML";

constexpr char kAppCss[] = R"CSS(*{box-sizing:border-box}body{margin:0;background:#070b12;color:#eef2ff;font:15px Arial,sans-serif}header{padding:20px;background:#111827;border-bottom:1px solid #263247}h1{margin:0;font-size:30px}header span,.hint{color:#94a3b8}main{max-width:900px;margin:auto;padding:14px}.card{background:#111827;border:1px solid #263247;border-radius:15px;padding:16px;margin-bottom:13px}h2{margin:0 0 12px}h3{text-align:center;font-size:13px;color:#cbd5e1;letter-spacing:.5px}label{display:block;margin:10px 0 5px;color:#cbd5e1}input,select,button{width:100%;padding:11px;border-radius:9px;border:1px solid #334155;background:#0f172a;color:white}input[type=range]{padding:0;accent-color:#86efac}button{margin-top:10px;background:#e5e7eb;color:#111827;font-weight:800}.accent,.play{background:#86efac;color:#052e16}.ghost{background:#1f2937;color:#e5e7eb}.two,.pads{display:grid;grid-template-columns:1fr 1fr;gap:14px}.gamepad{padding:12px;border:1px solid #29364b;border-radius:24px;background:#0b1220}.pad{width:min(36vw,230px);height:min(36vw,230px);margin:auto;border:2px solid #475569;border-radius:50%;position:relative;background:radial-gradient(circle,#172033 0,#0b1220 70%);overflow:hidden}.pad:before,.pad:after{content:"";position:absolute;background:#334155;z-index:0}.pad:before{width:1px;height:100%;left:50%}.pad:after{height:1px;width:100%;top:50%}.pad i{position:absolute;border-radius:50%;transform:translate(-50%,-50%);z-index:4}.pad .raw{width:18px;height:18px;background:#86efac;box-shadow:0 0 12px #86efac}.pad .filtered{width:10px;height:10px;background:#60a5fa;box-shadow:0 0 8px #60a5fa}.center{position:absolute;width:12px;height:12px;border:2px solid #f8fafc;border-radius:50%;transform:translate(-50%,-50%);z-index:3}.dzring{position:absolute;border:2px dashed #fbbf24;border-radius:50%;transform:translate(-50%,-50%);z-index:2;pointer-events:none}.mono{font:12px monospace;color:#94a3b8;text-align:center;margin:7px}.metric{text-align:center}.metric b{font-size:18px;color:#86efac}.metric small{display:block;color:#64748b}.legend{display:flex;justify-content:center;gap:14px;flex-wrap:wrap;margin:12px 0;color:#94a3b8;font-size:11px}.legend i{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:5px}.rawkey{background:#86efac}.filterkey{background:#60a5fa}.centerkey{border:2px solid #f8fafc}.shoulders,.face,.systembuttons{display:flex;gap:7px;justify-content:center;flex-wrap:wrap;margin:8px}.shoulders{justify-content:space-between}.shoulders span,.face span,.systembuttons span{min-width:38px;text-align:center;padding:7px 9px;border-radius:9px;border:1px solid #334155;background:#111827;color:#64748b;font-weight:800}.on{background:#86efac!important;color:#052e16!important;border-color:#86efac!important;box-shadow:0 0 10px #86ef9666}.trigs{display:grid;grid-template-columns:1fr 1fr;gap:15px;margin:12px}.trigs em{display:block;height:7px;background:#1e293b;border-radius:5px;overflow:hidden}.trigs em i{display:block;height:100%;width:0;background:#86efac}.wizard{margin-top:15px;padding-top:8px;border-top:1px solid #263247}.step{border:1px solid #334155;border-radius:10px;padding:10px;margin:8px 0}.stepgrid{display:grid;grid-template-columns:1.2fr 1.2fr .8fr .8fr .8fr;gap:7px}.step button{margin-top:7px}.status,.msg{margin:9px 0;color:#86efac}.bad{color:#fca5a5}@media(max-width:560px){.card{padding:12px}.pad{width:40vw;height:40vw}.two{grid-template-columns:1fr 1fr}.stepgrid{grid-template-columns:1fr 1fr}.trigs{margin:10px 0}})CSS";

constexpr char kAppJs[] = R"JS(const $=x=>document.getElementById(x);let cfg,games=[],weapons=[],combos=[];
function opts(e,n,p){e.innerHTML='';for(let i=1;i<=n;i++){let o=document.createElement('option');o.value=i;o.textContent=p+' '+i;e.appendChild(o)}}
opts($('gs'),8,'ADD OAG GAME');opts($('ws'),24,'ADD OAG WEAPON');opts($('cs'),16,'ADD OAG COMBO');
const enc=d=>new URLSearchParams(d);async function post(p,d){let r=await fetch(p,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc(d)}),t=await r.text();if(!r.ok)throw Error(t);return t}
async function json(p){let r=await fetch(p,{cache:'no-store'});if(!r.ok)throw Error(await r.text());return r.json()}
function names(e,a,p){e.innerHTML='';a.forEach((n,i)=>{let o=document.createElement('option');o.value=i+1;o.textContent=n||p+' '+(i+1);e.appendChild(o)})}
async function load(){cfg=await json('/api/config');$('gen').textContent='Flash generation: '+cfg.generation;games=(await json('/api/games')).names;combos=(await json('/api/combos')).names;names($('gs'),games,'ADD OAG GAME');names($('ag'),games,'OAG GAME');names($('cs'),combos,'ADD OAG COMBO');$('ag').value=cfg.activeGame+1;await loadWeapons($('ag').value);$('aw').value=cfg.activeWeapon+1;showGameName();showComboName();updateAddButtons()}
async function loadWeapons(g){weapons=(await json('/api/weapons?game='+g)).names;names($('ws'),weapons,'ADD OAG WEAPON');names($('aw'),weapons,'OAG WEAPON');showWeaponName();updateAddButtons();if(typeof loadWeaponSettings==='function')await loadWeaponSettings()}
)JS";



constexpr char kGwcJs[] = R"JS(const controls=[['None',0],['South (A / Cross)',1],['East (B / Circle)',2],['West (X / Square)',3],['North (Y / Triangle)',4],['LB / L1',5],['RB / R1',6],['LT / L2',7],['RT / R2',8],['Left Stick Click',9],['Right Stick Click',10],['Back / View',11],['Start / Menu',12],['Guide',13],['D-Pad Up',14],['D-Pad Down',15],['D-Pad Left',16],['D-Pad Right',17]];
const kinds=[['Press',0],['Hold Start',1],['Hold End',2],['Pulse',3],['Wait ms',4],['Wait Until Pressed',5],['Wait Until Released',6]];
function fillCtl(e,none=true){e.innerHTML='';controls.forEach(x=>{if(!none&&x[1]===0)return;let o=document.createElement('option');o.value=x[1];o.textContent=x[0];e.appendChild(o)})}
fillCtl($('ctl'));fillCtl($('cc'));$('cc').insertAdjacentHTML('afterbegin','<option value="-1">None</option>');$('cc').value=-1;
function keyToHid(v){v=(v||'').trim().toUpperCase();if(!v)return 0;if(v.length===1&&v>='A'&&v<='Z')return v.charCodeAt(0)-65+4;if(v.length===1&&v>='1'&&v<='9')return v.charCodeAt(0)-49+30;if(v==='0')return 39;if(v==='SPACE')return 44;if(v==='ENTER')return 40;if(v==='TAB')return 43;throw Error('Keyboard trigger: use A-Z, 0-9, Space, Enter or Tab')}
function hidToKey(v){if(v>=4&&v<=29)return String.fromCharCode(65+v-4);if(v>=30&&v<=38)return String(v-29);if(v===39)return'0';if(v===44)return'Space';if(v===40)return'Enter';if(v===43)return'Tab';return''}
function stepRow(s={kind:0,control:1,durationMs:50,intervalMs:50,repeatCount:1}){let d=document.createElement('div');d.className='step';d.innerHTML='<div class=stepgrid><select class=sk></select><select class=sco></select><input class=sd type=number min=0 max=60000 step=1 title="Duration ms"><input class=si type=number min=0 max=60000 step=1 title="Interval ms"><input class=srp type=number min=0 max=1000 step=1 title="Repeat; 0 = until cancelled"></div><button class=ghost>REMOVE STEP</button>';kinds.forEach(x=>d.querySelector('.sk').add(new Option(x[0],x[1])));controls.forEach(x=>d.querySelector('.sco').add(new Option(x[0],x[1])));d.querySelector('.sk').value=s.kind;d.querySelector('.sco').value=s.control;d.querySelector('.sd').value=s.durationMs;d.querySelector('.si').value=s.intervalMs;d.querySelector('.srp').value=s.repeatCount;d.querySelector('button').onclick=()=>d.remove();$('csteps').appendChild(d)}
$('cadd').onclick=()=>{if($('csteps').children.length>=16){$('cmsg').textContent='Maximum 16 steps';return}stepRow()};
async function loadWeaponSettings(){try{let g=$('gs').value,w=$('ws').value,d=await json('/api/recoil?game='+g+'&weapon='+w);$('rh').value=(d.horizontalRaw/100).toFixed(2);$('rv').value=(d.verticalRaw/100).toFixed(2);$('rt').value=d.tickMs}catch(e){$('nmsg').textContent=e.message}}
async function loadComboTiming(){try{let d=await json('/api/combo-program?slot='+$('cs').value);$('ca').value=d.activation;$('car').value=d.repeat;$('cpass').checked=!!d.passTrigger;$('crel').checked=!!d.cancelRelease;$('cagain').checked=!!d.cancelAgain;$('ctl').value=d.logicalTrigger||0;$('ctk').value=hidToKey(d.keyboardTrigger||0);$('ctm').value=d.mouseTrigger||0;$('cc').value=d.cancelEnabled?d.cancelControl:-1;$('csteps').innerHTML='';(d.steps||[]).forEach(stepRow)}catch(e){$('cmsg').textContent=e.message}}
$('sr').onclick=async()=>{try{let h=Math.round(Number($('rh').value)*100),v=Math.round(Number($('rv').value)*100);if(h<-200||h>200||v<-200||v>200)throw Error('Recoil must be between -2.00 and +2.00');await post('/api/recoil',{game:$('gs').value,weapon:$('ws').value,horizontal:h,vertical:v,tickMs:$('rt').value});$('nmsg').textContent='OAG Weapon recoil saved to Flash';await loadWeaponSettings()}catch(e){$('nmsg').textContent=e.message}};
$('sct').onclick=async()=>{try{let rows=[...$('csteps').children],d={slot:$('cs').value,activation:$('ca').value,repeat:$('car').value,pass:$('cpass').checked?1:0,cancelRelease:$('crel').checked?1:0,cancelAgain:$('cagain').checked?1:0,cancelEnabled:+$('cc').value>=0?1:0,cancelControl:+$('cc').value>=0?$('cc').value:0,logicalTrigger:$('ctl').value,keyboardTrigger:keyToHid($('ctk').value),mouseTrigger:$('ctm').value,stepCount:rows.length};rows.forEach((r,i)=>{d['s'+i+'k']=r.querySelector('.sk').value;d['s'+i+'c']=r.querySelector('.sco').value;d['s'+i+'d']=r.querySelector('.sd').value;d['s'+i+'i']=r.querySelector('.si').value;d['s'+i+'r']=r.querySelector('.srp').value});await post('/api/combo-program',d);$('cmsg').textContent='OAG Combo program saved to Flash and ready for gameplay';await loadComboTiming()}catch(e){$('cmsg').textContent=e.message}};
)JS";

constexpr char kNamesJs[] = R"JS(function showGameName(){$('gn').value=games[+$('gs').value-1]||'';updateAddButtons()}function showWeaponName(){$('wn').value=weapons[+$('ws').value-1]||'';updateAddButtons()}function showComboName(){$('cn').value=combos[+$('cs').value-1]||'';updateAddButtons()}
function updateAddButtons(){let g=+$('gs').value||1,w=+$('ws').value||1,c=+$('cs').value||1;$('sg').textContent=games[g-1]?'UPDATE '+games[g-1]:'ADD OAG GAME '+g;$('sw').textContent=weapons[w-1]?'UPDATE '+weapons[w-1]:'ADD OAG WEAPON '+w;$('sc').textContent=combos[c-1]?'UPDATE '+combos[c-1]:'ADD OAG COMBO '+c}
$('gs').onchange=async()=>{showGameName();await loadWeapons($('gs').value)};$('ws').onchange=()=>{showWeaponName();loadWeaponSettings()};$('cs').onchange=()=>{showComboName();loadComboTiming()};$('ag').onchange=async()=>{await loadWeapons($('ag').value)};
$('sg').onclick=async()=>{try{let slot=$('gs').value;await post('/api/name',{type:'game',slot,name:$('gn').value});games=(await json('/api/games')).names;names($('gs'),games,'ADD OAG GAME');names($('ag'),games,'OAG GAME');$('gs').value=slot;showGameName();$('nmsg').textContent='OAG Game saved'}catch(e){$('nmsg').textContent=e.message}};
$('sw').onclick=async()=>{try{let slot=$('ws').value;await post('/api/name',{type:'weapon',game:$('gs').value,slot,name:$('wn').value});await loadWeapons($('gs').value);$('ws').value=slot;showWeaponName();$('nmsg').textContent='OAG Weapon saved'}catch(e){$('nmsg').textContent=e.message}};
$('sc').onclick=async()=>{try{let slot=$('cs').value;await post('/api/name',{type:'combo',slot,name:$('cn').value});combos=(await json('/api/combos')).names;names($('cs'),combos,'ADD OAG COMBO');$('cs').value=slot;showComboName();$('nmsg').textContent='OAG Combo saved'}catch(e){$('nmsg').textContent=e.message}};
$('sp').onclick=async()=>{try{await post('/api/profile',{game:$('ag').value,weapon:$('aw').value,enabled:1});$('nmsg').textContent='Active profile saved';await load()}catch(e){$('nmsg').textContent=e.message}};
$('play').onclick=async()=>{try{await post('/api/save-play',{});$('pmsg').textContent='Saved. Restarting into Gaming Mode...'}catch(e){$('pmsg').textContent=e.message}};
load().then(()=>loadComboTiming()).catch(e=>$('gen').textContent=e.message);
)JS";

static_assert(sizeof(kDashboardHtml) + 640u < TCP_SND_BUF);
static_assert(sizeof(kAppCss) + 640u < TCP_SND_BUF);
static_assert(sizeof(kAppJs) + 640u < TCP_SND_BUF);
static_assert(sizeof(kNamesJs) + 640u < TCP_SND_BUF);
static_assert(sizeof(kGwcJs) + 640u < TCP_SND_BUF);

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

bool DiamondWifiPortal::start(DiamondConfigStore& store){
    if(started_)return true;store_=&store;if(!store_->load())return false;if(cyw43_arch_init()!=0)return false;
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
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/names.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kNamesJs);return;}
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/gwc.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kGwcJs);return;}

    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/games")){sendNames(client,pc.names.games);return;}
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/combos")){sendNames(client,pc.names.combos);return;}
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/weapons?game=",18)){
        std::uint32_t g=static_cast<std::uint32_t>(std::strtoul(path+18,nullptr,10));if(g<1||g>oag::kDiamondGameSlots){sendResponse(client,"400 Bad Request","text/plain","Invalid game");return;}sendNames(client,pc.names.weapons[g-1]);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/recoil?game=",17)){
        const char* wp=std::strstr(path,"&weapon=");
        if(!wp){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG weapon");return;}
        const auto g=static_cast<std::uint32_t>(std::strtoul(path+17,nullptr,10));
        const auto w=static_cast<std::uint32_t>(std::strtoul(wp+8,nullptr,10));
        if(g<1||g>oag::kDiamondGameSlots||w<1||w>oag::kDiamondWeaponSlotsPerGame){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG weapon");return;}
        const auto& r=runtime.games[g-1].weapons[w-1];char j[256]{};
        std::snprintf(j,sizeof(j),"{\"horizontalRaw\":%ld,\"verticalRaw\":%ld,\"tickMs\":%u,\"enabled\":%s}",
            static_cast<long>(r.horizontalHalfPermille),static_cast<long>(r.verticalHalfPermille),r.tickMs,r.enabled?"true":"false");
        sendResponse(client,"200 OK","application/json",j);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/combo-program?slot=",24)){
        const auto slot=static_cast<std::uint32_t>(std::strtoul(path+24,nullptr,10));
        if(slot<1||slot>oag::kDiamondComboSlots){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo");return;}
        const auto& p=pc.names.comboPrograms[slot-1];
        std::uint16_t logical=0,key=0,mouse=0;
        for(const auto& t:p.triggers){
            if(!t.enabled)continue;
            if(t.kind==oag::DiamondComboTriggerKind::LogicalControl&&!logical)logical=t.code;
            else if(t.kind==oag::DiamondComboTriggerKind::KeyboardUsage&&!key)key=t.code;
            else if(t.kind==oag::DiamondComboTriggerKind::MouseButton&&!mouse)mouse=t.code;
        }
        static char j[3072];std::size_t used=0;
        int n=std::snprintf(j,sizeof(j),"{\"activation\":%u,\"repeat\":%u,\"passTrigger\":%s,\"cancelRelease\":%s,\"cancelAgain\":%s,\"cancelEnabled\":%s,\"cancelControl\":%u,\"logicalTrigger\":%u,\"keyboardTrigger\":%u,\"mouseTrigger\":%u,\"steps\":[",
            static_cast<unsigned>(p.activation),static_cast<unsigned>(p.repeat),p.passTriggerThrough?"true":"false",p.cancelOnTriggerRelease?"true":"false",p.cancelOnTriggerPressAgain?"true":"false",p.cancelControlEnabled?"true":"false",static_cast<unsigned>(p.cancelControl),logical,key,mouse);
        if(n<0||static_cast<std::size_t>(n)>=sizeof(j)){sendResponse(client,"500 Internal Server Error","text/plain","Combo encode failed");return;}used=static_cast<std::size_t>(n);
        for(std::uint8_t i=0;i<p.stepCount&&i<oag::kDiamondComboSteps;i++){
            const auto& s=p.steps[i];
            n=std::snprintf(j+used,sizeof(j)-used,"%s{\"kind\":%u,\"control\":%u,\"durationMs\":%u,\"intervalMs\":%u,\"repeatCount\":%u}",
                i?",":"",static_cast<unsigned>(s.kind),static_cast<unsigned>(s.control),s.durationMs,s.intervalMs,s.repeatCount);
            if(n<0||static_cast<std::size_t>(n)>=sizeof(j)-used){sendResponse(client,"500 Internal Server Error","text/plain","Combo encode failed");return;}used+=static_cast<std::size_t>(n);
        }
        if(used+3>=sizeof(j)){sendResponse(client,"500 Internal Server Error","text/plain","Combo encode failed");return;}
        j[used++]=']';j[used++]='}';j[used]='\0';
        sendResponse(client,"200 OK","application/json",j);return;
    }

    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/combo?slot=",16)){
        const auto slot=static_cast<std::uint32_t>(std::strtoul(path+16,nullptr,10));
        if(slot<1||slot>oag::kDiamondComboSlots){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo");return;}
        const auto& t=pc.names.comboTiming[slot-1];char j[256]{};
        std::snprintf(j,sizeof(j),"{\"pressMs\":%u,\"delayAfterMs\":%u,\"repeatCount\":%u,\"enabled\":%s}",
            t.pressMs,t.delayAfterMs,t.repeatCount,t.enabled?"true":"false");
        sendResponse(client,"200 OK","application/json",j);return;
    }

    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/config")){
        char j[256]{};
        std::snprintf(j,sizeof(j),"{\"generation\":%lu,\"activeGame\":%u,\"activeWeapon\":%u}",
            static_cast<unsigned long>(store_->generation()),runtime.activeGame,runtime.activeWeapon);
        sendResponse(client,"200 OK","application/json",j);return;
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

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/recoil")){
        std::uint32_t g=0,w=0,tick=0;std::int32_t horizontal=0,vertical=0;
        if(!parseUnsigned(body,"game",1,oag::kDiamondGameSlots,g)||!parseUnsigned(body,"weapon",1,oag::kDiamondWeaponSlotsPerGame,w)||
           !parseSigned(body,"horizontal",-200,200,horizontal)||!parseSigned(body,"vertical",-200,200,vertical)||!parseUnsigned(body,"tickMs",1,1000,tick)){
            sendResponse(client,"400 Bad Request","text/plain","Invalid OAG recoil");return;
        }
        auto old=runtime.games[g-1].weapons[w-1];auto& r=runtime.games[g-1].weapons[w-1];
        r.enabled=true;r.horizontalHalfPermille=horizontal;r.verticalHalfPermille=vertical;r.tickMs=static_cast<std::uint16_t>(tick);
        if(!store_->save()){r=old;sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}
        sendResponse(client,"200 OK","text/plain","OAG recoil saved");return;
    }
    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/combo-program")){
        std::uint32_t slot=0,activation=0,repeatMode=0,pass=0,cancelRelease=0,cancelAgain=0,cancelEnabled=0,cancelControl=0;
        std::uint32_t logicalTrigger=0,keyboardTrigger=0,mouseTrigger=0,stepCount=0;
        if(!parseUnsigned(body,"slot",1,oag::kDiamondComboSlots,slot)||
           !parseUnsigned(body,"activation",0,2,activation)||!parseUnsigned(body,"repeat",0,1,repeatMode)||
           !parseUnsigned(body,"pass",0,1,pass)||!parseUnsigned(body,"cancelRelease",0,1,cancelRelease)||
           !parseUnsigned(body,"cancelAgain",0,1,cancelAgain)||!parseUnsigned(body,"cancelEnabled",0,1,cancelEnabled)||
           !parseUnsigned(body,"cancelControl",0,17,cancelControl)||!parseUnsigned(body,"logicalTrigger",0,17,logicalTrigger)||
           !parseUnsigned(body,"keyboardTrigger",0,255,keyboardTrigger)||!parseUnsigned(body,"mouseTrigger",0,31,mouseTrigger)||
           !parseUnsigned(body,"stepCount",0,oag::kDiamondComboSteps,stepCount)){
            sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo program");return;
        }

        auto old=pc.names.comboPrograms[slot-1];
        oag::DiamondComboProgram p{};
        p.activation=static_cast<oag::DiamondComboActivationMode>(activation);
        p.repeat=static_cast<oag::DiamondComboRepeatMode>(repeatMode);
        p.passTriggerThrough=pass!=0;p.cancelOnTriggerRelease=cancelRelease!=0;p.cancelOnTriggerPressAgain=cancelAgain!=0;
        p.cancelControlEnabled=cancelEnabled!=0;p.cancelControl=static_cast<oag::DiamondLogicalControl>(cancelControl);
        std::size_t ti=0;
        if(logicalTrigger&&ti<p.triggers.size())p.triggers[ti++]={true,oag::DiamondComboTriggerKind::LogicalControl,static_cast<std::uint16_t>(logicalTrigger),0};
        if(keyboardTrigger&&ti<p.triggers.size())p.triggers[ti++]={true,oag::DiamondComboTriggerKind::KeyboardUsage,static_cast<std::uint16_t>(keyboardTrigger),0};
        if(mouseTrigger&&ti<p.triggers.size())p.triggers[ti++]={true,oag::DiamondComboTriggerKind::MouseButton,static_cast<std::uint16_t>(mouseTrigger),0};

        p.stepCount=static_cast<std::uint8_t>(stepCount);
        for(std::uint32_t i=0;i<stepCount;i++){
            char kk[8]{},kc[8]{},kd[8]{},ki[8]{},kr[8]{};
            std::snprintf(kk,sizeof(kk),"s%luk",static_cast<unsigned long>(i));std::snprintf(kc,sizeof(kc),"s%luc",static_cast<unsigned long>(i));
            std::snprintf(kd,sizeof(kd),"s%lud",static_cast<unsigned long>(i));std::snprintf(ki,sizeof(ki),"s%lui",static_cast<unsigned long>(i));std::snprintf(kr,sizeof(kr),"s%lur",static_cast<unsigned long>(i));
            std::uint32_t kind=0,control=0,duration=0,interval=0,repeats=0;
            if(!parseUnsigned(body,kk,0,6,kind)||!parseUnsigned(body,kc,0,17,control)||!parseUnsigned(body,kd,0,60000,duration)||
               !parseUnsigned(body,ki,0,60000,interval)||!parseUnsigned(body,kr,0,1000,repeats)){
                sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo step");return;
            }
            auto& s=p.steps[i];s.enabled=true;s.kind=static_cast<oag::DiamondComboStepKind>(kind);s.control=static_cast<oag::DiamondLogicalControl>(control);
            s.durationMs=static_cast<std::uint16_t>(duration);s.intervalMs=static_cast<std::uint16_t>(interval);s.repeatCount=static_cast<std::uint16_t>(repeats);
        }
        p.enabled=ti>0&&p.stepCount>0;
        pc.names.comboPrograms[slot-1]=p;
        if(!store_->save()){pc.names.comboPrograms[slot-1]=old;sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}
        sendResponse(client,"200 OK","text/plain","OAG combo program saved");return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/combo")){
        std::uint32_t slot=0,press=0,delay=0,repeat=0;
        if(!parseUnsigned(body,"slot",1,oag::kDiamondComboSlots,slot)||!parseUnsigned(body,"pressMs",1,60000,press)||
           !parseUnsigned(body,"delayMs",0,60000,delay)||!parseUnsigned(body,"repeat",1,1000,repeat)){
            sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo timing");return;
        }
        auto old=pc.names.comboTiming[slot-1];auto& t=pc.names.comboTiming[slot-1];
        t.enabled=true;t.pressMs=static_cast<std::uint16_t>(press);t.delayAfterMs=static_cast<std::uint16_t>(delay);t.repeatCount=static_cast<std::uint16_t>(repeat);
        if(!store_->save()){t=old;sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}
        sendResponse(client,"200 OK","text/plain","OAG combo timing saved");return;
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
