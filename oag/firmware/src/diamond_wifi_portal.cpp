#include "oag/firmware/diamond_wifi_portal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

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
<header><h1>OAG ABO GEMI</h1><span>Controller Lab • Smart Anti-Drift • Profiles</span></header><main>
<section class=card><h2>Controller Tester & Smart Anti-Drift</h2><label>Controller</label><select id=ci></select><div id=conn class=status>Waiting for controller...</div>
<div class=gamepad>
<div class=shoulders><span id=lb>LB</span><span id=rb>RB</span></div>
<div class=pads>
<div><h3>LEFT STICK</h3><div class=pad id=lpad><span class=dzring id=lring></span><span class=center id=lcenter></span><i class=raw id=ldot></i><i class=filtered id=lfdot></i></div><div id=lraw class=mono></div><div class=metric><b id=ldrift>0.0%</b><small>LIVE DRIFT</small></div></div>
<div><h3>RIGHT STICK</h3><div class=pad id=rpad><span class=dzring id=rring></span><span class=center id=rcenter></span><i class=raw id=rdot></i><i class=filtered id=rfdot></i></div><div id=rraw class=mono></div><div class=metric><b id=rdrift>0.0%</b><small>LIVE DRIFT</small></div></div>
</div>
<div class=legend><span><i class=rawkey></i>RAW STICK</span><span><i class=filterkey></i>CORRECTED OUTPUT</span><span><i class=centerkey></i>SAVED CENTER</span></div>
<div class=face><span id=du>↑</span><span id=dl>←</span><span id=dd>↓</span><span id=dr>→</span><span id=b2>X</span><span id=b3>Y</span><span id=b0>A</span><span id=b1>B</span></div>
<div class=systembuttons><span id=b8>BACK</span><span id=b10>GUIDE</span><span id=b9>START</span><span id=b11>SHARE</span><span id=b6>L3</span><span id=b7>R3</span></div>
<div class=trigs><div>LT <b id=lt>0%</b><em><i id=ltbar></i></em></div><div>RT <b id=rt>0%</b><em><i id=rtbar></i></em></div></div><div id=mask class=mono>BUTTON MASK 0x0 • DPAD 0</div>
</div>
<div class=wizard><h3>MANUAL + SMART DRIFT CALIBRATION</h3><p class=hint>The GREEN dot is the real stick position. The BLUE dot is what the game will receive. Move each Deadzone slider yourself until the BLUE dot stays centered while the stick is untouched, then Save. Smart Calibrate is optional and can capture the physical center automatically.</p>
<button id=auto class=accent>SMART CALIBRATE CENTER & DRIFT</button><div id=calmsg class=msg></div>
<div class=two><div><label>Left Deadzone <b id=ldv></b></label><input id=ld type=range min=0 max=20 step=.1></div><div><label>Right Deadzone <b id=rdv></b></label><input id=rd type=range min=0 max=20 step=.1></div></div>
<button id=range>TEST FULL STICK RANGE</button><div id=rangemsg class=msg></div>
<button id=savecal>SAVE & VERIFY DRIFT CALIBRATION</button><button id=disable class=ghost>DISABLE ANTI-DRIFT</button></div></section>

<section class=card><h2>Games & Weapons</h2><label>OAG Game</label><select id=gs></select><input id=gn maxlength=32 placeholder="Type your game name"><button id=sg>ADD OAG GAME 1</button>
<label>OAG Weapon for selected game</label><select id=ws></select><input id=wn maxlength=32 placeholder="Type your weapon name"><button id=sw>ADD OAG WEAPON 1</button><div id=nmsg class=msg></div></section>

<section class=card><h2>Combos</h2><label>OAG Combo</label><select id=cs></select><input id=cn maxlength=32 placeholder="Type your combo name"><button id=sc>ADD OAG COMBO 1</button></section>

<section class=card><h2>Active Profile</h2><div class=two><div><label>Game</label><select id=ag></select></div><div><label>Weapon</label><select id=aw></select></div></div><button id=sp>SAVE ACTIVE PROFILE</button></section>
<section class=card><h2>System</h2><p id=gen class=hint>Loading...</p><button id=play class=play>SAVE & PLAY</button><div id=pmsg class=msg></div></section>
</main><script src=/app.js></script><script src=/cal.js></script><script src=/names.js></script></body></html>)HTML";

