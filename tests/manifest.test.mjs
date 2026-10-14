import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { parseAddonManifest, validateSettingValues, getSchemaDefaultsForScope } from '../../MyWallpaper/packages/sdk-public/dist/addon-schema/index.js'
const manifest=JSON.parse(await readFile(new URL('../manifest.json',import.meta.url),'utf8'))
const parsed=parseAddonManifest(manifest)
assert.equal(parsed.runtime,'native-v1')
assert.ok(manifest.settings.length<=100)
assert.equal(new Set(manifest.settings.map(s=>s.id)).size,manifest.settings.length)
const defaults=getSchemaDefaultsForScope(manifest,'device')
assert.equal(validateSettingValues(manifest,defaults,'device').length,0)
for(const preset of manifest.presets) {
 assert.equal(validateSettingValues(manifest,preset.settings,'device').length,0,`Preset ${preset.id}`)
 assert.deepEqual(JSON.parse(await readFile(new URL(`../profiles/${preset.id}.json`,import.meta.url),'utf8')),preset)
}
for(const s of manifest.settings) {
 if(s.type!=='section')assert.equal(s.scope,'device')
 if(s.showIf)assert.ok(manifest.settings.some(p=>p.id===s.showIf.setting))
 if(s.parent)assert.ok(manifest.settings.some(p=>p.id===s.parent&&p.type==='section'))
}
assert.equal(manifest.settings.find(s=>s.id==='advancedEnabled').default,false)
assert.equal(manifest.settings.find(s=>s.id==='focusGuard').default,false)
assert.equal(manifest.settings.find(s=>s.id==='layout').default,'duo')
console.log(`Public MyWallpaper manifest contract: ${manifest.settings.length} definitions, ${manifest.presets.length} profiles passed`)
