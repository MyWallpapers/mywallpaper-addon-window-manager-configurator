import assert from 'node:assert/strict'
import {spawn,execFileSync} from 'node:child_process'
import {mkdir,readFile,writeFile} from 'node:fs/promises'
import {resolve} from 'node:path'
import {createInterface} from 'node:readline'
import {once} from 'node:events'
import {createHash} from 'node:crypto'
const root=resolve('.'),binary=resolve('native/out/windows-x86_64/companion.exe')
const fixture=spawn(process.env.MW_FIXTURE_BINARY??resolve('.mywallpaper/build/windows-x86_64/Release/window_fixture.exe'),[],{stdio:['pipe','pipe','pipe'],windowsHide:true})
const wait=ms=>new Promise(r=>setTimeout(r,ms))
function mailbox() {
 const values=[],waiters=[]
 return {push(value){const w=waiters.shift();if(w){clearTimeout(w.timer);w.resolve(value)}else values.push(value)},next(ms=7000){if(values.length)return Promise.resolve(values.shift());return new Promise((resolve,reject)=>{const w={resolve,reject};w.timer=setTimeout(()=>{const i=waiters.indexOf(w);if(i>=0)waiters.splice(i,1);reject(new Error('Timed out waiting for native evidence'))},ms);waiters.push(w)})}}
}
const fixtureMailbox=mailbox()
createInterface({input:fixture.stdout}).on('line',line=>fixtureMailbox.push(JSON.parse(line)))
let fixtureStderr='';fixture.stderr.on('data',d=>fixtureStderr+=d)
async function query(action,index=0,extra={}){fixture.stdin.write(JSON.stringify({action,index,...extra})+'\n');const reply=await fixtureMailbox.next();assert.equal(reply.error,undefined,JSON.stringify(reply));return reply}
class Companion {
 constructor(){this.child=spawn(binary,['--fixture-target',String(fixture.pid)],{stdio:['pipe','pipe','pipe'],windowsHide:true});this.mailbox=mailbox();this.buffer=Buffer.alloc(0);this.parts=[];this.stderr='';this.child.stderr.on('data',d=>this.stderr+=d);this.child.stdout.on('data',d=>{this.buffer=Buffer.concat([this.buffer,d]);while(this.buffer.length>=4){const header=this.buffer.readUInt32LE(0),length=header&0x3fffffff,kind=header>>>30;if(this.buffer.length<length+4)return;this.parts.push(this.buffer.subarray(4,length+4));this.buffer=this.buffer.subarray(4+length);if(kind===0||kind===3){this.mailbox.push(JSON.parse(Buffer.concat(this.parts).toString('utf8')));this.parts=[]}}})}
 send(frame){const bytes=Buffer.from(JSON.stringify(frame));for(let offset=0;offset<bytes.length;offset+=1048576){const size=Math.min(1048576,bytes.length-offset),kind=bytes.length<=1048576?0:offset===0?1:offset+size===bytes.length?3:2;const h=Buffer.alloc(4);h.writeUInt32LE(((kind<<30)|size)>>>0);this.child.stdin.write(Buffer.concat([h,bytes.subarray(offset,offset+size)]))}}
 async start(settings={},job=false,scratchName='session'){this.scratch=resolve('.mywallpaper/tests',scratchName);await mkdir(this.scratch,{recursive:true});if(job)await query('assign-job',0,{pid:this.child.pid});this.send({type:'init',v:5,addonId:'dev:window-manager-fixture',artifactDigest:'sha256:'+'a'.repeat(64),version:'0.1.0',targetArchitecture:'windows-x86_64',scratchDirectory:this.scratch,layerSettings:{},deviceSettings:settings});const frame=await this.mailbox.next();assert.equal(frame.type,'ready',JSON.stringify(frame)+' '+this.stderr);return this}
 async settings(values){this.send({type:'settings',v:5,layerSettings:{},deviceSettings:values});return (await this.mailbox.next()).payload}
 async action(action,extra={}){this.send({type:'message',v:5,surface:'interface',instanceId:'fixture:interface:0',displayIndex:0,payload:{action,...extra}});const frame=await this.mailbox.next();assert.equal(frame.type,'message',JSON.stringify(frame)+' '+this.stderr);return frame.payload}
 async stop(){this.send({type:'shutdown',v:5});if(this.child.exitCode===null)await Promise.race([once(this.child,'exit'),wait(5000).then(()=>{throw new Error('Native shutdown timed out')})]);assert.equal(this.child.exitCode,0,this.stderr)}
 async eof(){this.child.stdin.end();if(this.child.exitCode===null)await Promise.race([once(this.child,'exit'),wait(5000).then(()=>{throw new Error('EOF cleanup timed out')})])}
}
const results=[],companions=[]
const record=(name,evidence={},status='passed')=>{results.push({name,status,...evidence});console.log(`${status==='passed'?'PASS':'OBSERVED'} ${name}`)}
async function settled(index,condition){for(let i=0;i<35;i++){const state=await query('state',index);if(condition(state))return state;await wait(80)}throw new Error('Native state did not settle: '+JSON.stringify(await query('state',index)))}
const close=(a,b,tolerance=2)=>Math.abs(a-b)<=tolerance
try {
 const initial=await fixtureMailbox.next();assert.equal(initial.pid,fixture.pid)
 const c=new Companion();companions.push(c);await c.start({motion:'instant',reducedMotion:true})
 const status=await c.action('status');assert.equal(status.guardianRunning,true);assert.ok(status.monitors.length>0);record('process-v2 initialization and guardian',status)
 const original=await query('state')
 await c.settings({motion:'instant',layout:'duo',splitShare:65});
 for(let i=0;i<3;i++){
  await c.action('palette');await wait(100);
  await query('overlay-click',0,{x:original.work[0]+original.work[2]-100,y:original.work[1]+100});
  await settled(0,s=>s.visibleRect[0]>s.work[0]+s.work[2]*.64);
 }
 await c.action('restore');await settled(0,s=>s.rect.every((n,i)=>close(n,original.rect[i])));
 await c.settings({motion:'instant',reducedMotion:true});
 record('three palette reopenings keep topmost hit-testing and mouse selection');
 const gap=Math.round(12*original.dpi/96),half=Math.round(original.work[2]/2)
 assert.equal((await c.action('left')).ok,true)
 const left=await settled(0,s=>close(s.visibleRect[0],s.work[0]+gap)&&close(s.visibleRect[2],half-gap-Math.floor(gap/2)))
 record('visible DWM bounds, reserved work area and DPI gaps',{before:original,after:left})
 assert.equal((await c.action('bottom-right')).ok,true)
 const quarter=await settled(0,s=>s.visibleRect[0]>s.work[0]+s.work[2]/2&&s.visibleRect[1]>s.work[1]+s.work[3]/2)
 record('quarters',{state:quarter})
 await c.settings({motion:'smooth',duration:180,centerSize:'comfortable'})
 assert.equal((await c.action('center')).ok,true)
 const center=await settled(0,s=>close(s.visibleRect[2],Math.trunc(s.work[2]*.7)))
 record('centering and native animation',{state:center})
 await c.settings({motion:'instant',reducedMotion:true})
 assert.equal((await c.action('restore')).ok,true)
 await settled(0,s=>s.rect.every((n,i)=>close(n,original.rect[i])))
 record('restore first placement')
 await c.settings({motion:'instant',rememberOnMove:true});
 const holding=query('hold',0,{milliseconds:450});await wait(30);
 await c.action('right');await holding;
 const moved=await settled(0,s=>s.visibleRect[0]>s.work[0]+s.work[2]/2);
 await wait(150);const memory=JSON.parse(await readFile(resolve(c.scratch,'window-memory.json'),'utf8'));
 const saved=Object.values(memory.placements).at(-1);
 assert.ok(close(saved.x*moved.work[2]+moved.work[0],moved.visibleRect[0]));
 assert.ok(close(saved.width*moved.work[2],moved.visibleRect[2]));
 record('instant placement memory waits for the asynchronous window request',{heldMs:450,state:moved,saved});
 await c.settings({motion:'instant'});await c.action('restore');
 await settled(0,s=>s.rect.every((n,i)=>close(n,original.rect[i])));
 await c.action('topmost');await settled(0,s=>s.topmost);await c.action('topmost');await settled(0,s=>!s.topmost);record('topmost toggle')
 const beforeFullscreen=await query('state');await c.action('fullscreen');await settled(0,s=>(s.style&0x00c00000)===0);await c.action('fullscreen');await settled(0,s=>s.style===beforeFullscreen.style&&s.rect.every((n,i)=>close(n,beforeFullscreen.rect[i])));record('reversible fullscreen')
 const rejected=await c.settings({innerGap:999});assert.equal(rejected.event,'rejected');assert.equal((await c.action('status')).enabled,true);assert.equal((await c.settings({motion:'instant'})).event,'settings-applied');record('invalid settings keep previous configuration active')
 const big=await c.settings({motion:'instant',fixturePadding:'x'.repeat(2*1048576)});assert.equal(big.event,'settings-applied');record('ordered process-v2 records larger than one MiB')
 await c.action('right');const remembered=await settled(0,s=>s.visibleRect[0]>s.work[0]+s.work[2]/2);await c.action('remember');await query('new',2);const reopened=await settled(2,s=>s.visibleRect.every((n,i)=>close(n,remembered.visibleRect[i])));record('explicit memory and WinEvent opening restore',{state:reopened})
 const targetImage=(await c.action('status')).targetImage;assert.ok(targetImage);console.log(`Fixture image: ${targetImage}`)
 await c.settings({motion:'instant',ruleApp1:targetImage,ruleAction1:'bottom-right'});await query('new',3);await settled(3,s=>s.visibleRect[0]>s.work[0]+s.work[2]/2&&s.visibleRect[1]>s.work[1]+s.work[3]/2);record('application opening rule')
 const filters=await c.settings({motion:'instant',excludedApps:targetImage});assert.equal(filters.event,'settings-applied');assert.equal((await c.action('left')).ok,false);await c.settings({motion:'instant'});record('application exclusion')
 const m=status.monitors[0],area={x:m.work[0]+20,y:m.work[1]+20,width:m.work[2]-40,height:m.work[3]-40};assert.equal((await c.action('work-area',{version:1,monitorDevice:m.device,rect:area})).event,'work-area-applied');assert.equal((await c.action('left')).ok,true);await settled(0,s=>close(s.visibleRect[0],area.x+gap));record('add-on-owned work-area input contract',{area})
 await c.action('topmost');await c.action('fullscreen');await c.action('tray');await settled(0,s=>status.contained?s.minimized:!s.visible);await c.settings({enabled:false});await settled(0,s=>s.visible&&!s.minimized&&!s.topmost&&(s.style&0x00c00000)!==0);record('disable restores owned fullscreen, tray and topmost states')
 await c.settings({});assert.equal((await c.action('status')).enabled,true);record('reset to defaults and re-enable')
 const manifest=JSON.parse(await readFile('manifest.json','utf8'));await mkdir('evidence/profiles',{recursive:true});for(const preset of manifest.presets){await c.settings(preset.settings);const rendered=await c.action('render-profile',{file:resolve(`evidence/profiles/${preset.id}.png`)});assert.equal(rendered.event,'rendered')};record('five profiles rendered by native GDI+ overlay')
 const desktop=await c.action('save-desktop-layout');record('public virtual desktop API capability probe',{result:desktop},desktop.event==='desktop-layout-saved'?'passed':'unavailable-for-fixture')
 await c.stop();record('graceful shutdown')
 const eof=new Companion();companions.push(eof);await eof.start({motion:'instant'},false,'eof');await eof.action('topmost');await eof.action('tray');await settled(0,s=>s.topmost);await eof.eof();await settled(0,s=>s.visible&&!s.minimized&&!s.topmost);record('stdin EOF restoration')
 const crash=new Companion();companions.push(crash);await crash.start({motion:'instant'},false,'crash');const crashStatus=await crash.action('status');await crash.action('fullscreen');await crash.action('topmost');await crash.action('tray');await settled(0,s=>s.topmost);crash.child.kill();await once(crash.child,'exit');await settled(0,s=>s.visible&&!s.minimized&&!s.topmost&&(s.style&0x00c00000)!==0);record('forced companion termination with surviving guardian',{contained:crashStatus.contained})
 const job=new Companion();companions.push(job);await job.start({motion:'instant'},true,'job');const jobStatus=await job.action('status');assert.equal(jobStatus.contained,true);assert.equal(jobStatus.trayMode,'minimize-accessible');await job.action('tray');await settled(0,s=>s.minimized&&s.visible);await query('kill-job');if(job.child.exitCode===null)await once(job.child,'exit');const accessible=await query('state');assert.equal(accessible.visible,true);record('whole Job Object termination keeps tray window accessible',{state:accessible})
 const resume=new Companion();companions.push(resume);await resume.start({motion:'instant'},false,'job');await settled(0,s=>s.visible&&!s.minimized);await resume.stop();record('next activation recovers stale journal after entire job termination')
 const ledger=await readFile(process.env.MW_LEDGER_PATH??'../MyWallpaper/docs/decisions/decision-ledger.json'),bytes=await readFile(binary)
 const summary={passed:results.filter(r=>r.status==='passed').length,capabilityProbes:results.filter(r=>r.status!=='passed').length}
 await writeFile('evidence/windows-validation.json',JSON.stringify({date:new Date().toISOString(),qualification:'private external Win32 fixture; no Explorer or user application modified',os:execFileSync('cmd.exe',['/c','ver'],{encoding:'utf8'}).trim(),binarySha256:createHash('sha256').update(bytes).digest('hex'),ledgerSha256:createHash('sha256').update(ledger).digest('hex'),summary,results,remaining:['real application matrix','mixed DPI monitors and display unplug','real mouse/keyboard/touchpad gestures','Explorer Snap Layout private hooks','FancyZones coexistence in live drag','focus restoration heuristic under real activation restrictions','desktop switches across multiple actual virtual desktops','tray hiding outside a Job Object; current fixture uses accessible minimization']},null,2)+'\n')
 console.log(`${summary.passed} Windows integration checks passed; ${summary.capabilityProbes} capability probe recorded`)
} catch(error) {
 console.error(companions.map(c=>c.stderr).join('\n'))
 throw error
} finally {
 for(const c of companions)if(c.child.exitCode===null){try{await c.eof()}catch{c.child.kill()}}
 if(fixture.exitCode===null){try{await query('exit');await once(fixture,'exit')}catch{fixture.kill()}}
 if(fixtureStderr)console.error(fixtureStderr)
}