constexpr char kAppCss[] = R"CSS(*{box-sizing:border-box}body{margin:0;background:#070b12;color:#eef2ff;font:15px Arial,sans-serif}header{padding:20px;background:#111827;border-bottom:1px solid #263247}h1{margin:0;font-size:30px}header span,.hint{color:#94a3b8}main{max-width:900px;margin:auto;padding:14px}.card{background:#111827;border:1px solid #263247;border-radius:15px;padding:16px;margin-bottom:13px}h2{margin:0 0 12px}h3{text-align:center;font-size:13px;color:#cbd5e1;letter-spacing:.5px}label{display:block;margin:10px 0 5px;color:#cbd5e1}input,select,button{width:100%;padding:11px;border-radius:9px;border:1px solid #334155;background:#0f172a;color:white}input[type=range]{padding:0;accent-color:#86efac}button{margin-top:10px;background:#e5e7eb;color:#111827;font-weight:800}.accent,.play{background:#86efac;color:#052e16}.ghost{background:#1f2937;color:#e5e7eb}.two,.pads{display:grid;grid-template-columns:1fr 1fr;gap:14px}.gamepad{padding:12px;border:1px solid #29364b;border-radius:24px;background:#0b1220}.pad{width:min(36vw,230px);height:min(36vw,230px);margin:auto;border:2px solid #475569;border-radius:50%;position:relative;background:radial-gradient(circle,#172033 0,#0b1220 70%);overflow:hidden}.pad:before,.pad:after{content:"";position:absolute;background:#334155;z-index:0}.pad:before{width:1px;height:100%;left:50%}.pad:after{height:1px;width:100%;top:50%}.pad i{position:absolute;border-radius:50%;transform:translate(-50%,-50%);z-index:4}.pad .raw{width:18px;height:18px;background:#86efac;box-shadow:0 0 12px #86efac}.pad .filtered{width:10px;height:10px;background:#60a5fa;box-shadow:0 0 8px #60a5fa}.center{position:absolute;width:12px;height:12px;border:2px solid #f8fafc;border-radius:50%;transform:translate(-50%,-50%);z-index:3}.dzring{position:absolute;border:2px dashed #fbbf24;border-radius:50%;transform:translate(-50%,-50%);z-index:2;pointer-events:none}.mono{font:12px monospace;color:#94a3b8;text-align:center;margin:7px}.metric{text-align:center}.metric b{font-size:18px;color:#86efac}.metric small{display:block;color:#64748b}.legend{display:flex;justify-content:center;gap:14px;flex-wrap:wrap;margin:12px 0;color:#94a3b8;font-size:11px}.legend i{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:5px}.rawkey{background:#86efac}.filterkey{background:#60a5fa}.centerkey{border:2px solid #f8fafc}.shoulders,.face,.systembuttons{display:flex;gap:7px;justify-content:center;flex-wrap:wrap;margin:8px}.shoulders{justify-content:space-between}.shoulders span,.face span,.systembuttons span{min-width:38px;text-align:center;padding:7px 9px;border-radius:9px;border:1px solid #334155;background:#111827;color:#64748b;font-weight:800}.on{background:#86efac!important;color:#052e16!important;border-color:#86efac!important;box-shadow:0 0 10px #86ef9666}.trigs{display:grid;grid-template-columns:1fr 1fr;gap:15px;margin:12px}.trigs em{display:block;height:7px;background:#1e293b;border-radius:5px;overflow:hidden}.trigs em i{display:block;height:100%;width:0;background:#86efac}.wizard{margin-top:15px;padding-top:8px;border-top:1px solid #263247}.status,.msg{margin:9px 0;color:#86efac}.bad{color:#fca5a5}@media(max-width:560px){.card{padding:12px}.pad{width:40vw;height:40vw}.two{grid-template-columns:1fr 1fr}.trigs{margin:10px 0}})CSS";

