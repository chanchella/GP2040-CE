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

constexpr char kDashboardHtml[] = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>OAG ABO GEMI</title><link rel=stylesheet href=/app.css><link rel=stylesheet href=/combo.css></head><body>
<header><h1>OAG ABO GEMI</h1><span>Controller Lab • Smart Anti-Drift • Profiles</span></header><main>
<section class=card><h2>OAG Games & Weapons</h2><label>OAG Game</label><select id=gs></select><input id=gn maxlength=32 placeholder="Type your OAG game name"><button id=sg>ADD OAG GAME 1</button>
<label>OAG Weapon for selected game</label><select id=ws></select><input id=wn maxlength=32 placeholder="Type your OAG weapon name"><button id=sw>ADD OAG WEAPON 1</button>
<h3>OAG WEAPON RECOIL</h3><p class=hint>Move the sliders while testing. No number typing required.</p><div class=two><div><label>Horizontal • Left ↔ Right</label><input id=rh type=range min=-200 max=200 step=1 value=0><div class=rangeval id=rhv>0.00</div><div class=ends><span>-2.00 LEFT</span><span>+2.00 RIGHT</span></div></div><div><label>Vertical • Up ↔ Down</label><input id=rv type=range min=-200 max=200 step=1 value=0><div class=rangeval id=rvv>0.00</div><div class=ends><span>-2.00 UP</span><span>+2.00 DOWN</span></div></div></div>
<label>Recoil Tick (ms)</label><input id=rt type=number min=1 max=1000 step=1 value=40><button id=sr>SAVE OAG WEAPON RECOIL</button><div id=nmsg class=msg></div></section>

<section class="card combocard" dir=rtl><h2>كومبوهات OAG</h2><label>لعبة الكومبو OAG</label><select id=cg></select><label>الكومبو الخاص باللعبة المختارة</label><select id=cs></select><input id=cn maxlength=32 placeholder="اكتب اسم كومبو OAG"><button id=sc>إضافة كومبو OAG 1</button>
<h3>إعداد كومبو OAG</h3>
<div class=triggerline><strong>عند الضغط على</strong><select id=ctr></select><select id=ctmode><option value=press>ضغطة</option><option value=hold>استمرار بالضغط</option></select><select id=ctlife><option value=ms>لمدة بالمللي ثانية</option><option value=end>حتى نهاية الكومبو</option><option value=again>حتى أضغط الزر مرة أخرى</option><option value=release>حتى أرفع يدي عن الزر</option></select><input id=ctms type=number min=0 max=60000 step=1 value=0 inputmode=numeric placeholder="ms"></div>
<div class=equalsline>=</div>
<p class=hint>لكل حركة OAG: اختر الزر، وحدد مدة الضغط أو الاستمرار، ثم مدة التحرير قبل الحركة التالية.</p>
<div id=csteps class=actionlist></div>
<button id=sct>حفظ كومبو OAG</button><div id=cmsg class=msg></div>
</section>

<section class=card><h2>Active Profile</h2><div class=two><div><label>Game</label><select id=ag></select></div><div><label>Weapon</label><select id=aw></select></div></div><button id=sp>SAVE ACTIVE PROFILE</button></section>
<section class=card><h2>System</h2><p id=gen class=hint>Loading...</p><button id=play class=play>SAVE & PLAY</button><div id=pmsg class=msg></div></section>
</main><script src=/app.js></script></body></html>)HTML";

