# Placement des fenêtres

Un add-on MyWallpaper pour organiser les fenêtres depuis les réglages habituels : **70 valeurs dans sept sections, 77 définitions au total**, et cinq profils complets. Le défaut choisi est deux zones souples, avec placement à la demande. Les déplacements manuels restent libres.

Le companion Windows x64 agit depuis l’extérieur des applications. Il ne charge aucune DLL dans Explorer, ne redémarre pas le shell et ne remplace pas la géométrie de la barre des tâches. Une seule couche de cet add-on peut être active par session Windows.

## Utilisation

Dans MyWallpaper, choisissez une disposition et un profil. Les profils de l’interface appliquent des ensembles complets de valeurs de présentation et de disposition ; vos applications exclues, règles d’ouverture, raccourcis et préférence de réduction des animations sont préservés. « Ambiance » change seulement la palette. Choisissez « Mes couleurs » pour éditer directement les couleurs et le fond.

- **Alt + glisser avec le bouton gauche** : déplacer depuis l’intérieur de la fenêtre.
- **Alt + glisser avec le bouton droit** : redimensionner depuis l’intérieur.
- **Maj pendant un déplacement Alt** : choisir une zone, puis relâcher le bouton pour placer la fenêtre.
- **Ctrl + Alt + Espace** : ouvrir toutes les zones ; choisir avec un chiffre de 1 à 9, les flèches et Entrée, ou la souris. Échap ferme l’aperçu. Les grilles de plus de neuf zones restent parcourables avec les flèches.
- **Ctrl + Alt + flèches** : moitiés ; pavé numérique 1, 3, 7, 9 : quarts ; pavé 5 : centrer.
- **Ctrl + Alt + C / R / N / T / M / P / F** : centrer, revenir au placement précédent, écran suivant, premier plan, rangement, retenir la position, plein écran réversible.

Les touches et leurs préfixes sont réglables. Un raccourci occupé est laissé à son propriétaire et signalé. Le menu de l’icône de notification propose aussi les placements et la liste des fenêtres rangées. Un geste à trois ou quatre doigts peut ouvrir les zones lorsque vous lui affectez **Ctrl + Maj + F12** dans les réglages du pavé tactile de Windows et activez le réglage correspondant ici.

Les règles d’ouverture acceptent directement les noms des applications, un placement et un écran. Les positions retenues sont stockées par application, classe de fenêtre et bureau virtuel. La mémoire vise une position par groupe ; elle ne différencie pas plusieurs documents de la même application. L’action « Retenir cette disposition pour ce bureau » conserve le layout pour les placements suivants sur ce bureau ; elle ne réorganise pas automatiquement toutes les fenêtres à chaque changement de bureau.

## Profils

| Profil | Disposition | Présentation |
| --- | --- | --- |
| Bureau calme | Deux zones | Aurore turquoise et bleue, angles doux |
| Lecture sur papier | Une grande zone et deux outils | Fond clair, contours chaleureux |
| Studio nocturne | Trois colonnes | Violet profond, ressort discret |
| Mosaïque rosée | Quatre zones | Dégradé corail et prune, coins généreux |
| Repères à fort contraste | Deux zones | Fond sombre presque opaque, contour jaune, mouvement immédiat |

Les images dans [evidence/profiles](evidence/profiles) sont produites par le **vrai renderer GDI+ de l’overlay**, sur un canevas transparent de 960 × 600. Elles montrent le rendu natif, sans constituer des captures d’un placement dans Explorer ou dans une application utilisateur. Le voile transparent est une composition alpha ; aucun flou de verre live n’est annoncé.

## Fonctions réalisées

Zones, moitiés, quarts, colonnes, layout principal/outils et grille jusqu’à 6 × 4 ; proportions, marges et espacements adaptés au DPI ; mesure des bords visibles DWM ; centrage avec dimensions prédéfinies ; déplacement relatif vers l’écran précédent/suivant ; restauration du premier placement ; premier plan ; plein écran avec restauration du cadre ; règles d’ouverture ; mémoire de positions et de dispositions ; aperçu cliquable et au clavier ; animations de placement et respect des préférences Windows.