constexpr char kAppJs[] = R"JS(const $=x=>document.getElementById(x);let cfg,games=[],weapons=[],combos=[],last=null,pcal=null,calBusy=false,rangeBusy=false;
function opts(e,n,p){e.innerHTML='';for(let i=1;i<=n;i++){let o=document.createElement('option');o.value=i;o.textContent=p+' '+i;e.appendChild(o)}}
opts($('ci'),8,'OAG ABO GEMI CONTROLLER');opts($('gs'),8,'ADD OAG GAME');opts($('ws'),24,'ADD OAG WEAPON');opts($('cs'),16,'ADD OAG COMBO');
const enc=d=>new URLSearchParams(d);async function post(p,d){let r=await fetch(p,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc(d)}),t=await r.text();if(!r.ok)throw Error(t);return t}
async function json(p){let r=await fetch(p,{cache:'no-store'});if(!r.ok)throw Error(await r.text());return r.json()}
const AX=2147483647,TR=4294967295;const clamp=(v,a,b)=>Math.max(a,Math.min(b,v)),pctRaw=v=>v/AX*100,rawPct=p=>Math.round(clamp(+p,0,99)*AX/100);
function pos(id,x,y){let e=$(id);e.style.left=(50+clamp(x/AX,-1,1)*45)+'%';e.style.top=(50+clamp(y/AX,-1,1)*45)+'%'}
function ring(id,cid,cx,cy,p){pos(cid,cx,cy);let e=$(id);e.style.left=(50+clamp(cx/AX,-1,1)*45)+'%';e.style.top=(50+clamp(cy/AX,-1,1)*45)+'%';let d=clamp(+p,0,20)*.9;e.style.width=d+'%';e.style.height=d+'%'}
function axis(v,c,d){let q=v-c,m=Math.abs(q),dz=rawPct(d);if(m<=dz)return 0;let a=AX-dz;if(a<=0)return 0;return Math.round(clamp(Math.sign(q)*(m-dz)*AX/a,-AX,AX))}
function preview(){if(!last)return;let c=pcal||currentCal();let ld=+$('ld').value,rd=+$('rd').value;pos('lfdot',axis(last.lx,c.lx,ld),axis(last.ly,c.ly,ld));pos('rfdot',axis(last.rx,c.rx,rd),axis(last.ry,c.ry,rd));ring('lring','lcenter',c.lx,c.ly,ld);ring('rring','rcenter',c.rx,c.ry,rd);$('ldrift').textContent=(Math.hypot(last.lx-c.lx,last.ly-c.ly)/AX*100).toFixed(1)+'%';$('rdrift').textContent=(Math.hypot(last.rx-c.rx,last.ry-c.ry)/AX*100).toFixed(1)+'%'}
function currentCal(){let c=cfg.controllers[+$('ci').value-1];return{lx:c.lx||0,ly:c.ly||0,rx:c.rx||0,ry:c.ry||0}}
function names(e,a,p){e.innerHTML='';a.forEach((n,i)=>{let o=document.createElement('option');o.value=i+1;o.textContent=n||p+' '+(i+1);e.appendChild(o)})}
function dz(){$('ldv').textContent=(+$('ld').value).toFixed(1)+'%';$('rdv').textContent=(+$('rd').value).toFixed(1)+'%';preview()}
function active(id,on){let e=$(id);if(e)e.classList.toggle('on',!!on)}
function buttons(v){for(let i=0;i<12;i++)active('b'+i,(v.buttons&(1<<i))!==0);active('lb',(v.buttons&(1<<4))!==0);active('rb',(v.buttons&(1<<5))!==0);active('du',(v.dpad&1)!==0);active('dd',(v.dpad&2)!==0);active('dl',(v.dpad&4)!==0);active('dr',(v.dpad&8)!==0)}
async function load(){cfg=await json('/api/config');$('gen').textContent='Flash generation: '+cfg.generation;let c=cfg.controllers[+$('ci').value-1];$('ld').value=pctRaw(c.leftDeadzone).toFixed(1);$('rd').value=pctRaw(c.rightDeadzone).toFixed(1);pcal={lx:c.lx||0,ly:c.ly||0,rx:c.rx||0,ry:c.ry||0};dz();games=(await json('/api/games')).names;combos=(await json('/api/combos')).names;names($('gs'),games,'ADD OAG GAME');names($('ag'),games,'OAG GAME');names($('cs'),combos,'ADD OAG COMBO');$('ag').value=cfg.activeGame+1;await loadWeapons($('ag').value);$('aw').value=cfg.activeWeapon+1;showGameName();showComboName();updateAddButtons()}
async function loadWeapons(g){weapons=(await json('/api/weapons?game='+g)).names;names($('ws'),weapons,'ADD OAG WEAPON');names($('aw'),weapons,'OAG WEAPON');showWeaponName();updateAddButtons()}
async function live(){try{last=await json('/api/live?slot='+$('ci').value);$('conn').textContent=last.connected?'CONNECTED • LIVE INPUT':'No controller in this slot';$('conn').className=last.connected?'status':'status bad';pos('ldot',last.lx,last.ly);pos('rdot',last.rx,last.ry);$('lraw').textContent='RAW X '+last.lx+' • Y '+last.ly;$('rraw').textContent='RAW X '+last.rx+' • Y '+last.ry;let ltp=clamp(last.lt/TR*100,0,100),rtp=clamp(last.rt/TR*100,0,100);$('lt').textContent=ltp.toFixed(0)+'%';$('rt').textContent=rtp.toFixed(0)+'%';$('ltbar').style.width=ltp+'%';$('rtbar').style.width=rtp+'%';$('mask').textContent='BUTTON MASK 0x'+Number(last.buttons).toString(16).toUpperCase()+' • DPAD '+last.dpad;buttons(last);preview()}catch(e){}setTimeout(live,80)}
$('ld').oninput=$('rd').oninput=dz;$('ci').onchange=()=>{pcal=null;let c=cfg.controllers[+$('ci').value-1];$('ld').value=pctRaw(c.leftDeadzone).toFixed(1);$('rd').value=pctRaw(c.rightDeadzone).toFixed(1);pcal={lx:c.lx||0,ly:c.ly||0,rx:c.rx||0,ry:c.ry||0};$('calmsg').textContent='';$('rangemsg').textContent='';dz()};
)JS";

