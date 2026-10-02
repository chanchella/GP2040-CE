'use strict';
OAG.sources=['g','k','m','w','s','a'];
OAG.hex=b=>Array.from(new Uint8Array(b),v=>v.toString(16).padStart(2,'0')).join('');
OAG.view=s=>{if(!/^(?:[0-9a-f]{2})+$/i.test(s))throw Error('بيانات OAG مش سليمة');return new DataView(Uint8Array.from(s.match(/../g),v=>parseInt(v,16)).buffer)};
OAG.putControl=(d,o,c)=>{d.setUint8(o,OAG.sources.indexOf(c[0]));d.setUint8(o+1,0);d.setUint16(o+2,Number(c.slice(1)),true)};
OAG.getControl=(d,o)=>OAG.sources[d.getUint8(o)]+d.getUint16(o+2,true);
OAG.encodeBranch=b=>{
if(b.conditions.length>4||b.then.length+b.else.length>16)throw Error('الحد ٤ شروط و١٦ خطوة للفرع');
const d=new DataView(new ArrayBuffer(424)),pool=[];
const refs=list=>{if(list.length<2)throw Error('اختار زرين على الأقل');let at=pool.findIndex((_,i)=>list.every((x,j)=>pool[i+j]===x));if(at<0){at=pool.length;pool.push(...list)}if(pool.length>8)throw Error('الفرع يستوعب ٨ أزرار في قوائم التسلسل والضغط الجماعي');return at};
d.setUint8(0,b.conditions.length);d.setUint8(2,b.then.length);d.setUint8(3,b.else.length);d.setUint8(4,b.enabled);
b.conditions.forEach((c,i)=>{const o=8+i*16;OAG.putControl(d,o,c.control);d.setUint8(o+4,c.kind);d.setUint8(o+5,c.join);d.setUint8(o+6,c.negate);d.setUint8(o+7,c.taps);d.setUint16(o+8,c.window,true);d.setUint16(o+10,c.hold,true);d.setInt16(o+12,c.threshold,true);if(c.kind===8||c.kind===9){if(c.refs[0]!==c.control)throw Error('أول زر في المجموعة لازم يكون زر التشغيل');d.setUint8(o+14,refs(c.refs));d.setUint8(o+15,c.refs.length)}});
[...b.then,...b.else].forEach((a,i)=>{const o=104+i*20;OAG.putControl(d,o,a.control);d.setUint8(o+4,a.kind);d.setUint8(o+5,a.count);d.setUint8(o+6,a.kind===7||a.kind===8?a.first-1:0);d.setUint16(o+8,a.duration,true);d.setUint16(o+10,a.before,true);d.setUint16(o+12,a.after,true);d.setUint16(o+14,a.interval,true);d.setInt16(o+16,a.x,true);d.setInt16(o+18,a.y,true);if(a.kind===6){d.setUint8(o+6,refs(a.refs));d.setUint8(o+5,a.refs.length)}});
d.setUint8(1,pool.length);pool.forEach((c,i)=>OAG.putControl(d,72+i*4,c));
return OAG.hex(d.buffer)};
OAG.decodeBranch=s=>{
const d=OAG.view(s);if(d.byteLength!==424)throw Error('نسخة بيانات الفرع مش مدعومة');
const pool=Array.from({length:d.getUint8(1)},(_,i)=>OAG.getControl(d,72+i*4));
const b={enabled:d.getUint8(4),conditions:[],then:[],else:[]};
for(let i=0;i<d.getUint8(0);i++){const o=8+i*16;b.conditions.push({control:OAG.getControl(d,o),kind:d.getUint8(o+4),join:d.getUint8(o+5),negate:d.getUint8(o+6),taps:d.getUint8(o+7),window:d.getUint16(o+8,true),hold:d.getUint16(o+10,true),threshold:d.getInt16(o+12,true),refs:pool.slice(d.getUint8(o+14),d.getUint8(o+14)+d.getUint8(o+15))})}
for(let i=0;i<d.getUint8(2)+d.getUint8(3);i++){const o=104+i*20,a={control:OAG.getControl(d,o),kind:d.getUint8(o+4),count:d.getUint8(o+5),first:d.getUint8(o+6)+1,duration:d.getUint16(o+8,true),before:d.getUint16(o+10,true),after:d.getUint16(o+12,true),interval:d.getUint16(o+14,true),x:d.getInt16(o+16,true),y:d.getInt16(o+18,true),refs:pool.slice(d.getUint8(o+6),d.getUint8(o+6)+d.getUint8(o+5))};(i<d.getUint8(2)?b.then:b.else).push(a)}
return !b.then.length&&b.conditions[0]?.control==='g0'?OAG.branch():b};
OAG.weaponFields=[['configured',0,1],['enabled',1,1],['antiShake',2,1],['smoothing',3,1],['horizontal',4,-2],['vertical',6,-2],['tickMs',8,2],['rpm',10,2],['reloadMs',12,2],['startDelayMs',14,2],['rampMs',16,2],['syncRpm',18,1],['adsOverride',19,1],['adsHorizontal',20,-2],['adsVertical',22,-2],['firstHorizontal',24,-2],['firstVertical',26,-2],['firstShot',28,1],['curve',29,1],['firingMode',30,1],['burstCount',31,1],['triggerThreshold',32,2],['pressMs',34,2],['releaseMs',36,2],['stageMs0',38,2],['stageMs1',40,2],['stageMs2',42,2],['stageX0',44,-2],['stageX1',46,-2],['stageX2',48,-2],['stageY0',50,-2],['stageY1',52,-2],['stageY2',54,-2]];
OAG.weaponWire=w=>{const d=new DataView(new ArrayBuffer(56));OAG.weaponFields.forEach(([k,o,t])=>t===1?d.setUint8(o,w[k]):t===2?d.setUint16(o,w[k],true):d.setInt16(o,w[k],true));return OAG.hex(d.buffer)};
OAG.weaponRead=s=>{const d=OAG.view(s),w={};OAG.weaponFields.forEach(([k,o,t])=>w[k]=t===1?d.getUint8(o):t===2?d.getUint16(o,true):d.getInt16(o,true));return w};
OAG.defaultWeapon=()=>Object.fromEntries(OAG.weaponFields.map(([k])=>[k,({tickMs:40,rpm:600,reloadMs:2500,burstCount:3,triggerThreshold:50,stageMs0:500,stageMs1:1000,stageMs2:1500})[k]||0]));
