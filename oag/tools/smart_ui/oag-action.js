'use strict';
OAG.renderAction=(a,b,part,i)=>{
const key=b+':'+part+':'+i,at='data-oag-action="'+key+'"',f=k=>at+' data-field="'+k+'"',start=part==='else'?OAG.combo.branches[b].then.length:0;
let h=(i?'<label class="oag-link">قبل الخطوة دي<select '+f('together')+'>'+OAG.options(['ثم — بعد ما المجموعة اللي قبلها تخلص','+ مع بعض — في نفس مجموعة الضغط'],a.together||0).replace('value="1"','value="1"'+([7,8,12].includes(a.kind)||[7,8,12].includes(OAG.combo.branches[b][part][i-1]?.kind)?' disabled':''))+'</select></label>':'')+'<div class="oag-step"><div class="oag-step-head"><span class="oag-number">'+(start+i+1)+'</span><select '+f('kind')+'>'+OAG.options(OAG.actionNames,a.kind)+'</select><button data-oag-move="'+key+':-1" aria-label="طلّع الخطوة">↑</button><button data-oag-move="'+key+':1" aria-label="نزّل الخطوة">↓</button><button class="oag-danger" data-oag-remove-action="'+key+'" aria-label="امسح الخطوة">×</button></div><div class="oag-row">';
if(a.kind<=4||a.kind===9||a.kind===10||a.kind===11){let opts=OAG.controlOptions(a.control,false);if(a.kind===9)opts=OAG.controls.filter(c=>c.id[0]==='s').map(c=>'<option value="'+c.id+'"'+(a.control===c.id?' selected':'')+'>'+OAG.esc(c.name)+'</option>').join('');if(a.kind===10)opts=['الأنالوج الشمال','الأنالوج اليمين'].map((n,j)=>'<option value="a'+j+'"'+(a.control==='a'+j?' selected':'')+'>'+n+'</option>').join('');if(a.kind===11)opts='<option value="g7"'+(a.control==='g7'?' selected':'')+'>L2 / LT</option><option value="g8"'+(a.control==='g8'?' selected':'')+'>R2 / RT</option>';h+='<label>الحركة<select '+f('control')+'>'+opts+'</select></label>'}
if([0,2,3,4,5,6,9,10,11].includes(a.kind))h+=OAG.num(a.kind===5?'مدة الانتظار (ms)':'مدة الضغط أو الحركة (ms)',f('duration'),a.duration);
h+=OAG.num('استنى قبلها (ms)',f('before'),a.before)+OAG.num('استنى بعدها (ms)',f('after'),a.after);
if([4,7,8].includes(a.kind))h+=OAG.num('وقت الإفلات / الفاصل (ms)',f('interval'),a.interval);
if(a.kind===4||a.kind===7)h+=OAG.num('عدد التكرارات'+(a.kind===4?' — صفر يعني مستمر':''),f('count'),a.count,a.kind===7?1:0,100);
if(a.kind===7||a.kind===8)h+=OAG.num('ارجع للخطوة رقم',f('first'),a.first,start+1,start+i);
if(a.kind===10)h+=OAG.num('يمين / شمال (من ١٠٠٠)',f('x'),a.x,-1000,1000)+OAG.num('فوق / تحت (من ١٠٠٠)',f('y'),a.y,-1000,1000);
if(a.kind===11)h+=OAG.num('قوّة الضغط (من ١٠٠٠)',f('x'),a.x,0,1000);
if(a.kind===9)h+=OAG.num('قوّة الاتجاه (من ١٠٠٠)',f('x'),a.x,0,1000);
h+='</div>';if(a.kind===6)h+=OAG.refEditor(a.refs,'a:'+b+':'+part+':'+i);
if(a.kind===0&&!a.timed)h+='<p class="oag-small">مسكة محفوظة من V1. تعديل المدة يحوّلها لضغطة تنتهي تلقائيًا.</p>';
if(a.kind===2&&a.duration===0)h+='<p class="oag-small">صفر = امسكه لحد نهاية الكومبو أو إلغائه.</p>';
if(a.kind===13)h+='<p class="oag-small">بياخد مدة إعادة التلقيم من إعدادات السلاح الحالي.</p>';
return h+'</div>'};
OAG.actionChanged=el=>{const [b,part,i]=el.dataset.oagAction.split(':'),a=OAG.combo.branches[+b][part][+i],f=el.dataset.field;a[f]=f==='control'?el.value:Number(el.value);if(f==='duration'||f==='kind')a.timed=1;if(f==='kind'){if([7,8,12].includes(a.kind))a.together=0;if(a.kind===7||a.kind===8)a.first=part==='else'?OAG.combo.branches[+b].then.length+1:1;if(a.kind===9)a.control='s12';if(a.kind===10)a.control='a1';if(a.kind===11)a.control='g8';if(a.kind===6)a.refs=['g5','g4']}OAG.changed(el.tagName==='SELECT')};