constexpr char kCalJs[] = R"JS(function median(a){let b=[...a].sort((x,y)=>x-y),m=b.length>>1;return b.length%2?b[m]:Math.round((b[m-1]+b[m])/2)}
function percentile(a,p){let b=[...a].sort((x,y)=>x-y);return b[Math.min(b.length-1,Math.floor((b.length-1)*p))]}
async function samples(n,delay,msg){let a=[];for(let i=0;i<n;i++){let v=await json('/api/live?slot='+$('ci').value);if(v.connected)a.push(v);$(msg).textContent='Sampling '+Math.round((i+1)/n*100)+'%';await new Promise(r=>setTimeout(r,delay))}return a}
$('auto').onclick=async()=>{if(calBusy)return;calBusy=true;$('auto').disabled=true;$('calmsg').textContent='Release both sticks completely...';await new Promise(r=>setTimeout(r,800));try{let a=await samples(50,40,'calmsg');if(a.length<35)throw Error('Controller signal was not stable. Keep it connected and try again.');let lx=median(a.map(v=>v.lx)),ly=median(a.map(v=>v.ly)),rx=median(a.map(v=>v.rx)),ry=median(a.map(v=>v.ry));let lr=a.map(v=>Math.hypot(v.lx-lx,v.ly-ly)),rr=a.map(v=>Math.hypot(v.rx-rx,v.ry-ry));let lp=percentile(lr,.95),rp=percentile(rr,.95);let ldz=clamp((lp+Math.max(250,lp*.30))/AX*100,.8,12),rdz=clamp((rp+Math.max(250,rp*.30))/AX*100,.8,12);pcal={lx,ly,rx,ry};$('ld').value=ldz.toFixed(1);$('rd').value=rdz.toFixed(1);dz();$('calmsg').textContent='Smart center captured • Recommended L '+ldz.toFixed(1)+'% • R '+rdz.toFixed(1)+'%. Move the sticks now and watch RAW vs corrected dots.'}catch(e){$('calmsg').textContent=e.message}finally{calBusy=false;$('auto').disabled=false}};
$('range').onclick=async()=>{if(rangeBusy)return;rangeBusy=true;$('range').disabled=true;let c=pcal||currentCal(),end=Date.now()+6000,l={px:0,nx:0,py:0,ny:0},r={px:0,nx:0,py:0,ny:0};$('rangemsg').textContent='Rotate BOTH sticks around the full edge for 6 seconds...';try{while(Date.now()<end){let v=await json('/api/live?slot='+$('ci').value);if(v.connected){let lx=v.lx-c.lx,ly=v.ly-c.ly,rx=v.rx-c.rx,ry=v.ry-c.ry;l.px=Math.max(l.px,lx);l.nx=Math.max(l.nx,-lx);l.py=Math.max(l.py,ly);l.ny=Math.max(l.ny,-ly);r.px=Math.max(r.px,rx);r.nx=Math.max(r.nx,-rx);r.py=Math.max(r.py,ry);r.ny=Math.max(r.ny,-ry)}await new Promise(q=>setTimeout(q,45))}let cov=o=>clamp((o.px+o.nx+o.py+o.ny)/(4*AX)*100,0,100);$('rangemsg').textContent='Range coverage • LEFT '+cov(l).toFixed(0)+'% • RIGHT '+cov(r).toFixed(0)+'% • This test diagnoses stick travel; it does not fake or stretch the hardware range.'}catch(e){$('rangemsg').textContent=e.message}finally{rangeBusy=false;$('range').disabled=false}};
$('savecal').onclick=async()=>{if(!pcal)pcal=currentCal();let slot=+$('ci').value,want={lx:Math.round(pcal.lx),ly:Math.round(pcal.ly),rx:Math.round(pcal.rx),ry:Math.round(pcal.ry),ld:rawPct($('ld').value),rd:rawPct($('rd').value)};try{$('calmsg').textContent='Saving to Pico flash...';await post('/api/calibration',{slot,enabled:1,lx:want.lx,ly:want.ly,rx:want.rx,ry:want.ry,ld:want.ld,rd:want.rd});let verify=await json('/api/config'),c=verify.controllers[slot-1],ok=c.enabled&&c.lx===want.lx&&c.ly===want.ly&&c.rx===want.rx&&c.ry===want.ry&&c.leftDeadzone===want.ld&&c.rightDeadzone===want.rd;if(!ok)throw Error('VERIFY FAILED - saved values do not match');$('calmsg').textContent='SAVED + FLASH VERIFIED • Generation '+verify.generation+' • L '+pctRaw(c.leftDeadzone).toFixed(1)+'% • R '+pctRaw(c.rightDeadzone).toFixed(1)+'%';cfg=verify;pcal={lx:c.lx,ly:c.ly,rx:c.rx,ry:c.ry};dz()}catch(e){$('calmsg').textContent=e.message}};
$('disable').onclick=async()=>{try{await post('/api/calibration',{slot:$('ci').value,enabled:0,lx:0,ly:0,rx:0,ry:0,ld:0,rd:0});pcal={lx:0,ly:0,rx:0,ry:0};$('calmsg').textContent='Anti-drift disabled';await load()}catch(e){$('calmsg').textContent=e.message}};
)JS";

