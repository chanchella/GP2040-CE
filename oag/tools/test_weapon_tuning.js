const fs = require('node:fs');
const assert = require('node:assert/strict');
const path = require('node:path');
const portal = fs.readFileSync(path.join(__dirname, '../firmware/src/diamond_wifi_portal.cpp'), 'utf8');
const game = fs.readFileSync(path.join(__dirname, '../include/oag/config/diamond_game_library.h'), 'utf8');
const store = fs.readFileSync(path.join(__dirname, '../firmware/src/diamond_game_library_store.cpp'), 'utf8');
const main = fs.readFileSync(path.join(__dirname, '../firmware/src/main.cpp'), 'utf8');

for (const id of ['rh','rv','rt','wrpm','wreload','wshake','wsmooth','wsync','wstart','wramp','wcurve','wads','wah','wav','wfirst','wfh','wfv','wundo','wreset','wcopy','sr']) {
  assert.match(portal, new RegExp('id='+id+'(?:\\s|>)'));
}
for (const delta of ['-0.10','-0.05','-0.01','+0.01','+0.05','+0.10']) assert.ok(portal.includes(delta));
assert.ok(portal.includes('SAVE OAG WEAPON SETTINGS'));
assert.ok(portal.includes("json('/api/weapon-tuning?game='"));
assert.ok(portal.includes("post('/api/weapon-tuning'"));
assert.match(portal, /parseUnsigned\(body,"fireRateRpm",60,2000,rpm\)/);
assert.match(portal, /parseUnsigned\(body,"antiShake",0,100,shake\)/);
assert.match(portal, /parseUnsigned\(body,"smoothing",0,100,smooth\)/);
assert.match(portal, /parseSigned\(body,"adsHorizontal",-200,200,adsH\)/);
assert.match(portal, /parseSigned\(body,"firstShotVertical",-200,200,firstV\)/);

assert.match(game, /struct WeaponTuningProfile/);
assert.match(game, /kRecordVersion = 2/);
assert.match(game, /weaponTuning/);
assert.match(store, /LegacyDiamondGameRecordV1/);
assert.match(store, /selected\.version == 1u/);

assert.match(main, /effectiveRecoilTickMs/);
assert.match(main, /recoilScalePermille/);
assert.match(main, /antiShakeRecoilRaw/);
assert.match(main, /smoothRecoilRaw/);
assert.match(main, /output\.leftTrigger != 0 \|\| mouseAds/);
assert.match(main, /MouseButtonRight/);
assert.match(main, /firstShotPending/);

console.log('OAG_WEAPON_TUNING_V1_STATIC_GUARDS=PASS');
