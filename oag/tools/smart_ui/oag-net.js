'use strict';
OAG.preview=location.protocol==='file:'||globalThis.OAG_PREVIEW===true;
OAG.state={game:1,slot:1,weaponSlot:1,dirty:false,weaponDirty:false,token:1,tab:'combo'};
OAG.message=t=>{OAG.$('message').textContent=t};
OAG.chain=Promise.resolve();OAG.serial=fn=>{const p=OAG.chain.then(fn);OAG.chain=p.catch(()=>{});return p};
OAG.api=async(path,data)=>{if(OAG.preview)return OAG.demoApi(path,data);const r=await fetch(path,data?{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)}:{});const t=await r.text();let j;try{j=JSON.parse(t)}catch{throw Error(t||'البيكو ما ردش')}if(!r.ok)throw Error(j.error||'الطلب ما نجحش');return j};
OAG.post=async(path,data)=>{const a=await OAG.api(path,data);if(!a.ticket)return a;for(let i=0;i<200;i++){await new Promise(r=>setTimeout(r,25));const s=await OAG.api('/api/oag/status');if(s.doneTicket>=a.ticket){if(!s.ok)throw Error(s.error);return s}}throw Error('البيكو اتأخر في الرد؛ جرّب تاني')};
OAG.demoStore=(()=>{try{return JSON.parse(localStorage.getItem('OAG_SMART_DEMO_V1'))||{}}catch{return {}}})();
OAG.demoDraft={};OAG.demoWrites=0;
OAG.demoApi=async(path,data)=>{
const p=new URL(path,'http://oag.local'),q=p.searchParams,g=+(data?.game||q.get('game')||1),s=+(data?.slot||q.get('slot')||1),key=g+':'+s;
if(path==='/api/games')return {names:Array.from({length:20},(_,i)=>i===0?'eFootball':'')};
if(path.startsWith('/api/weapons?'))return {names:Array(24).fill('')};
if(path.startsWith('/api/recoil?'))return {horizontalRaw:0,verticalRaw:0,tickMs:40};
if(path==='/api/oag/status')return {ok:true,runtime:false,ready:true,game:OAG.state.game,writes:OAG.demoWrites};
if(p.pathname==='/api/oag/combo')return OAG.clone((q.get('saved')!=='1'&&OAG.demoDraft['c'+key])||OAG.demoStore['c'+key]||{name:'',enabled:0,mode:0,cancelable:1,branches:[OAG.encodeBranch(OAG.branch())]});
if(p.pathname==='/api/oag/weapon')return {wire:((q.get('saved')!=='1'&&OAG.demoDraft['w'+key])||OAG.demoStore['w'+key]||OAG.weaponWire(OAG.defaultWeapon()))};
if(path.endsWith('combo-begin'))OAG.demoStage={name:data.name,enabled:+data.enabled,mode:+data.mode,cancelable:+data.cancelable,branches:[],key,token:data.token};
if(path.endsWith('combo-branch'))OAG.demoStage.branches[+data.branch]=data.wire;
if(path.endsWith('combo-apply'))OAG.demoDraft['c'+key]=OAG.clone(OAG.demoStage);
if(path.endsWith('weapon-preview'))OAG.demoDraft['w'+key]=data.wire;
if(path.endsWith('combo-save'))OAG.demoStore['c'+key]=OAG.clone(OAG.demoDraft['c'+key]);
if(path.endsWith('weapon-save'))OAG.demoStore['w'+key]=OAG.demoDraft['w'+key];
if(path.endsWith('-save')){OAG.demoWrites++;try{localStorage.setItem('OAG_SMART_DEMO_V1',JSON.stringify(OAG.demoStore))}catch{}}
if(path.endsWith('discard-combo'))for(const k of Object.keys(OAG.demoDraft))if(k[0]==='c')delete OAG.demoDraft[k];
if(path.endsWith('discard-weapon'))for(const k of Object.keys(OAG.demoDraft))if(k[0]==='w')delete OAG.demoDraft[k];
return {ok:true}};
OAG.safe=fn=>async()=>{try{await fn()}catch(e){OAG.message(e.message)}};
OAG.selectNames=(el,list,prefix)=>{el.textContent='';list.forEach((n,i)=>{const o=document.createElement('option');o.value=i+1;o.textContent=n||prefix+' '+(i+1);el.append(o)})};