constexpr char kAppCss[] = R"CSS(*{box-sizing:border-box}body{margin:0;background:#070b12;color:#eef2ff;font:15px Arial,sans-serif}header{padding:20px;background:#111827;border-bottom:1px solid #263247}h1{margin:0;font-size:30px}header span,.hint{color:#94a3b8}main{max-width:900px;margin:auto;padding:14px}.card{background:#111827;border:1px solid #263247;border-radius:15px;padding:16px;margin-bottom:13px}h2{margin:0 0 12px}h3{text-align:center;font-size:13px;color:#cbd5e1;letter-spacing:.5px}label{display:block;margin:10px 0 5px;color:#cbd5e1}input,select,button{width:100%;padding:11px;border-radius:9px;border:1px solid #334155;background:#0f172a;color:white}input[type=range]{padding:0;accent-color:#86efac}button{margin-top:10px;background:#e5e7eb;color:#111827;font-weight:800}.accent,.play{background:#86efac;color:#052e16}.ghost{background:#1f2937;color:#e5e7eb}.two,.pads{display:grid;grid-template-columns:1fr 1fr;gap:14px}.three{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.gamepad{padding:12px;border:1px solid #29364b;border-radius:24px;background:#0b1220}.pad{width:min(36vw,230px);height:min(36vw,230px);margin:auto;border:2px solid #475569;border-radius:50%;position:relative;background:radial-gradient(circle,#172033 0,#0b1220 70%);overflow:hidden}.pad:before,.pad:after{content:"";position:absolute;background:#334155;z-index:0}.pad:before{width:1px;height:100%;left:50%}.pad:after{height:1px;width:100%;top:50%}.pad i{position:absolute;border-radius:50%;transform:translate(-50%,-50%);z-index:4}.pad .raw{width:18px;height:18px;background:#86efac;box-shadow:0 0 12px #86efac}.pad .filtered{width:10px;height:10px;background:#60a5fa;box-shadow:0 0 8px #60a5fa}.center{position:absolute;width:12px;height:12px;border:2px solid #f8fafc;border-radius:50%;transform:translate(-50%,-50%);z-index:3}.dzring{position:absolute;border:2px dashed #fbbf24;border-radius:50%;transform:translate(-50%,-50%);z-index:2;pointer-events:none}.mono{font:12px monospace;color:#94a3b8;text-align:center;margin:7px}.metric{text-align:center}.metric b{font-size:18px;color:#86efac}.metric small{display:block;color:#64748b}.legend{display:flex;justify-content:center;gap:14px;flex-wrap:wrap;margin:12px 0;color:#94a3b8;font-size:11px}.legend i{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:5px}.rawkey{background:#86efac}.filterkey{background:#60a5fa}.centerkey{border:2px solid #f8fafc}.shoulders,.face,.systembuttons{display:flex;gap:7px;justify-content:center;flex-wrap:wrap;margin:8px}.shoulders{justify-content:space-between}.shoulders span,.face span,.systembuttons span{min-width:38px;text-align:center;padding:7px 9px;border-radius:9px;border:1px solid #334155;background:#111827;color:#64748b;font-weight:800}.on{background:#86efac!important;color:#052e16!important;border-color:#86efac!important;box-shadow:0 0 10px #86ef9666}.trigs{display:grid;grid-template-columns:1fr 1fr;gap:15px;margin:12px}.trigs em{display:block;height:7px;background:#1e293b;border-radius:5px;overflow:hidden}.trigs em i{display:block;height:100%;width:0;background:#86efac}.wizard{margin-top:15px;padding-top:8px;border-top:1px solid #263247}.rangeval{text-align:center;font-size:22px;font-weight:900;color:#86efac;margin:5px}.ends{display:flex;justify-content:space-between;color:#64748b;font-size:10px}.equation{display:grid;grid-template-columns:auto minmax(260px,1fr) auto;gap:10px;align-items:center;margin:10px 0 6px}.equation strong{font-size:18px;color:#86efac;white-space:nowrap}.triggerselect{margin:0}.timelinewrap{overflow-x:auto;padding:8px 2px 14px}.timeline{display:flex;align-items:stretch;gap:34px;min-width:max-content}.step{width:270px;flex:0 0 270px;border:1px solid #334155;border-radius:12px;padding:11px;position:relative;background:#0b1220}.step:not(:last-child):after{content:"+";position:absolute;right:-25px;top:50%;transform:translateY(-50%);font-size:30px;font-weight:900;color:#86efac}.stephead{display:flex;justify-content:space-between;align-items:center;font-weight:900;color:#86efac;margin-bottom:7px}.action3{display:grid;grid-template-columns:1.35fr 1fr .8fr;gap:8px;align-items:end}.action3 label{margin-top:0}.action3 select,.action3 input{margin-bottom:0}.multi{min-height:150px}.step button{margin-top:7px}.status,.msg{margin:9px 0;color:#86efac}.bad{color:#fca5a5}@media(max-width:560px){.card{padding:12px}.pad{width:40vw;height:40vw}.two,.three{grid-template-columns:1fr}.triggerline{grid-template-columns:auto 1fr auto}.triggerline #ctmode,.triggerline #ctlife,.triggerline #ctms{grid-column:2/3}.triggerline strong:last-child{grid-column:3/4;grid-row:1}.actionfields{grid-template-columns:1fr 1fr}.actionfields .durationbox{grid-column:1/2}.actionfields .msbox{grid-column:2/3}.trigs{margin:10px 0}})CSS";
constexpr char kComboCss[] = R"CSS(.triggerline{display:grid;grid-template-columns:auto minmax(180px,1.4fr) minmax(110px,.8fr) minmax(165px,1.1fr) 105px;gap:8px;align-items:center;margin:10px 0}.triggerline strong{color:#86efac;white-space:nowrap;font-size:17px}.triggerline select,.triggerline input{margin:0}.equalsline{text-align:center;color:#86efac;font-size:30px;font-weight:900;line-height:38px;margin:2px 0 8px}.actionlist{display:flex;flex-direction:column;gap:0;margin:12px 0}.actionrow{border:1px solid #334155;border-radius:12px;padding:10px;background:#0b1220}.actiontitle{font-weight:800;color:#86efac;margin-bottom:7px}.actionfields{display:grid;grid-template-columns:1.25fr .75fr 1.15fr;gap:8px;align-items:start}.actionfields label{font-size:11px;margin:0 0 4px}.actionfields select,.actionfields input{margin:0}.pressbox .unitbox{margin-top:7px}.pressbox .releaselabel{display:block;margin-top:9px;color:#cbd5e1}.plusline{text-align:center;color:#86efac;font-size:27px;font-weight:900;line-height:32px}.hiddenms{display:none}.unitbox{position:relative}.unitbox input{padding-right:34px}.unitbox span{position:absolute;right:9px;bottom:12px;color:#94a3b8;font-size:11px;pointer-events:none}@media(max-width:560px){.triggerline{grid-template-columns:auto 1fr}.triggerline strong{grid-column:1/2;grid-row:1}.triggerline #ctr{grid-column:2/3;grid-row:1}.triggerline #ctmode,.triggerline #ctlife,.triggerline #ctms{grid-column:2/3}.actionfields{grid-template-columns:1fr 1fr}.actionfields .buttonbox{grid-column:1/3}.actionfields .modebox{grid-column:1/2}.actionfields .pressbox{grid-column:2/3}.actionfields .pressbox{grid-column:2/3}.equalsline{font-size:34px;margin:0 0 10px}})CSS";

constexpr char kAppJs[] = R"JS(const $=x=>document.getElementById(x);let cfg,games=[],weapons=[],combos=[];
function opts(e,n,p){e.innerHTML='';for(let i=1;i<=n;i++){let o=document.createElement('option');o.value=i;o.textContent=p+' '+i;e.appendChild(o)}}
opts($('gs'),20,'ADD OAG GAME');opts($('cg'),20,'OAG COMBO GAME');opts($('ws'),24,'ADD OAG WEAPON');opts($('cs'),16,'ADD OAG COMBO');
const enc=d=>new URLSearchParams(d);async function post(p,d){let r=await fetch(p,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:enc(d)}),t=await r.text();if(!r.ok)throw Error(t);return t}
async function json(p){let r=await fetch(p,{cache:'no-store'});if(!r.ok)throw Error(await r.text());return r.json()}
function names(e,a,p){e.innerHTML='';a.forEach((n,i)=>{let o=document.createElement('option');o.value=i+1;o.textContent=n||p+' '+(i+1);e.appendChild(o)})}
function comboGameNames(e,a){e.innerHTML='';a.forEach((n,i)=>{let o=document.createElement('option');o.value=i+1;o.textContent='OAG COMBO GAME '+(i+1)+(n?' — '+n:'');e.appendChild(o)})}
async function load(){cfg=await json('/api/config');$('gen').textContent='Flash generation: '+cfg.generation;games=(await json('/api/games')).names;names($('gs'),games,'ADD OAG GAME');names($('ag'),games,'OAG GAME');comboGameNames($('cg'),games);$('ag').value=cfg.activeGame+1;$('cg').value=cfg.activeGame+1;await loadWeapons($('ag').value);$('aw').value=cfg.activeWeapon>=0?cfg.activeWeapon+1:'';await loadCombos($('cg').value);showGameName();showComboName();updateAddButtons()}
async function loadCombos(g){combos=(await json('/api/combos?game='+g)).names;names($('cs'),combos,'ADD OAG COMBO');showComboName();updateAddButtons()}
async function loadWeapons(g){weapons=(await json('/api/weapons?game='+g)).names;names($('ws'),weapons,'ADD OAG WEAPON');names($('aw'),weapons,'OAG WEAPON');showWeaponName();updateAddButtons();if(typeof loadWeaponSettings==='function')await loadWeaponSettings()}
function recoilLive(){let h=$('rh'),v=$('rv'),hv=$('rhv'),vv=$('rvv');if(!h||!v||!hv||!vv)return;let draw=()=>{let x=Number(h.value)/100,y=Number(v.value)/100;hv.textContent=(x>=0?'+':'')+x.toFixed(2);vv.textContent=(y>=0?'+':'')+y.toFixed(2)};h.addEventListener('input',draw);v.addEventListener('input',draw);h.addEventListener('change',draw);v.addEventListener('change',draw);draw()}recoilLive();