L’observation repose sur `SetWinEventHook`, les raccourcis sur `RegisterHotKey`, et les gestes de souris sur les hooks externes `WH_MOUSE_LL` / `WH_KEYBOARD_LL`. Les mouvements asynchrones n’empilent pas une nouvelle requête tant que la précédente n’a pas été appliquée. Les grilles usuelles, règles et profils ne nécessitent aucun JSON. L’extension avancée est facultative, limitée à douze zones normalisées.

## Arrêt et récupération

Le placement ordinaire est une action voulue : il reste en place à l’arrêt. Les états temporaires possédés par l’add-on — premier plan, cadre de plein écran et rangement — sont restaurés à la désactivation, à l’arrêt et après EOF. Les journaux sont écrits avant la mutation et identifient le processus par PID **et date de création**, ainsi que la fenêtre par une propriété de session et un cookie ; un HWND recyclé n’est pas une preuve de propriété.

Un gardien attend la mort du companion et tente une récupération bornée. Dans le host Desktop courant, le companion et ses enfants sont dans un Job Object à arrêt groupé : ce gardien ne survit pas à la mort de tout le groupe. **Dans ce contexte, le rangement réduit la fenêtre en la laissant visible dans la barre des tâches.** Hors Job Object, le rangement peut cacher la fenêtre et le gardien la réaffiche après la mort du parent. Les journaux encore applicables sont repris à la prochaine activation. Les fenêtres qui refusent les API ou changent leurs styles indépendamment ne reçoivent pas une garantie de restauration absolue.

Le besoin commun est décrit dans [contracts/recovery-lease.proposal.v1.json](contracts/recovery-lease.proposal.v1.json). Il reste une proposition de qualification ; aucun droit de sortir du Job Object ni changement du core n’est ajouté ici.

## Frontières de qualification

La protection de focus est une restitution externe après une ouverture inattendue, désactivée par défaut. Elle peut être refusée par Windows ; elle n’intercepte pas tous les appels d’activation des applications. La coopération FancyZones laisse les déplacements Windows avec Maj à PowerToys ; aucun fichier de zones PowerToys n’est importé. Les contacts bruts d’un pavé tactile ne sont pas décodés. L’accélération du flyout Snap d’Explorer et l’ajout de layouts privés dans son UI ne sont pas implémentés ; le choix de zones de cet add-on ouvre directement.

La zone de travail provient de `MONITORINFO.rcWork`. L’entrée [work-area v1](contracts/work-area.v1.schema.json) accepte une zone attendue fournie par le propriétaire de la barre, et son application est testée. Aucun fournisseur A01 n’est encore connecté à ce prototype.

Voir [docs/qualification.md](docs/qualification.md) pour les preuves réellement observées et les essais restants.

## Construction et validation locales

Windows x64, Visual Studio 2022 ou 2026 C++ Build Tools et Node.js. Le dépôt est autonome : son en-tête JSON MIT et sa notice sont présents dans `third_party/nlohmann`, vérifiés par SHA-256 avant compilation. Aucun checkout du moteur n’est requis.

```powershell
./native/build.ps1
node tests/manifest.test.mjs
node tests/windows.test.mjs
```

Le bundle utilisable est `native/out/windows-x86_64/companion.exe`, avec licence et notices. `mywallpaper.config.json` décrit la construction pour l’outillage créateur. Les tests Windows créent leurs propres fenêtres Win32 non activantes et n’appliquent aucun effet à une application utilisateur. Les tests portables de géométrie, configuration et framing sont dans CTest.

Les sources de réglages sont dans `scripts/generate-manifest.mjs` ; manifeste, profils et en-tête embarqué sont régénérés ensemble. Cette réalisation locale ne ratifie aucune nouvelle portée de lancement (DEC-0157). Le ledger MyWallpaper demeure l’unique autorité produit et architecture.
