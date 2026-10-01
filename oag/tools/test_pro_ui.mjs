// Browser regression: real embedded assets and production C++ Pro API handlers.
// OAG_PRO_API_TEST points to the built host executable. PLAYWRIGHT_MODULE and
// CHROMIUM_PATH are optional overrides for environments without installed browsers.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {spawn} from 'node:child_process';
import readline from 'node:readline';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(path.dirname(new URL(import.meta.url).pathname),'..');
const source=fs.readFileSync(path.join(root,'firmware/src/diamond_wifi_portal.cpp'),'utf8');
const assets=new Map;
for(const [name,v] of Object.entries({'/':'kDashboardHtml','/app.css':'kAppCss','/combo.css':'kComboCss','/app.js':'kAppJs','/names.js':'kNamesJs','/gwc.js':'kGwcJs','/gwc-editor.js':'kGwcEditorJs','/gwc-save.js':'kGwcSaveJs'})){
 const re=new RegExp('constexpr char '+v+'\\[\\] = R"(\\w+)\\(([\\s\\S]*?)\\)\\1";');
 const match=source.match(re);assert(match,v);assets.set(name,match[2]);
}
for(const name of ['pro.html','pro.css','pro-form.js','pro-monitor.js'])assets.set('/'+name,fs.readFileSync(path.join(root,'tools/pro_ui_assets',name),'utf8'));
const api=spawn(process.env.OAG_PRO_API_TEST||path.resolve(root,'../../pro-host/oag_pro_portal_tests'),['serve'],{stdio:['pipe','pipe','inherit']});
const pending=[];readline.createInterface({input:api.stdout}).on('line',line=>{const resolve=pending.shift();assert(resolve,'Unexpected API output');const at=line.indexOf('\t');resolve({status:+line.slice(0,at),body:line.slice(at+1)})});
const call=(method,url,body)=>new Promise(resolve=>{pending.push(resolve);api.stdin.write(method+'\t'+url+'\t'+body+'\n')});
const names=n=>({names:Array.from({length:n},(_,i)=>'OAG '+(i+1))});
const server=http.createServer(async(req,res)=>{try{const url=new URL(req.url,'http://localhost');let body='';for await(const b of req)body+=b;
 if(assets.has(url.pathname)){res.setHeader('Content-Type',url.pathname.endsWith('.js')?'application/javascript; charset=utf-8':url.pathname.endsWith('.css')?'text/css; charset=utf-8':'text/html; charset=utf-8');res.end(assets.get(url.pathname));return}
 if(url.pathname.startsWith('/api/pro-')){const r=await call(req.method,url.pathname+url.search,body);res.writeHead(r.status,{'Content-Type':'application/json; charset=utf-8'});res.end(r.body);return}
 const p=url.pathname;const fixture=p==='/api/config'?{generation:0,activeGame:0,activeWeapon:-1}:p==='/api/games'?names(20):p==='/api/weapons'?names(24):p==='/api/combos'?names(16):p==='/api/recoil'?{horizontalRaw:0,verticalRaw:0,tickMs:40}:p==='/api/combo-program'?{activation:0,repeat:0,pass:true,cancelRelease:true,cancelAgain:false,steps:[]}:null;
 if(fixture){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(fixture))}else{res.writeHead(404);res.end('Not found')}
 }catch(e){res.writeHead(500);res.end(e.message)}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;const errors=[];
try{
 browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{}),args:['--no-sandbox','--no-zygote','--single-process','--disable-gpu','--disable-dev-shm-usage']});
 const page=await browser.newPage({viewport:{width:1280,height:1000}});page.on('pageerror',e=>errors.push(e.message));
 const origin='http://127.0.0.1:'+server.address().port;
 const ready=async()=>{await page.waitForFunction(()=>window.OAGPro&&window.OAGPro.devices.length===2&&document.querySelector('#pi-save'));await page.evaluate(()=>window.OAGPro.ready)};
 await page.goto(origin);await ready();
 assert.equal(await page.inputValue('#pi-target'),'1600');assert.equal(await page.inputValue('#pi-multiplier'),'1');assert.equal(await page.inputValue('#pi-source'),'0');assert(await page.isChecked('#pi-fractional'));
 const mouse='1133:49271:0';await page.selectOption('#pi-scope',mouse);await page.waitForFunction(()=>document.querySelector('#pi-save').disabled===false);
 await page.fill('#pi-source','800');await page.fill('#pi-target','1600');await page.fill('#pi-fullScale','48');assert.match(await page.textContent('#pi-effective'),/2\.0000/);
 await page.click('#pi-save');await page.waitForFunction(()=>document.querySelector('#pi-msg').textContent.includes('تم الحفظ'));
 const saved=await (await fetch(origin+'/api/pro-input?kind=0&scope=1&vid=1133&pid=49271&transport=0')).json();assert.equal(saved.sourceDpi,800);assert.equal(saved.effectiveQ16,131072);assert.equal(saved.fullScale,48);
 await page.reload();await ready();await page.selectOption('#pi-scope',mouse);await page.waitForFunction(()=>document.querySelector('#pi-source').value==='800');
 await page.fill('#pi-source','900');await page.waitForFunction(()=>document.querySelector('#pi-devices').children.length===2);await page.waitForTimeout(1150);assert.equal(await page.inputValue('#pi-source'),'900','Telemetry must not overwrite unsaved edits');
 await page.selectOption('#pi-scope','default');await page.waitForFunction(()=>document.querySelector('#pi-source').value==='0');
 await page.selectOption('#pi-kind','2');await page.waitForFunction(()=>document.querySelector('#pi-note').textContent.includes('الديفولت خطي'));
 await page.selectOption('#pi-scope','9571:1397:0');await page.waitForFunction(()=>document.querySelector('#pi-save').disabled===false);
 await page.fill('#pi-inner','5');await page.fill('#pi-outer','2');await page.fill('#pi-curve','1.5');await page.click('#pi-save');await page.waitForFunction(()=>document.querySelector('#pi-msg').textContent.includes('تم الحفظ'));
 const pad=await (await fetch(origin+'/api/pro-input?kind=2&scope=1&vid=9571&pid=1397&transport=0')).json();assert.equal(pad.inner,50);assert.equal(pad.outer,20);assert.equal(pad.curve,1500);
 await page.check('#pi-desktop');await page.click('#pi-desktop-save');await page.waitForFunction(()=>document.querySelector('#pi-msg').textContent.includes('تم حفظ وضع'));assert.equal((await(await fetch(origin+'/api/pro-profiles')).json()).desktop,1);
 await page.click('#pi-reset');await page.waitForFunction(()=>document.querySelector('#pi-msg').textContent.includes('تمت الاستعادة'));assert.equal(await page.inputValue('#pi-inner'),'0');
 assert.equal(await page.locator('#cg option').count(),20);assert.equal(await page.locator('#ws option').count(),24);assert.equal(await page.locator('#cs option').count(),16);assert.equal(await page.locator('.actionrow').count(),8);
 await page.selectOption('#pi-kind','0');await page.waitForFunction(()=>document.querySelector('#pi-target').offsetParent!==null);await page.selectOption('#pi-scope',mouse);await page.waitForFunction(()=>document.querySelector('#pi-source').value==='800');
 const output=process.env.OAG_UI_SCREENSHOTS;if(output)fs.mkdirSync(output,{recursive:true});
 for(const width of [1280,390,360,320]){
  await page.setViewportSize({width,height:1000});await page.locator('#pro-lab').scrollIntoViewIfNeeded();
  const dimensions=await page.locator('#pro-lab').evaluate(el=>({scroll:el.scrollWidth,client:el.clientWidth,window:innerWidth,right:el.getBoundingClientRect().right}));assert(dimensions.scroll<=dimensions.client+1,JSON.stringify({width,...dimensions}));assert(dimensions.right<=width+1,JSON.stringify({width,...dimensions}));
  if(output)await page.locator('#pro-lab').screenshot({path:path.join(output,'pro-ui-'+width+'.png')});
 }
 assert.deepEqual(errors,[]);console.log('OAG_PRO_UI=PASS (real C++ API saves/reset; mouse/pad profiles; desktop; edit preservation; 1280/390/360/320 layouts; legacy RS8)');
}finally{if(browser)await browser.close();api.stdin.end();server.close()}