function oagLoadScript(src,attempt=0){return new Promise((resolve,reject)=>{let s=document.createElement('script');s.src=src;s.onload=()=>resolve();s.onerror=()=>{s.remove();if(attempt<20)setTimeout(()=>oagLoadScript(src,attempt+1).then(resolve,reject),150);else reject(new Error('Failed to load '+src))};document.body.appendChild(s)})}
(async()=>{try{await oagLoadScript('/gwc.js');await oagLoadScript('/gwc-editor.js');await oagLoadScript('/gwc-save.js');await oagLoadScript('/names.js')}catch(e){$('cmsg').textContent=e.message}})();
)JS";


constexpr char kGwcJs[] = R"JS(const controls=[['A / Cross',1],['B / Circle',2],['X / Square',3],['Y / Triangle',4],['LB / L1',5],['RB / R1',6],['LT / L2',7],['RT / R2',8],['L3 / ضغطة الأنالوج الأيسر',9],['R3 / ضغطة الأنالوج الأيمن',10],['Back / View / Select / Share / Create',11],['Start / Menu',12],['Guide / Home',13],['D-Pad أعلى',14],['D-Pad أسفل',15],['D-Pad يسار',16],['D-Pad يمين',17]];
const actionChoices=[];controls.forEach(x=>actionChoices.push(['يد التحكم • '+x[0],'c'+x[1]]));[['الماوس • كليك شمال','m1'],['الماوس • كليك يمين','m2'],['الماوس • ضغطة Scroll','m4'],['الماوس • زر خلفي','m8'],['الماوس • زر أمامي','m16'],['الماوس • Scroll لأعلى','wu'],['الماوس • Scroll لأسفل','wd']].forEach(x=>actionChoices.push(x));[['الكيبورد • Ctrl','md1'],['الكيبورد • Shift','md2'],['الكيبورد • Alt','md4']].forEach(x=>actionChoices.push(x));for(let i=0;i<26;i++)actionChoices.push(['الكيبورد • '+String.fromCharCode(65+i),'k'+(4+i)]);for(let i=1;i<=9;i++)actionChoices.push(['الكيبورد • '+i,'k'+(29+i)]);actionChoices.push(['الكيبورد • 0','k39'],['الكيبورد • Enter','k40'],['الكيبورد • Escape','k41'],['الكيبورد • Backspace','k42'],['الكيبورد • Tab','k43'],['الكيبورد • Space','k44']);for(let i=1;i<=12;i++)actionChoices.push(['الكيبورد • F'+i,'k'+(57+i)]);actionChoices.push(['الكيبورد • سهم يمين','k79'],['الكيبورد • سهم يسار','k80'],['الكيبورد • سهم لأسفل','k81'],['الكيبورد • سهم لأعلى','k82']);
function fillChoice(e,placeholder){e.innerHTML='';e.add(new Option(placeholder,''));actionChoices.forEach(x=>e.add(new Option(x[0],x[1])))}
function firstAction(s){if(s.logicalMask){for(let i=1;i<=17;i++)if((s.logicalMask&(1<<i))!==0)return'c'+i}if(s.control)return'c'+s.control;if(s.modifiers){for(const m of [1,2,4])if(s.modifiers&m)return'md'+m}if(s.keys)for(const k of s.keys)if(k)return'k'+k;if(s.mouseButtons){for(const m of [1,2,4,8,16])if(s.mouseButtons&m)return'm'+m}if(s.mouseWheel>0)return'wu';if(s.mouseWheel<0)return'wd';return''}
function comboTriggerValue(d){if(d.logicalTrigger)return'c'+d.logicalTrigger;if(d.keyboardModifiersTrigger)return'md'+d.keyboardModifiersTrigger;if(d.keyboardTrigger)return'k'+d.keyboardTrigger;if(d.mouseTrigger){if(d.mouseTrigger===32)return'wu';if(d.mouseTrigger===64)return'wd';return'm'+d.mouseTrigger}return''}
function triggerFields(v,d){d.logicalTrigger=0;d.keyboardTrigger=0;d.keyboardModifiersTrigger=0;d.mouseTrigger=0;if(!v)return;if(v[0]==='c')d.logicalTrigger=+v.slice(1);else if(v.startsWith('md'))d.keyboardModifiersTrigger=+v.slice(2);else if(v[0]==='k')d.keyboardTrigger=+v.slice(1);else if(v[0]==='m')d.mouseTrigger=+v.slice(1);else if(v==='wu')d.mouseTrigger=32;else if(v==='wd')d.mouseTrigger=64}
fillChoice($('ctr'),'اختر زر تشغيل الكومبو...');
function syncMs(select,input){input.classList.toggle('hiddenms',select.value!=='ms')}$('ctlife').onchange=()=>syncMs($('ctlife'),$('ctms'));syncMs($('ctlife'),$('ctms'));
)JS";
constexpr char kGwcEditorJs[] = R"JS(function actionRow(index,s=null){let d=document.createElement('div');d.className='actionrow';d.innerHTML='<div class=actiontitle>حركة OAG '+(index+1)+'</div><div class=actionfields><div class=buttonbox><label>الزر</label><select class=sact></select></div><div class=modebox><label>نوع الحركة</label><select class=smode><option value=press>ضغط</option><option value=hold>استمرار</option><option value=pulse>ضغط نبضي متكرر</option></select></div><div class=pressbox><label>مدة الضغط / الاستمرار</label><select class=slife><option value=ms>بالمللي ثانية</option><option value=release>حتى أرفع يدي عن زر التشغيل</option><option value=end>حتى نهاية الكومبو</option></select><div class=unitbox><input class=sms type=number min=1 max=60000 step=1 value=100 inputmode=numeric><span>ms</span></div><label class=releaselabel>مدة التحرير قبل الحركة التالية</label><div class=unitbox><input class=srel type=number min=0 max=60000 step=1 value=0 inputmode=numeric><span>ms</span></div></div></div>';fillChoice(d.querySelector('.sact'),'غير مستخدم');let life=d.querySelector('.slife'),ms=d.querySelector('.sms'),mode=d.querySelector('.smode'),rel=d.querySelector('.srel'),rl=d.querySelector('.releaselabel');let sync=()=>{let finite=life.value==='ms';if((mode.value==='press'||mode.value==='pulse')&&life.value!=='ms')life.value='ms';life.querySelector('option[value=release]').disabled=mode.value!=='hold';life.querySelector('option[value=end]').disabled=mode.value!=='hold';finite=life.value==='ms';ms.parentElement.classList.toggle('hiddenms',!finite);rel.parentElement.classList.toggle('hiddenms',!finite);rl.classList.toggle('hiddenms',!finite);rel.disabled=!finite};life.onchange=sync;mode.onchange=sync;if(s){d.querySelector('.sact').value=firstAction(s);if(s.kind===1){mode.value='hold';if(s.delayAfterMs===65535){life.value='end';rel.value=0}else if(s.durationMs===0){life.value='release';rel.value=0}else{life.value='ms';ms.value=s.durationMs||100;rel.value=s.delayAfterMs||0}}else if(s.kind===3){mode.value='pulse';life.value='ms';ms.value=s.durationMs||100;rel.value=s.intervalMs||0}else{mode.value='press';life.value='ms';ms.value=s.durationMs||100;rel.value=s.delayAfterMs||0}}sync();$('csteps').appendChild(d);if(index<7){let p=document.createElement('div');p.className='plusline';p.textContent='+';$('csteps').appendChild(p)}}
function buildEight(steps=[]){$('csteps').innerHTML='';for(let i=0;i<8;i++)actionRow(i,steps[i]||null)}
buildEight();
)JS";constexpr char kGwcSaveJs[] = R"JS(function paintRecoilValues(){let h=Number($('rh').value)/100,v=Number($('rv').value)/100;$('rhv').textContent=(h>=0?'+':'')+h.toFixed(2);$('rvv').textContent=(v>=0?'+':'')+v.toFixed(2)}
async function loadWeaponSettings(){try{let g=$('gs').value,w=$('ws').value,d=await json('/api/recoil?game='+g+'&weapon='+w);$('rh').value=d.horizontalRaw;$('rv').value=d.verticalRaw;$('rt').value=d.tickMs;paintRecoilValues()}catch(e){$('nmsg').textContent=e.message}}
async function loadComboTiming(){try{let d=await json('/api/combo-program?game='+$('cg').value+'&slot='+$('cs').value);$('ctr').value=comboTriggerValue(d);$('ctmode').value=d.activation===0?'hold':'press';let steps=d.steps||[],visible=steps;if(steps.length&&steps[0].kind===4&&steps[0].control===0&&!steps[0].logicalMask&&!steps[0].mouseButtons&&!steps[0].mouseWheel){$('ctlife').value='ms';$('ctms').value=steps[0].durationMs||0;visible=steps.slice(1)}else if(d.activation===2||d.cancelAgain){$('ctlife').value='again';$('ctms').value=0}else if(d.cancelRelease){$('ctlife').value='release';$('ctms').value=0}else{$('ctlife').value='end';$('ctms').value=0}syncMs($('ctlife'),$('ctms'));buildEight(visible.slice(0,8))}catch(e){$('cmsg').textContent=e.message}}
$('sr').onclick=async()=>{try{await post('/api/recoil',{game:$('gs').value,weapon:$('ws').value,horizontal:$('rh').value,vertical:$('rv').value,tickMs:$('rt').value});$('nmsg').textContent='OAG Weapon recoil saved to Flash';await loadWeaponSettings()}catch(e){$('nmsg').textContent=e.message}};
function writeAction(d,i,a,mode,life,ms,releaseMs){let kind=0,duration=0,delay=0,interval=0,repeatCount=1;if(!Number.isFinite(releaseMs)||releaseMs<0||releaseMs>60000)throw Error('مدة التحرير يجب أن تكون من 0 إلى 60000 ms');if(life==='ms'){if(!Number.isFinite(ms)||ms<1||ms>60000)throw Error('مدة الضغط أو الاستمرار يجب أن تكون من 1 إلى 60000 ms');duration=Math.round(ms);if(mode==='pulse'){kind=3;interval=Math.round(releaseMs);repeatCount=0}else{kind=mode==='hold'?1:0;delay=Math.round(releaseMs)}}else{if(mode!=='hold')throw Error('خيارات حتى الرفع أو نهاية الكومبو متاحة مع الاستمرار فقط');kind=1;duration=0;delay=life==='end'?65535:0}d['s'+i+'k']=kind;d['s'+i+'c']=0;d['s'+i+'d']=duration;d['s'+i+'w']=delay;d['s'+i+'i']=interval;d['s'+i+'r']=repeatCount;d['s'+i+'lm']=0;d['s'+i+'md']=0;d['s'+i+'mb']=0;d['s'+i+'mw']=0;for(let q=0;q<6;q++)d['s'+i+'q'+q]=0;if(a[0]==='c')d['s'+i+'lm']=1<<+a.slice(1);else if(a.startsWith('md'))d['s'+i+'md']=+a.slice(2);else if(a[0]==='k')d['s'+i+'q0']=+a.slice(1);else if(a[0]==='m')d['s'+i+'mb']=+a.slice(1);else if(a==='wu')d['s'+i+'mw']=1;else if(a==='wd')d['s'+i+'mw']=-1}
$('sct').onclick=async()=>{try{let triggerLife=$('ctlife').value,activation=triggerLife==='again'?2:($('ctmode').value==='hold'?0:1);let d={game:$('cg').value,slot:$('cs').value,activation:activation,repeat:0,pass:1,cancelRelease:triggerLife==='release'||($('ctmode').value==='hold'&&triggerLife==='ms')?1:0,cancelAgain:0,cancelEnabled:0,cancelControl:0,logicalTrigger:0,keyboardTrigger:0,keyboardModifiersTrigger:0,mouseTrigger:0,stepCount:0};triggerFields($('ctr').value,d);if(!d.logicalTrigger&&!d.keyboardTrigger&&!d.keyboardModifiersTrigger&&!d.mouseTrigger)throw Error('اختر زر تشغيل الكومبو');let i=0;if($('ctlife').value==='ms'){let t=Number($('ctms').value);if(!Number.isFinite(t)||t<0||t>60000)throw Error('مدة زر التشغيل يجب أن تكون من 0 إلى 60000 ms');if(t>0){d['s0k']=4;d['s0c']=0;d['s0d']=Math.round(t);d['s0w']=0;d['s0i']=0;d['s0r']=1;d['s0lm']=0;d['s0md']=0;d['s0mb']=0;d['s0mw']=0;for(let q=0;q<6;q++)d['s0q'+q]=0;i=1}}for(const r of document.querySelectorAll('.actionrow')){let a=r.querySelector('.sact').value;if(!a)continue;writeAction(d,i++,a,r.querySelector('.smode').value,r.querySelector('.slife').value,Number(r.querySelector('.sms').value),Number(r.querySelector('.srel').value))}if(i===0)throw Error('اختر حركة OAG واحدة على الأقل');d.stepCount=i;await post('/api/combo-program',d);$('cmsg').textContent='تم حفظ كومبو OAG بنجاح';await loadComboTiming()}catch(e){$('cmsg').textContent=e.message}};
)JS";
constexpr char kNamesJs[] = R"JS(function showGameName(){$('gn').value=games[+$('gs').value-1]||'';updateAddButtons()}function showWeaponName(){$('wn').value=weapons[+$('ws').value-1]||'';updateAddButtons()}function showComboName(){$('cn').value=combos[+$('cs').value-1]||'';updateAddButtons()}
function updateAddButtons(){let g=+$('gs').value||1,w=+$('ws').value||1,c=+$('cs').value||1;$('sg').textContent=games[g-1]?'UPDATE '+games[g-1]:'ADD OAG GAME '+g;$('sw').textContent=weapons[w-1]?'UPDATE '+weapons[w-1]:'ADD OAG WEAPON '+w;$('sc').textContent=combos[c-1]?'تحديث '+combos[c-1]:'إضافة كومبو OAG '+c}
$('gs').onchange=async()=>{showGameName();await loadWeapons($('gs').value)};$('ws').onchange=()=>{showWeaponName();loadWeaponSettings()};$('cs').onchange=()=>{showComboName();loadComboTiming()};$('cg').onchange=async()=>{await loadCombos($('cg').value);await loadComboTiming()};$('ag').onchange=async()=>{await loadWeapons($('ag').value)};
$('sg').onclick=async()=>{try{let slot=$('gs').value;await post('/api/name',{type:'game',slot,name:$('gn').value});games=(await json('/api/games')).names;names($('gs'),games,'ADD OAG GAME');names($('ag'),games,'OAG GAME');$('gs').value=slot;showGameName();$('nmsg').textContent='OAG Game saved'}catch(e){$('nmsg').textContent=e.message}};
$('sw').onclick=async()=>{try{let slot=$('ws').value;await post('/api/name',{type:'weapon',game:$('gs').value,slot,name:$('wn').value});await loadWeapons($('gs').value);$('ws').value=slot;showWeaponName();$('nmsg').textContent='OAG Weapon saved'}catch(e){$('nmsg').textContent=e.message}};
$('sc').onclick=async()=>{try{let slot=$('cs').value;await post('/api/name',{type:'combo',game:$('cg').value,slot,name:$('cn').value});await loadCombos($('cg').value);$('cs').value=slot;showComboName();$('nmsg').textContent='OAG Combo saved'}catch(e){$('nmsg').textContent=e.message}};
$('sp').onclick=async()=>{try{await post('/api/profile',{game:$('ag').value,weapon:$('aw').value,enabled:1});$('nmsg').textContent='Active profile saved';await load()}catch(e){$('nmsg').textContent=e.message}};
$('play').onclick=async()=>{try{await post('/api/save-play',{});$('pmsg').textContent='Saved. Restarting into Gaming Mode...'}catch(e){$('pmsg').textContent=e.message}};
load().then(()=>loadComboTiming()).catch(e=>$('gen').textContent=e.message);
)JS";

