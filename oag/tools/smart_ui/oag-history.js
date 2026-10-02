'use strict';
OAG.comboFrom=j=>({name:j.name,enabled:j.enabled,mode:j.mode,cancelable:j.cancelable,branches:j.branches.map(OAG.decodeBranch)});
OAG.comboState=()=>{OAG.state.dirty=JSON.stringify(OAG.combo)!==JSON.stringify(OAG.savedCombo);OAG.$('combo-state').textContent=OAG.state.dirty?'تعديلات مش محفوظة':'محفوظ';OAG.$('combo-state').classList.toggle('oag-dirty',OAG.state.dirty)};
OAG.weaponState=()=>{OAG.state.weaponDirty=JSON.stringify(OAG.weapon)!==JSON.stringify(OAG.savedWeapon);OAG.$('weapon-state').textContent=OAG.state.weaponDirty?'تعديلات مش محفوظة':'محفوظ';OAG.$('weapon-state').classList.toggle('oag-dirty',OAG.state.weaponDirty)};
OAG.readWeapon=async(g,s,saved=0)=>{const j=await OAG.api('/api/oag/weapon?game='+g+'&slot='+s+'&saved='+saved),w=OAG.weaponRead(j.wire);if(!w.configured){const old=await OAG.api('/api/recoil?game='+g+'&weapon='+s);w.horizontal=old.horizontalRaw;w.vertical=old.verticalRaw;w.tickMs=old.tickMs;w.enabled=+(old.enabled||0)}return w};
