'use strict';
OAG.paintCombo=()=>{
OAG.$('name').value=OAG.combo.name;OAG.$('enabled').checked=!!OAG.combo.enabled;OAG.$('cancelable').checked=!!OAG.combo.cancelable;OAG.$('mode').innerHTML=OAG.options(OAG.modeNames,OAG.combo.mode);
OAG.$('branches').innerHTML=OAG.combo.branches.map((b,j)=>'<article class="oag-branch"><div class="oag-branch-head"><h3>'+(j?'وإلا جرّب الفرع '+(j+1):'الفرع الأول')+'</h3><label class="oag-check"><input data-oag-enable-branch="'+j+'" type="checkbox"'+(b.enabled?' checked':'')+'>شغّال</label><button class="oag-danger" data-oag-remove-branch="'+j+'"'+(!j?' disabled':'')+'>امسح الفرع</button></div><div class="oag-block"><span class="oag-label">لما — WHEN</span>'+b.conditions.map((c,i)=>OAG.renderCondition(c,j,i)).join('')+'<button data-oag-add-condition="'+j+'" class="oag-soft"'+(b.conditions.length>=4?' disabled':'')+'>+ ضيف شرط</button></div><div class="oag-block"><span class="oag-label">نفّذ — THEN</span>'+b.then.map((a,i)=>OAG.renderAction(a,j,'then',i)).join('')+'<button data-oag-add-action="'+j+':then" class="oag-soft">+ ضيف خطوة</button></div><details class="oag-else"'+(b.else.length?' open':'')+'><summary>وإلا — ELSE / STOP</summary><p class="oag-small">لو شرط التشغيل حصل وباقي الشروط ما اتحققتش، نفّذ الخطوات دي. لو فاضية مش هيشغّل حاجة.</p>'+b.else.map((a,i)=>OAG.renderAction(a,j,'else',i)).join('')+'<button data-oag-add-action="'+j+':else" class="oag-soft">+ ضيف خطوة بديلة</button></details></article>').join('');
OAG.$('add-branch').disabled=OAG.combo.branches.length>=4;OAG.paintDetection()};
OAG.paintDetection=()=>{let max=0;OAG.combo.branches.forEach(b=>b.conditions.forEach(c=>{if(c.kind<=3||c.kind===8||c.kind===9)max=Math.max(max,c.window)}));OAG.$('detection').textContent='مهلة الكشف في الكومبو ده لحد '+max+' ms. السنجل والدبل بيستنّوا لو فيه فرع أدق على نفس الزر. المسكة الطويلة ما بتتحسبش سنجل.'};
OAG.changed=render=>{OAG.state.dirty=true;OAG.$('combo-state').textContent='تعديلات مش محفوظة';OAG.$('combo-state').classList.add('oag-dirty');if(render)OAG.paintCombo();else OAG.paintDetection();clearTimeout(OAG.comboTimer);OAG.comboTimer=setTimeout(()=>OAG.previewCombo().catch(e=>OAG.message(e.message)),160)};
OAG.builderClick=OAG.safe(async()=>{
const el=OAG.clicked,ds=el.dataset;
if(ds.oagAddCondition!==undefined){const b=OAG.combo.branches[+ds.oagAddCondition];if(b.conditions.length>=4)return;const c=OAG.condition();c.control='g5';c.kind=5;b.conditions.push(c)}
else if(ds.oagRemoveCondition){const [b,i]=ds.oagRemoveCondition.split(':').map(Number);if(i)OAG.combo.branches[b].conditions.splice(i,1)}
else if(ds.oagAddAction){const [b,part]=ds.oagAddAction.split(':'),r=OAG.combo.branches[+b];if(r.then.length+r.else.length>=16)throw Error('الفرع وصل لحد ١٦ خطوة');r[part].push(OAG.action())}
else if(ds.oagRemoveAction){const [b,part,i]=ds.oagRemoveAction.split(':');OAG.combo.branches[+b][part].splice(+i,1)}
else if(ds.oagMove){const [b,part,i,by]=ds.oagMove.split(':'),a=OAG.combo.branches[+b][part],to=+i+(+by);if(to<0||to>=a.length)return;[a[+i],a[to]]=[a[to],a[+i]]}
else if(ds.oagRemoveBranch!==undefined){if(+ds.oagRemoveBranch)OAG.combo.branches.splice(+ds.oagRemoveBranch,1)}
else if(ds.oagAddRef){OAG.refChanged(ds.oagAddRef);return}
else if(ds.oagRemoveRef){const a=ds.oagRemoveRef.split(':'),n=+a.pop();OAG.refChanged(a.join(':'),n);return}
else return;
OAG.changed(true)});