static_assert(sizeof(kDashboardHtml) + 640u < TCP_SND_BUF);
static_assert(sizeof(kAppCss) + 640u < TCP_SND_BUF);
static_assert(sizeof(kComboCss) + 640u < TCP_SND_BUF);
static_assert(sizeof(kAppJs) + 640u < TCP_SND_BUF);
static_assert(sizeof(kNamesJs) + 640u < TCP_SND_BUF);
static_assert(sizeof(kGwcJs) + 640u < TCP_SND_BUF);
static_assert(sizeof(kGwcEditorJs) + 640u < TCP_SND_BUF);
static_assert(sizeof(kGwcSaveJs) + 640u < TCP_SND_BUF);

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

bool DiamondWifiPortal::start(DiamondConfigStore& store, DiamondGameLibraryStore& games){
    if(started_)return true;store_=&store;games_=&games;if(cyw43_arch_init()!=0)return false;
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
    if(!games_){sendResponse(client,"503 Service Unavailable","text/plain","OAG game library unavailable");return;}
    const auto loadGame=[&](std::uint32_t oneBased)->bool{
        return oneBased>=1&&oneBased<=oag::kDiamondLibraryGameSlots&&
            games_->loadGame(oneBased-1,pc,scratchGame_);
    };

    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/")){sendResponse(client,"200 OK","text/html; charset=utf-8",kDashboardHtml);return;}
    if(!std::strcmp(method,"GET")&&staticPath(path,"/app.css")){sendResponse(client,"200 OK","text/css; charset=utf-8",kAppCss);return;}
    if(!std::strcmp(method,"GET")&&staticPath(path,"/combo.css")){sendResponse(client,"200 OK","text/css; charset=utf-8",kComboCss);return;}
    if(!std::strcmp(method,"GET")&&staticPath(path,"/app.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kAppJs);return;}
    if(!std::strcmp(method,"GET")&&staticPath(path,"/names.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kNamesJs);return;}
    if(!std::strcmp(method,"GET")&&staticPath(path,"/gwc.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kGwcJs);return;}
    if(!std::strcmp(method,"GET")&&staticPath(path,"/gwc-editor.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kGwcEditorJs);return;}
    if(!std::strcmp(method,"GET")&&staticPath(path,"/gwc-save.js")){sendResponse(client,"200 OK","application/javascript; charset=utf-8",kGwcSaveJs);return;}

    if(!std::strcmp(method,"GET")&&!std::strcmp(path,"/api/games")){
        static std::array<std::array<char,oag::kDiamondDisplayNameBytes>,oag::kDiamondLibraryGameSlots> gameNames{};
        for(std::size_t i=0;i<gameNames.size();++i){
            if(!games_->loadGame(i,pc,scratchGame_)){sendResponse(client,"500 Internal Server Error","text/plain","Game library read failed");return;}
            gameNames[i]=scratchGame_.gameName;
        }
        sendNames(client,gameNames);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/combos?game=",17)){
        const auto g=static_cast<std::uint32_t>(std::strtoul(path+17,nullptr,10));
        if(!loadGame(g)){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG game");return;}
        sendNames(client,scratchGame_.comboNames);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/weapons?game=",18)){
        const auto g=static_cast<std::uint32_t>(std::strtoul(path+18,nullptr,10));
        if(!loadGame(g)){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG game");return;}
        sendNames(client,scratchGame_.weaponNames);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/recoil?game=",17)){
        const char* wp=std::strstr(path,"&weapon=");
        if(!wp){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG weapon");return;}
        const auto g=static_cast<std::uint32_t>(std::strtoul(path+17,nullptr,10));
        const auto w=static_cast<std::uint32_t>(std::strtoul(wp+8,nullptr,10));
        if(!loadGame(g)||w<1||w>oag::kDiamondWeaponSlotsPerGame){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG weapon");return;}
        const auto& rr=scratchGame_.weapons[w-1];char j[256]{};
        std::snprintf(j,sizeof(j),"{\"horizontalRaw\":%ld,\"verticalRaw\":%ld,\"tickMs\":%u,\"enabled\":%s}",
            static_cast<long>(rr.horizontalHalfPermille),static_cast<long>(rr.verticalHalfPermille),rr.tickMs,rr.enabled?"true":"false");
        sendResponse(client,"200 OK","application/json",j);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/combo-program?game=",24)){
        const char* sp=std::strstr(path,"&slot=");
        if(!sp){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo");return;}
        const auto g=static_cast<std::uint32_t>(std::strtoul(path+24,nullptr,10));
        const auto slot=static_cast<std::uint32_t>(std::strtoul(sp+6,nullptr,10));
        if(!loadGame(g)||slot<1||slot>oag::kDiamondComboSlots){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo");return;}
        const auto& p=scratchGame_.comboPrograms[slot-1];
        std::uint16_t logical=0,key=0,mouse=0;std::uint8_t keyMods=0;
        for(const auto& t:p.triggers){
            if(!t.enabled)continue;
            if(t.kind==oag::DiamondComboTriggerKind::LogicalControl&&!logical)logical=t.code;
            else if(t.kind==oag::DiamondComboTriggerKind::KeyboardUsage&&!key&&!keyMods){key=t.code;keyMods=t.modifiers;}
            else if(t.kind==oag::DiamondComboTriggerKind::MouseButton&&!mouse)mouse=t.code;
            else if(t.kind==oag::DiamondComboTriggerKind::MouseWheel&&!mouse)mouse=t.code==1?32:64;
        }
        static char j[6144];std::size_t used=0;
        int n=std::snprintf(j,sizeof(j),"{\"activation\":%u,\"repeat\":%u,\"passTrigger\":%s,\"cancelRelease\":%s,\"cancelAgain\":%s,\"cancelEnabled\":%s,\"cancelControl\":%u,\"logicalTrigger\":%u,\"keyboardTrigger\":%u,\"keyboardModifiersTrigger\":%u,\"mouseTrigger\":%u,\"steps\":[",
            static_cast<unsigned>(p.activation),static_cast<unsigned>(p.repeat),p.passTriggerThrough?"true":"false",p.cancelOnTriggerRelease?"true":"false",p.cancelOnTriggerPressAgain?"true":"false",p.cancelControlEnabled?"true":"false",static_cast<unsigned>(p.cancelControl),logical,key,keyMods,mouse);
        if(n<0||static_cast<std::size_t>(n)>=sizeof(j)){sendResponse(client,"500 Internal Server Error","text/plain","Combo encode failed");return;}used=static_cast<std::size_t>(n);
        for(std::uint8_t i=0;i<p.stepCount&&i<oag::kDiamondComboSteps;i++){
            const auto& step=p.steps[i];
            n=std::snprintf(j+used,sizeof(j)-used,"%s{\"kind\":%u,\"control\":%u,\"durationMs\":%u,\"delayAfterMs\":%u,\"intervalMs\":%u,\"repeatCount\":%u,\"logicalMask\":%lu,\"modifiers\":%u,\"mouseButtons\":%u,\"mouseWheel\":%d,\"keys\":[%u,%u,%u,%u,%u,%u]}",
                i?",":"",static_cast<unsigned>(step.kind),static_cast<unsigned>(step.control),step.durationMs,step.delayAfterMs,step.intervalMs,step.repeatCount,
                static_cast<unsigned long>(step.logicalMask),step.keyboardModifiers,step.mouseButtons,static_cast<int>(step.mouseWheel),
                step.keyboardKeys[0],step.keyboardKeys[1],step.keyboardKeys[2],step.keyboardKeys[3],step.keyboardKeys[4],step.keyboardKeys[5]);
            if(n<0||static_cast<std::size_t>(n)>=sizeof(j)-used){sendResponse(client,"500 Internal Server Error","text/plain","Combo encode failed");return;}used+=static_cast<std::size_t>(n);
        }
        if(used+3>=sizeof(j)){sendResponse(client,"500 Internal Server Error","text/plain","Combo encode failed");return;}
        j[used++]=']';j[used++]='}';j[used]='\0';
        sendResponse(client,"200 OK","application/json",j);return;
    }
    if(!std::strcmp(method,"GET")&&!std::strncmp(path,"/api/combo?game=",16)){
        const char* sp=std::strstr(path,"&slot=");
        if(!sp){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo");return;}
        const auto g=static_cast<std::uint32_t>(std::strtoul(path+16,nullptr,10));
        const auto slot=static_cast<std::uint32_t>(std::strtoul(sp+6,nullptr,10));
        if(!loadGame(g)||slot<1||slot>oag::kDiamondComboSlots){sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo");return;}
        const auto& t=scratchGame_.comboTiming[slot-1];char j[256]{};
        std::snprintf(j,sizeof(j),"{\"pressMs\":%u,\"delayAfterMs\":%u,\"repeatCount\":%u,\"enabled\":%s}",
            t.pressMs,t.delayAfterMs,t.repeatCount,t.enabled?"true":"false");
        sendResponse(client,"200 OK","application/json",j);return;
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
        if(!formValue(body,"type",type,sizeof(type))||!formValue(body,"name",name,sizeof(name))||!safeName(name)||!parseUnsigned(body,"slot",1,24,slot)){sendResponse(client,"400 Bad Request","text/plain","Invalid name");return;}

        if(!std::strcmp(type,"game")){
            game=slot;
            if(game<1||game>oag::kDiamondLibraryGameSlots||!loadGame(game)){sendResponse(client,"400 Bad Request","text/plain","Invalid game");return;}
            std::memset(scratchGame_.gameName.data(),0,oag::kDiamondDisplayNameBytes);
            std::memcpy(scratchGame_.gameName.data(),name,std::strlen(name));
        }else{
            if(!parseUnsigned(body,"game",1,oag::kDiamondLibraryGameSlots,game)||!loadGame(game)){sendResponse(client,"400 Bad Request","text/plain","Invalid game");return;}
            char* target=nullptr;
            if(!std::strcmp(type,"combo")&&slot<=oag::kDiamondComboSlots)target=scratchGame_.comboNames[slot-1].data();
            else if(!std::strcmp(type,"weapon")&&slot<=oag::kDiamondWeaponSlotsPerGame)target=scratchGame_.weaponNames[slot-1].data();
            if(!target){sendResponse(client,"400 Bad Request","text/plain","Invalid name target");return;}
            std::memset(target,0,oag::kDiamondDisplayNameBytes);std::memcpy(target,name,std::strlen(name));
        }
        if(!games_->saveGame(game-1,scratchGame_)){sendResponse(client,"500 Internal Server Error","text/plain","OAG game Flash save failed");return;}
        sendResponse(client,"200 OK","text/plain","Name saved");return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/recoil")){
        std::uint32_t g=0,w=0,tick=0;std::int32_t horizontal=0,vertical=0;
        if(!parseUnsigned(body,"game",1,oag::kDiamondLibraryGameSlots,g)||!parseUnsigned(body,"weapon",1,oag::kDiamondWeaponSlotsPerGame,w)||
           !parseSigned(body,"horizontal",-200,200,horizontal)||!parseSigned(body,"vertical",-200,200,vertical)||!parseUnsigned(body,"tickMs",1,1000,tick)||!loadGame(g)){
            sendResponse(client,"400 Bad Request","text/plain","Invalid OAG recoil");return;
        }
        auto& rr=scratchGame_.weapons[w-1];
        rr.enabled=true;rr.horizontalHalfPermille=horizontal;rr.verticalHalfPermille=vertical;rr.tickMs=static_cast<std::uint16_t>(tick);
        if(!games_->saveGame(g-1,scratchGame_)){sendResponse(client,"500 Internal Server Error","text/plain","OAG game Flash save failed");return;}
        sendResponse(client,"200 OK","text/plain","OAG recoil saved");return;
    }
    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/combo-program")){
        std::uint32_t game=0,slot=0,activation=0,repeatMode=0,pass=0,cancelRelease=0,cancelAgain=0,cancelEnabled=0,cancelControl=0;
        std::uint32_t logicalTrigger=0,keyboardTrigger=0,keyboardModifiersTrigger=0,mouseTrigger=0,stepCount=0;
        if(!parseUnsigned(body,"game",1,oag::kDiamondLibraryGameSlots,game)||!loadGame(game)||!parseUnsigned(body,"slot",1,oag::kDiamondComboSlots,slot)||
           !parseUnsigned(body,"activation",0,2,activation)||!parseUnsigned(body,"repeat",0,1,repeatMode)||
           !parseUnsigned(body,"pass",0,1,pass)||!parseUnsigned(body,"cancelRelease",0,1,cancelRelease)||
           !parseUnsigned(body,"cancelAgain",0,1,cancelAgain)||!parseUnsigned(body,"cancelEnabled",0,1,cancelEnabled)||
           !parseUnsigned(body,"cancelControl",0,17,cancelControl)||!parseUnsigned(body,"logicalTrigger",0,17,logicalTrigger)||
           !parseUnsigned(body,"keyboardTrigger",0,255,keyboardTrigger)||!parseUnsigned(body,"keyboardModifiersTrigger",0,255,keyboardModifiersTrigger)||
           !parseUnsigned(body,"mouseTrigger",0,64,mouseTrigger)||!parseUnsigned(body,"stepCount",0,oag::kDiamondComboSteps,stepCount)){
            sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo program");return;
        }

        oag::DiamondComboProgram p{};
        p.activation=static_cast<oag::DiamondComboActivationMode>(activation);
        p.repeat=static_cast<oag::DiamondComboRepeatMode>(repeatMode);
        p.passTriggerThrough=pass!=0;p.cancelOnTriggerRelease=cancelRelease!=0;p.cancelOnTriggerPressAgain=cancelAgain!=0;
        p.cancelControlEnabled=cancelEnabled!=0;p.cancelControl=static_cast<oag::DiamondLogicalControl>(cancelControl);
        std::size_t ti=0;
        if(logicalTrigger&&ti<p.triggers.size())p.triggers[ti++]={true,oag::DiamondComboTriggerKind::LogicalControl,static_cast<std::uint16_t>(logicalTrigger),0};
        if((keyboardTrigger||keyboardModifiersTrigger)&&ti<p.triggers.size())p.triggers[ti++]={true,oag::DiamondComboTriggerKind::KeyboardUsage,static_cast<std::uint16_t>(keyboardTrigger),static_cast<std::uint8_t>(keyboardModifiersTrigger)};
        if(mouseTrigger&&ti<p.triggers.size()){
            if(mouseTrigger<=16)p.triggers[ti++]={true,oag::DiamondComboTriggerKind::MouseButton,static_cast<std::uint16_t>(mouseTrigger),0};
            else p.triggers[ti++]={true,oag::DiamondComboTriggerKind::MouseWheel,static_cast<std::uint16_t>(mouseTrigger==32?1:2),0};
        }

        p.stepCount=static_cast<std::uint8_t>(stepCount);
        for(std::uint32_t i=0;i<stepCount;i++){
            char kk[10]{},kc[10]{},kd[10]{},kw[10]{},ki[10]{},kr[10]{},klm[10]{},kmd[10]{},kmb[10]{},kmw[10]{};
            std::snprintf(kk,sizeof(kk),"s%luk",static_cast<unsigned long>(i));std::snprintf(kc,sizeof(kc),"s%luc",static_cast<unsigned long>(i));
            std::snprintf(kd,sizeof(kd),"s%lud",static_cast<unsigned long>(i));std::snprintf(kw,sizeof(kw),"s%luw",static_cast<unsigned long>(i));
            std::snprintf(ki,sizeof(ki),"s%lui",static_cast<unsigned long>(i));std::snprintf(kr,sizeof(kr),"s%lur",static_cast<unsigned long>(i));
            std::snprintf(klm,sizeof(klm),"s%lulm",static_cast<unsigned long>(i));std::snprintf(kmd,sizeof(kmd),"s%lumd",static_cast<unsigned long>(i));
            std::snprintf(kmb,sizeof(kmb),"s%lumb",static_cast<unsigned long>(i));std::snprintf(kmw,sizeof(kmw),"s%lumw",static_cast<unsigned long>(i));
            std::uint32_t kind=0,control=0,duration=0,delay=0,interval=0,repeats=0,lmask=0,mods=0,mb=0;std::int32_t mw=0;
            if(!parseUnsigned(body,kk,0,6,kind)||!parseUnsigned(body,kc,0,17,control)||!parseUnsigned(body,kd,0,60000,duration)||
               !parseUnsigned(body,kw,0,65535,delay)||!parseUnsigned(body,ki,0,60000,interval)||!parseUnsigned(body,kr,0,1000,repeats)||
               !parseUnsigned(body,klm,0,262143,lmask)||!parseUnsigned(body,kmd,0,255,mods)||!parseUnsigned(body,kmb,0,31,mb)||
               !parseSigned(body,kmw,-1,1,mw)){
                sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo step");return;
            }
            auto& s=p.steps[i];s.enabled=true;s.kind=static_cast<oag::DiamondComboStepKind>(kind);s.control=static_cast<oag::DiamondLogicalControl>(control);
            s.durationMs=static_cast<std::uint16_t>(duration);s.delayAfterMs=static_cast<std::uint16_t>(delay);s.intervalMs=static_cast<std::uint16_t>(interval);s.repeatCount=static_cast<std::uint16_t>(repeats);
            s.logicalMask=lmask;s.keyboardModifiers=static_cast<std::uint8_t>(mods);s.mouseButtons=static_cast<std::uint16_t>(mb);s.mouseWheel=static_cast<std::int8_t>(mw);
            for(std::uint32_t q=0;q<6;q++){char kq[12]{};std::snprintf(kq,sizeof(kq),"s%luq%lu",static_cast<unsigned long>(i),static_cast<unsigned long>(q));std::uint32_t usage=0;if(!parseUnsigned(body,kq,0,255,usage)){sendResponse(client,"400 Bad Request","text/plain","Invalid keyboard chord");return;}s.keyboardKeys[q]=static_cast<std::uint8_t>(usage);}
        }
        p.enabled=ti>0&&p.stepCount>0;
        scratchGame_.comboPrograms[slot-1]=p;
        if(!games_->saveGame(game-1,scratchGame_)){sendResponse(client,"500 Internal Server Error","text/plain","OAG game Flash save failed");return;}
        sendResponse(client,"200 OK","text/plain","OAG combo program saved");return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/combo")){
        std::uint32_t game=0,slot=0,press=0,delay=0,repeat=0;
        if(!parseUnsigned(body,"game",1,oag::kDiamondLibraryGameSlots,game)||!loadGame(game)||
           !parseUnsigned(body,"slot",1,oag::kDiamondComboSlots,slot)||!parseUnsigned(body,"pressMs",1,60000,press)||
           !parseUnsigned(body,"delayMs",0,60000,delay)||!parseUnsigned(body,"repeat",1,1000,repeat)){
            sendResponse(client,"400 Bad Request","text/plain","Invalid OAG combo timing");return;
        }
        auto& t=scratchGame_.comboTiming[slot-1];
        t.enabled=true;t.pressMs=static_cast<std::uint16_t>(press);t.delayAfterMs=static_cast<std::uint16_t>(delay);t.repeatCount=static_cast<std::uint16_t>(repeat);
        if(!games_->saveGame(game-1,scratchGame_)){sendResponse(client,"500 Internal Server Error","text/plain","OAG game Flash save failed");return;}
        sendResponse(client,"200 OK","text/plain","OAG combo timing saved");return;
    }

    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/profile")){
        std::uint32_t g=0,w=0,en=0;
        if(!parseUnsigned(body,"game",1,oag::kDiamondLibraryGameSlots,g)||!parseUnsigned(body,"weapon",1,oag::kDiamondWeaponSlotsPerGame,w)||!parseUnsigned(body,"enabled",0,1,en)){
            sendResponse(client,"400 Bad Request","text/plain","Invalid profile");return;
        }
        if(!games_->activate(g-1,pc)){sendResponse(client,"500 Internal Server Error","text/plain","Game load failed");return;}
        const auto oldGame=runtime.activeGame,oldWeapon=runtime.activeWeapon;
        runtime.activeGame=static_cast<std::uint16_t>(g-1);
        runtime.activeWeapon=static_cast<std::uint16_t>(w-1);
        if(!store_->save()){
            runtime.activeGame=oldGame;runtime.activeWeapon=oldWeapon;
            games_->activate(oldGame<oag::kDiamondLibraryGameSlots?oldGame:0,pc);
            sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;
        }
        sendResponse(client,"200 OK","text/plain","Profile saved");return;
    }
    if(!std::strcmp(method,"POST")&&!std::strcmp(path,"/api/save-play")){if(!store_->save()){sendResponse(client,"500 Internal Server Error","text/plain","Flash save failed");return;}sendResponse(client,"200 OK","text/plain","Saved");schedulePlayReboot(1200);return;}
    sendResponse(client,"404 Not Found","text/plain","Not found");
}

} // namespace oag::firmware
