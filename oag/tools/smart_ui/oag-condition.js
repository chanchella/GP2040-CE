'use strict';
OAG.num=(label,attr,v,min=0,max=60000,step=1)=>'<label>'+label+'<input type="number" '+attr+' value="'+OAG.esc(v)+'" min="'+min+'" max="'+max+'" step="'+step+'"></label>';
OAG.refEditor=(list,attr)=>'<div class="oag-chips">'+list.map((r,i)=>'<button type="button" data-oag-remove-ref="'+attr+':'+i+'">'+OAG.esc(OAG.controlName(r))+' ×</button>').join('')+'</div><div class="oag-line"><select data-oag-ref-select="'+attr+'">'+OAG.controlOptions(list[0]||'g6',false)+'</select><button type="button" data-oag-add-ref="'+attr+'">+ ضيف زر للمجموعة</button></div>';
OAG.renderCondition=(c,b,i)=>{
const at='data-oag-condition="'+b+':'+i+'"',f=k=>at+' data-field="'+k+'"';
let html='<div class="oag-condition"><div class="oag-line"><strong>'+(i?'الشرط '+(i+1):'لما')+'</strong>';
if(i)html+='<select '+f('join')+'>'+OAG.options(['وكمان — AND','أو — OR'],c.join)+'</select><label class="oag-check"><input type="checkbox" '+f('negate')+(c.negate?' checked':'')+'>اعكس الشرط — NOT</label>';
html+='<button class="oag-quiet" data-oag-remove-condition="'+b+':'+i+'"'+(!i?' disabled':'')+'>امسح الشرط</button></div><div class="oag-row"><label>الزر أو الاتجاه<select '+f('control')+'>'+OAG.controlOptions(c.control)+'</select></label><label>طريقة الضغط<select '+f('kind')+'>'+OAG.options(OAG.triggerNames,c.kind)+'</select></label>';
if(c.kind<=3||c.kind===8||c.kind===9)html+=OAG.num(c.kind===9?'مهلة التسلسل (ms)':c.kind===8?'مهلة الضغط مع بعض (ms)':'مهلة الدوسات (ms)',f('window'),c.window,1);
if(c.kind===3)html+=OAG.num('عدد الدوسات',f('taps'),c.taps,1,20);
if(c.kind===4||c.kind===7)html+=OAG.num('مدة المسكة (ms)',f('hold'),c.hold,1);
if(c.kind===10||c.kind===11)html+=OAG.num('حد التفعيل (من ١٠٠٠)',f('threshold'),c.threshold,-1000,1000);
html+='</div>';
if(c.kind===8||c.kind===9)html+='<p class="oag-small">'+(c.kind===9?'رتّب الأزرار زي ما هتدوسها. ينفع تكرر نفس الزر.':'اختار الأزرار اللي لازم تتداس مع بعض.')+'</p>'+OAG.refEditor(c.refs,'c:'+b+':'+i);
return html+'</div>'};
OAG.conditionChanged=el=>{
const [b,i]=el.dataset.oagCondition.split(':').map(Number),c=OAG.combo.branches[b].conditions[i],f=el.dataset.field;
c[f]=el.type==='checkbox'?+el.checked:el.tagName==='SELECT'&&f==='control'?el.value:Number(el.value);
if(f==='control'&&c.refs.length)c.refs[0]=c.control;
if(f==='kind'){if(c.kind===8)c.refs=[c.control,c.control==='g6'?'g5':'g6'];if(c.kind===9)c.refs=[c.control,c.control,'g4'];if(c.kind===10)c.control='s12';if(c.kind===11)c.control='a5'}
OAG.changed(el.tagName==='SELECT'||el.type==='checkbox')};
OAG.refChanged=(key,remove)=>{
const [type,b,part,at]=key.split(':'),branch=OAG.combo.branches[+b],item=type==='c'?branch.conditions[+part]:branch[part][+at];
if(remove!==undefined){item.refs.splice(remove,1);if(type==='c'&&item.refs.length)item.control=item.refs[0]}else{const select=document.querySelector('[data-oag-ref-select="'+key+'"]');if(item.refs.length>=8)throw Error('المجموعة وصلت لحد ٨ أزرار');item.refs.push(select.value)}
OAG.changed(true)};