constexpr char kNamesJs[] = R"JS(function showGameName(){$('gn').value=games[+$('gs').value-1]||'';updateAddButtons()}function showWeaponName(){$('wn').value=weapons[+$('ws').value-1]||'';updateAddButtons()}function showComboName(){$('cn').value=combos[+$('cs').value-1]||'';updateAddButtons()}
function updateAddButtons(){let g=+$('gs').value||1,w=+$('ws').value||1,c=+$('cs').value||1;$('sg').textContent=games[g-1]?'UPDATE '+games[g-1]:'ADD OAG GAME '+g;$('sw').textContent=weapons[w-1]?'UPDATE '+weapons[w-1]:'ADD OAG WEAPON '+w;$('sc').textContent=combos[c-1]?'UPDATE '+combos[c-1]:'ADD OAG COMBO '+c}
$('gs').onchange=async()=>{showGameName();await loadWeapons($('gs').value)};$('ws').onchange=showWeaponName;$('cs').onchange=showComboName;$('ag').onchange=async()=>{await loadWeapons($('ag').value)};
$('sg').onclick=async()=>{try{let slot=$('gs').value;await post('/api/name',{type:'game',slot,name:$('gn').value});games=(await json('/api/games')).names;names($('gs'),games,'ADD OAG GAME');names($('ag'),games,'OAG GAME');$('gs').value=slot;showGameName();$('nmsg').textContent='OAG Game saved'}catch(e){$('nmsg').textContent=e.message}};
$('sw').onclick=async()=>{try{let slot=$('ws').value;await post('/api/name',{type:'weapon',game:$('gs').value,slot,name:$('wn').value});await loadWeapons($('gs').value);$('ws').value=slot;showWeaponName();$('nmsg').textContent='OAG Weapon saved'}catch(e){$('nmsg').textContent=e.message}};
$('sc').onclick=async()=>{try{let slot=$('cs').value;await post('/api/name',{type:'combo',slot,name:$('cn').value});combos=(await json('/api/combos')).names;names($('cs'),combos,'ADD OAG COMBO');$('cs').value=slot;showComboName();$('nmsg').textContent='OAG Combo saved'}catch(e){$('nmsg').textContent=e.message}};
$('sp').onclick=async()=>{try{await post('/api/profile',{game:$('ag').value,weapon:$('aw').value,enabled:1});$('nmsg').textContent='Active profile saved';await load()}catch(e){$('nmsg').textContent=e.message}};
$('play').onclick=async()=>{try{await post('/api/save-play',{});$('pmsg').textContent='Saved. Restarting into Gaming Mode...'}catch(e){$('pmsg').textContent=e.message}};
load().then(live).catch(e=>$('gen').textContent=e.message);
)JS";

