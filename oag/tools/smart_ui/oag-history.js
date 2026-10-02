'use strict';
OAG.comboFrom=j=>({name:j.name,enabled:j.enabled,mode:j.mode,cancelable:j.cancelable,cancel:OAG.cancelRead(j.cancelWire),branches:j.branches.map(OAG.decodeBranch)});
OAG.comboState=()=>{OAG.state.dirty=JSON.stringify(OAG.combo)!==JSON.stringify(OAG.savedCombo);OAG.$('combo-state').textContent=OAG.state.dirty?'تعديلات مش محفوظة':'محفوظ';OAG.$('combo-state').classList.toggle('oag-dirty',OAG.state.dirty)};
OAG.weaponState=()=>{OAG.state.weaponDirty=JSON.stringify(OAG.weapon)!==JSON.stringify(OAG.savedWeapon);OAG.$('weapon-state').textContent=OAG.state.weaponDirty?'تعديلات مش محفوظة':'محفوظ';OAG.$('weapon-state').classList.toggle('oag-dirty',OAG.state.weaponDirty)};
OAG.readWeapon=async(g,s,saved=0)=>OAG.weaponRead((await OAG.api('/api/oag/weapon?game='+g+'&slot='+s+'&saved='+saved)).wire);

OAG.withLoad=async fn=>{OAG.contextLoading=(OAG.contextLoading||0)+1;OAG.$('main').inert=true;try{return await fn()}finally{OAG.contextLoading--;OAG.$('main').inert=!!OAG.contextLoading}};
OAG.readCombo=async(g,s,saved=0)=>OAG.comboFrom(await OAG.api('/api/oag/combo?game='+g+'&slot='+s+'&saved='+saved));
OAG.loadGame=g=>OAG.withLoad(async()=>{const s=OAG.state,wn=(await OAG.api('/api/weapons?game='+g)).names,c=await OAG.readCombo(g,s.slot),sc=await OAG.readCombo(g,s.slot,1),w=await OAG.readWeapon(g,s.weaponSlot),sw=await OAG.readWeapon(g,s.weaponSlot,1);s.game=g;OAG.weaponNames=wn;OAG.combo=c;OAG.savedCombo=sc;OAG.weapon=w;OAG.savedWeapon=sw;OAG.weaponHistory=[];OAG.comboState();OAG.weaponState();OAG.paintCombo();OAG.paintWeapon()});
