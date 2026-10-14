import { mkdirSync, writeFileSync, readFileSync, existsSync } from 'node:fs'
const write=(path,text)=>{if(!existsSync(path)||readFileSync(path,'utf8')!==text)writeFileSync(path,text)}
const settings = []
const add = (id, type, label, value, extra = {}) => settings.push({id,type,label,...(type === 'section' ? {} : {default:value,scope:'device'}),...extra})
const section = (id,label,collapsed=false) => add(id,'section',label,undefined,{defaultCollapsed:collapsed})
const bool = (id,label,value,description,parent) => add(id,'boolean',label,value,{description,parent})
const range = (id,label,value,min,max,step,parent,description) => add(id,'range',label,value,{min,max,step,parent,...(description?{description}:{})})
const select = (id,label,value,options,parent,description) => add(id,'select',label,value,{options:options.map(([value,label])=>({value,label})),...(parent?{parent}:{}),...(description?{description}:{})})
const text = (id,label,value,parent,description) => add(id,'string',label,value,{parent,description})
const actions = [['none','Ne rien changer'],['left','Moitié gauche'],['right','Moitié droite'],['top-left','Quart supérieur gauche'],['bottom-right','Quart inférieur droit'],['zone1','Première zone'],['zone2','Deuxième zone'],['zone3','Troisième zone'],['center','Centrer'],['maximize','Agrandir'],['fullscreen','Plein écran réversible']]
bool('enabled','Organiser mes fenêtres',true,'Active les déplacements et les raccourcis de cet add-on.')
section('layoutSection','Ma disposition')
select('layout','Organisation','duo',[['duo','Deux fenêtres côte à côte'],['quad','Quatre fenêtres'],['columns','Colonnes égales'],['focus','Une grande fenêtre et deux outils'],['grid','Grille personnalisée']],'layoutSection')
range('columns','Nombre de colonnes',3,1,6,1,'layoutSection')
settings.at(-1).showIf={setting:'layout',equals:'columns'}
range('gridColumns','Colonnes de la grille',3,1,6,1,'layoutSection')
settings.at(-1).showIf={setting:'layout',equals:'grid'}
range('gridRows','Lignes de la grille',2,1,4,1,'layoutSection')
settings.at(-1).showIf={setting:'layout',equals:'grid'}
range('mainShare','Largeur de la grande fenêtre (%)',64,35,80,1,'layoutSection')
settings.at(-1).showIf={setting:'layout',equals:'focus'}
range('splitShare','Largeur de la fenêtre gauche (%)',50,25,75,1,'layoutSection')
settings.at(-1).showIf={setting:'layout',equals:'duo'}
range('outerGap','Marge au bord de l’écran (px)',12,0,80,1,'layoutSection','Les pixels sont adaptés au zoom de l’écran. La barre des tâches reste réservée.')
range('innerGap','Espace entre les fenêtres (px)',12,0,80,1,'layoutSection')
select('targetMonitor','Écran de placement','window',[['window','Celui de la fenêtre'],['cursor','Celui du pointeur'],['primary','Écran principal']],'layoutSection')
bool('desktopLayouts','Mémoriser une disposition par bureau',false,'Choisissez « Retenir cette disposition pour ce bureau » dans le menu de l’add-on.','layoutSection')
section('appearanceSection','Aperçu des zones')
select('profile','Ambiance','aurora',[['aurora','Aurore — turquoise et bleu'],['paper','Papier — clair et chaleureux'],['midnight','Nuit — violet profond'],['rose','Rosée — corail et prune'],['contrast','Contraste — contours nets'],['custom','Mes couleurs']],'appearanceSection','Ces ambiances changent l’aperçu de placement, pas le contenu des applications.')
select('surfaceMode','Fond des zones','gradient',[['solid','Couleur unie'],['gradient','Dégradé'],['translucent','Voile transparent']],'appearanceSection')
settings.at(-1).showIf={setting:'profile',equals:'custom'}
for (const [id,label,value] of [['startColor','Couleur de départ','#0B655FFF'],['endColor','Couleur d’arrivée','#254AD4FF'],['activeColor','Zone choisie','#64F3CCFF'],['textColor','Texte et numéros','#FFFFFFFF']]) {
  add(id,'color',label,value,{alpha:true,parent:'appearanceSection',showIf:{setting:'profile',equals:'custom'}})
}
select('gradientDirection','Direction du dégradé','diagonal',[['horizontal','De gauche à droite'],['vertical','De haut en bas'],['diagonal','En diagonale']],'appearanceSection')
settings.at(-1).showIf={setting:'profile',equals:'custom'}
range('opacity','Présence de l’aperçu (%)',38,10,100,1,'appearanceSection')
range('activeOpacity','Présence de la zone choisie (%)',78,20,100,1,'appearanceSection')
range('cornerRadius','Coins arrondis (px)',20,0,48,1,'appearanceSection')
range('borderWidth','Épaisseur des contours (px)',2,0,6,1,'appearanceSection')
range('shadowDepth','Ombre douce (px)',14,0,32,1,'appearanceSection')
bool('zoneLabels','Afficher les numéros et les noms',true,'Les numéros permettent de choisir une zone au clavier.','appearanceSection')
bool('showPreview','Voir les zones pendant le placement',true,'Maintenez Maj pendant un déplacement Alt pour choisir une zone.','appearanceSection')
section('movementSection','Déplacements et gestes')
bool('dragAnywhere','Déplacer depuis toute la fenêtre',true,'Maintenez Alt et faites glisser avec le bouton gauche.','movementSection')
bool('resizeAnywhere','Redimensionner depuis toute la fenêtre',true,'Maintenez Alt et faites glisser avec le bouton droit.','movementSection')
select('dragModifier','Touche à maintenir','alt',[['alt','Alt'],['win-alt','Windows + Alt'],['ctrl-alt','Ctrl + Alt']],'movementSection')
select('snapDuringDrag','Placer dans une zone pendant le déplacement','shift',[['shift','Seulement en maintenant Maj'],['always','Toujours'],['never','Déplacement libre']],'movementSection')
bool('edgeSnap','Placer près des bords de l’écran',true,'Fonctionne pendant les déplacements Windows classiques et les déplacements Alt.','movementSection')
range('edgeDistance','Distance d’activation des bords (px)',24,4,80,1,'movementSection')
bool('respectFullscreen','Laisser tranquilles les applications en plein écran',true,'Évite de déplacer les jeux et les présentations déjà en plein écran.','movementSection')
select('fancyzonesMode','Avec PowerToys FancyZones','coexist',[['off','Utiliser seulement cet add-on'],['coexist','Laisser les déplacements Maj à FancyZones']],'movementSection','En mode coopération, les déplacements Windows avec Maj sont laissés à FancyZones. Les raccourcis restent indépendants.')
select('motion','Mouvement des placements','smooth',[['instant','Immédiat'],['smooth','Doux'],['spring','Ressort discret']],'movementSection')
range('duration','Durée du mouvement (ms)',180,0,500,10,'movementSection')
bool('reducedMotion','Réduire les animations',false,'Les préférences d’animation de Windows sont aussi respectées.','movementSection')
section('shortcutSection','Raccourcis',true)
select('hotkeyPrefix','Touches des raccourcis','ctrl-alt',[['ctrl-alt','Ctrl + Alt'],['win-alt','Windows + Alt'],['ctrl-shift','Ctrl + Maj'],['off','Désactiver']],'shortcutSection','Les combinaisons déjà utilisées par une autre application sont signalées et laissées libres.')
text('paletteKey','Afficher toutes les zones','Space','shortcutSection','Avec les touches choisies ci-dessus. Les touches 1 à 9, les flèches et Entrée choisissent une zone ; Échap ferme l’aperçu.')
text('centerKey','Centrer la fenêtre','C','shortcutSection','Une lettre, un chiffre, une flèche, Space ou F1 à F24. Vide pour désactiver.')
text('restoreKey','Revenir au placement précédent','R','shortcutSection','Remet aussi une fenêtre sortie du plein écran à sa taille précédente.')
text('nextMonitorKey','Déplacer vers l’écran suivant','N','shortcutSection','Conserve les proportions et la position relative dans la zone de travail.')
text('topmostKey','Garder au premier plan','T','shortcutSection','Une seconde pression désactive le premier plan.')
text('trayKey','Ranger dans la zone de notification','M','shortcutSection','Pour la retrouver, ouvrez le menu de l’add-on dans la zone de notification.')
text('rememberKey','Retenir cette position','P','shortcutSection','La position sera proposée à la prochaine ouverture de cette application.')
text('fullscreenKey','Plein écran réversible','F','shortcutSection','Retire temporairement le cadre ; une seconde pression le remet.')
bool('arrowShortcuts','Activer les flèches et le pavé numérique',true,'Flèches : moitiés. Pavé 1, 3, 7 et 9 : quarts. Pavé 5 : centrer.','shortcutSection')
bool('touchpadShortcut','Activer un raccourci pour le pavé tactile',false,'Configurez un geste à trois ou quatre doigts dans Windows pour envoyer Ctrl + Maj + F12. Ce geste ouvre les zones ; cet add-on ne lit pas les contacts du pavé.','shortcutSection')
section('rulesSection','À l’ouverture des applications',true)
select('newWindowAction','Pour les autres nouvelles fenêtres','none',actions.filter(([a])=>['none','center','zone1','maximize'].includes(a)),'rulesSection')
bool('rememberOnMove','Retenir les placements effectués ici',false,'Mémorise les placements par application, classe de fenêtre et bureau.','rulesSection')
select('centerSize','Dimensions au centrage','keep',[['keep','Conserver la taille'],['compact','Compact : 50 % de l’écran'],['comfortable','Confortable : 70 %'],['large','Large : 88 %'],['custom','Choisir les proportions']],'rulesSection')
range('centerWidth','Largeur au centrage (%)',70,20,100,1,'rulesSection')
settings.at(-1).showIf={setting:'centerSize',equals:'custom'}
range('centerHeight','Hauteur au centrage (%)',75,20,100,1,'rulesSection')
settings.at(-1).showIf={setting:'centerSize',equals:'custom'}
for (let i=1;i<=3;i++) {
 text(`ruleApp${i}`,`Application ${i}`,'','rulesSection','Nom de l’application, par exemple notepad.exe. Plusieurs noms peuvent être séparés par une virgule ; aucune règle n’est nécessaire pour le placement manuel.')
 select(`ruleAction${i}`,`Placement de l’application ${i}`,'none',actions,'rulesSection')
 select(`ruleMonitor${i}`,`Écran de l’application ${i}`,'window',[['window','Son écran actuel'],['primary','Écran principal'],['cursor','Écran du pointeur']],'rulesSection')
 bool(`ruleTopmost${i}`,`Application ${i} au premier plan`,false,'Appliqué seulement aux nouvelles fenêtres correspondantes.','rulesSection')
}
section('focusSection','Applications à respecter',true)
text('excludedApps','Ne jamais modifier ces applications','','focusSection','Noms séparés par une virgule. Les fenêtres du shell et de MyWallpaper sont toujours exclues.')
text('onlyApps','Agir seulement sur ces applications','','focusSection','Laissez vide pour autoriser les fenêtres ordinaires. Sinon, indiquez leurs noms séparés par une virgule.')
bool('focusGuard','Rendre le focus après une ouverture inattendue',false,'Protection externe limitée : après une ouverture sans saisie récente, tente de rendre le focus à votre fenêtre et fait clignoter l’arrivante. Windows peut refuser cette restitution.','focusSection')
range('focusGrace','Délai après votre dernière action (ms)',1000,300,5000,100,'focusSection')
section('advancedSection','Personnalisation avancée',true)
bool('advancedEnabled','Ajouter des zones sur mesure',false,'Facultatif : la grille et les réglages usuels fonctionnent sans texte technique.','advancedSection')
text('advancedZones','Zones normalisées (JSON facultatif)','','advancedSection','Tableau de 12 zones au maximum : [{"x":0,"y":0,"width":0.5,"height":1,"name":"Lecture"}]. Les nombres sont des proportions de 0 à 1.')
settings.at(-1).showIf={setting:'advancedEnabled',equals:true}
const palettes = {
 aurora:{surfaceMode:'gradient',startColor:'#0B655FFF',endColor:'#254AD4FF',activeColor:'#64F3CCFF',textColor:'#FFFFFFFF',gradientDirection:'diagonal'},
 paper:{surfaceMode:'solid',startColor:'#F6EBD4FF',endColor:'#F6EBD4FF',activeColor:'#965018FF',textColor:'#291D10FF',gradientDirection:'vertical'},
 midnight:{surfaceMode:'gradient',startColor:'#192144FF',endColor:'#632D83FF',activeColor:'#E2B6FFFF',textColor:'#FFFFFFFF',gradientDirection:'vertical'},
 rose:{surfaceMode:'gradient',startColor:'#9D3657FF',endColor:'#3F2558FF',activeColor:'#FFDAA5FF',textColor:'#FFFFFFFF',gradientDirection:'horizontal'},
 contrast:{surfaceMode:'solid',startColor:'#101010FF',endColor:'#101010FF',activeColor:'#FFDF00FF',textColor:'#FFFFFFFF',gradientDirection:'vertical'}
}
const themes = [
 ['calm','Bureau calme','aurora',{layout:'duo',opacity:38,activeOpacity:78,cornerRadius:20,outerGap:12,innerGap:12,motion:'smooth'}],
 ['reading','Lecture sur papier','paper',{layout:'focus',opacity:78,activeOpacity:95,cornerRadius:8,outerGap:20,innerGap:16,motion:'smooth'}],
 ['studio','Studio nocturne','midnight',{layout:'columns',columns:3,opacity:60,activeOpacity:88,cornerRadius:16,outerGap:16,innerGap:12,motion:'spring'}],
 ['mosaic','Mosaïque rosée','rose',{layout:'quad',opacity:52,activeOpacity:86,cornerRadius:24,outerGap:16,innerGap:16,motion:'smooth'}],
 ['clear','Repères à fort contraste','contrast',{layout:'duo',opacity:94,activeOpacity:100,cornerRadius:0,borderWidth:4,shadowDepth:0,motion:'instant'}]
]
const profileKeys=['layout','columns','gridColumns','gridRows','mainShare','splitShare','outerGap','innerGap','opacity','activeOpacity','cornerRadius','borderWidth','shadowDepth','zoneLabels','motion','duration']
const profileDefaults=Object.fromEntries(profileKeys.map(id=>[id,settings.find(s=>s.id===id).default]))
const presets=themes.map(([id,name,profile,values])=>({id,name,settings:{...profileDefaults,profile:'custom',...palettes[profile],...values}}))
if(settings.length>100) throw new Error(`Settings limit exceeded: ${settings.length}`)
const manifest={runtime:'native-v1',name:'Placement des fenêtres',description:'Placez vos fenêtres, créez vos zones et retrouvez vos applications dans une organisation qui vous ressemble.',version:'0.1.0',deviceSettingsVersion:'1',settings,presets,native:{companion:{runtime:'process-v2',entries:{'windows-x86_64':'native/out/windows-x86_64/companion.exe'}}}}
write('manifest.json',JSON.stringify(manifest,null,2)+'\n')
mkdirSync('profiles',{recursive:true})
for(const preset of presets) write(`profiles/${preset.id}.json`,JSON.stringify(preset,null,2)+'\n')
write('profiles/palettes.json',JSON.stringify(palettes,null,2)+'\n')
const embed = (name,value) => `inline constexpr char ${name}[] =\n` + (JSON.stringify(value).match(/[\s\S]{1,6000}/g)||[]).map(part=>`R"MWCFG(${part})MWCFG"`).join('\n') + ';\n'
write('include/manifest_data.hpp','#pragma once\n// Generated by scripts/generate-manifest.mjs; do not edit.\n'+embed('kManifest',manifest)+embed('kPalettes',palettes))
console.log(`${settings.length} definitions, ${presets.length} profiles`)
