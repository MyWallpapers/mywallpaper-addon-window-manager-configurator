import {readFile,writeFile,mkdir} from 'node:fs/promises'
import {createHash} from 'node:crypto'
const hash = value => createHash('sha256').update(value).digest('hex')
const catalogue=JSON.parse(await readFile('../MyWallpaper/docs/audits/2026-10-13-windhawk-catalogue/catalogue.json','utf8'))
const ids=['always-on-top','minimize-to-tray','window-center-resize','center-new-windows','snap-commander','prevent-focus-stealing','snap-flyout-instant','three-finger-drag','windows-11-native-four-column-snap-layout']
const references=[]
for(const id of ids) {
 const mod=catalogue.mods.find(m=>m.id===id)
 const bytes=await readFile(`.mywallpaper/references/${id}.wh.cpp`)
 const source=bytes.toString('utf8')
 const variants={raw:hash(bytes),lf:hash(source.replace(/\r\n/g,'\n')),crlf:hash(source.replace(/\r?\n/g,'\r\n'))}
 references.push({id,sourceUrl:mod.source_url,catalogueSha256:mod.source_sha256,downloadSha256:hash(bytes),catalogueMatches:Object.entries(variants).filter(([,h])=>h===mod.source_sha256).map(([name])=>name),licenceEvidence:mod.license_evidence,lineCount:source.split('\n').length})
}
const ledger=await readFile('../MyWallpaper/docs/decisions/decision-ledger.json')
const json=await readFile('../mywallpaper-native-engine/third_party/nlohmann/json.hpp')
const notice=await readFile('../mywallpaper-native-engine/third_party/nlohmann/LICENSE.MIT')
await mkdir('evidence',{recursive:true})
await writeFile('evidence/source-provenance.json',JSON.stringify({examinedOn:'2026-10-13',coreCommit:'299548ebcc20770fd73b10bdffd5f05586fbe126',ledgerSha256:hash(ledger),catalogueCommit:catalogue.upstream_commit,engineReferenceCommit:'ff32c520dd4ccd9073c9dc001a180e886d50ab63',references},null,2)+'\n')
await writeFile('native/dependencies.lock.json',JSON.stringify({schemaVersion:1,json:{engineReferenceCommit:'ff32c520dd4ccd9073c9dc001a180e886d50ab63',path:'third_party/nlohmann/json.hpp',sha256:hash(json),noticePath:'third_party/nlohmann/LICENSE.MIT',noticeSha256:hash(notice)}},null,2)+'\n')
console.log(references.map(r=>`${r.id}: catalogue ${r.catalogueMatches.join('/')||'differs; raw hash retained'}`).join('\n'))