static_assert(sizeof(kDashboardHtml) + 640u < TCP_SND_BUF);
static_assert(sizeof(kAppCss) + 640u < TCP_SND_BUF);
static_assert(sizeof(kAppJs) + 640u < TCP_SND_BUF);
static_assert(sizeof(kCalJs) + 640u < TCP_SND_BUF);
static_assert(sizeof(kNamesJs) + 640u < TCP_SND_BUF);

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
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/cal.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kCalJs);return;}
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/names.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kNamesJs);return;}

    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/live?slot=",15)){
        std::uint32_t slot=static_cast<std::uint32_t>(std::strtoul(path+15,nullptr,10));if(slot<1||slot>liveStateCount_){sendResponse(client,"400 Bad Request","text/plain","Invalid slot");return;}
        const auto& s=liveStates_[slot-1];char j[384]{};std::snprintf(j,sizeof(j),"{\"connected\":%s,\"lx\":%ld,\"ly\":%ld,\"rx\":%ld,\"ry\":%ld,\"lt\":%lu,\"rt\":%lu,\"buttons\":%llu,\"dpad\":%u}",s.connected?"true":"false",static_cast<long>(s.lx),static_cast<long>(s.ly),static_cast<long>(s.rx),static_cast<long>(s.ry),static_cast<unsigned long>(s.leftTrigger),static_cast<unsigned long>(s.rightTrigger),static_cast<unsigned long long>(s.buttons),static_cast<unsigned>(s.dpad));sendResponse(client,"200 OK","application/json",j);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/games")){sendNames(client,pc.names.games);return;}
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/combos")){sendNames(client,pc.names.combos);return;}
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/weapons?game=",18)){
        std::uint32_t g=static_cast<std::uint32_t>(std::strtoul(path+18,nullptr,10));if(g<1||g>oag::kDiamondGameSlots){sendResponse(client,"400 Bad Request","text/plain","Invalid game");return;}sendNames(client,pc.names.weapons[g-1]);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/config")){
        char j[2048]{};int used=std::snprintf(j,sizeof(j),"{\"generation\":%lu,\"activeGame\":%u,\"activeWeapon\":%u,\"controllers\":[",static_cast<unsigned long>(store_->generation()),runtime.activeGame,runtime.activeWeapon);
        for(std::size_t i=0;i<runtime.controllers.size();++i){const auto& c=runtime.controllers[i];used+=std::snprintf(j+used,sizeof(j)-static_cast<std::size_t>(used),"%s{\"enabled\":%s,\"leftDeadzone\":%lu,\"rightDeadzone\":%lu,\"lx\":%ld,\"ly\":%ld,\"rx\":%ld,\"ry\":%ld}",i?",":"",c.enabled?"true":"false",static_cast<unsigned long>(c.left.deadzone),static_cast<unsigned long>(c.right.deadzone),static_cast<long>(c.left.centerX),static_cast<long>(c.left.centerY),static_cast<long>(c.right.centerX),static_cast<long>(c.right.centerY));}
        std::snprintf(j+used,sizeof(j)-static_cast<std::size_t>(used),"]}");sendResponse(client,"200 OK","application/json",j);return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/calibration")){
        std::uint32_t slot=0,en=0,ld=0,rd=0;std::int32_t lx=0,ly=0,rx=0,ry=0;
        if(!parseUnsigned(body,"slot",1,oag::kDiamondControllerSlots,slot)||!parseUnsigned(body,"enabled",0,1,en)||!parseSigned(body,"lx",std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),lx)||!parseSigned(body,"ly",std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),ly)||!parseSigned(body,"rx",std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),rx)||!parseSigned(body,"ry",std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),ry)||!parseUnsigned(body,"ld",0,static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),ld)||!parseUnsigned(body,"rd",0,static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),rd)){sendResponse(client,"400 Bad Request","text/plain","Invalid calibration");return;}
        auto old=runtime.controllers[slot-1];auto& c=runtime.controllers[slot-1];c.enabled=en!=0;c.left.centerX=lx;c.left.centerY=ly;c.right.centerX=rx;c.right.centerY=ry;c.left.deadzone=ld;c.right.deadzone=rd;
        if(!store_->save()){c=old;sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}sendResponse(client,"200 OK","text/plain","Calibration saved and flash verified");return;
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
