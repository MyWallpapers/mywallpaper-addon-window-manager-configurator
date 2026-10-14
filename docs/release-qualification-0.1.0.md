# A08 — qualification et publication 0.1.0

Le 15 octobre 2026, **Placement des fenêtres 0.1.0 est publié et disponible dans l’environnement de développement MyWallpaper**. Le binaire central publié a passé les tests Win32 et une exécution dans l’application actuelle, via un calque DEV temporaire. Les trois calques préexistants sont conservés ; le calque de test et son serveur ont été retirés. MyWallpaper reste connecté et ouvert. Le core, le moteur partagé et le ledger n’ont pas été modifiés.

Ce document consigne des preuves. Les décisions applicables restent DEC-0012/0013/0099/0140/0152/0157/0160 du ledger MyWallpaper, SHA-256 `5c198b56bd404e3e035f31687e2ed23b7c0d87655e998bd0940fa2649ddae993`. Il ne crée pas une nouvelle décision de lancement.

## Identité exacte

| Élément | Valeur |
| --- | --- |
| Sources et tag `v0.1.0` | `40069485aa0d356d721cad4cd286941e4e9fd392` |
| Add-on | `01a13ca4-67bc-7be1-9ccb-8a84601a865e` |
| Release disponible | `01a13caf-0ea1-7a82-893c-6a9ed77d0403` |
| Publication | `01a13cab-3e50-7123-b6b4-88fb672ac5ee` |
| Distribution | `sha256:0f52f92f11af7bdb8cb6c124b9b94c6e264dac8edb6506888c03dcc6704dba2f` |
| EXE central testé | `7d18ea7ba110a4a8ddf71f0dca01df4e6e78ad18efb5c12f955be5a692521d67` |

Les cinq jobs du [workflow central 38652074104](https://github.com/MyWallpapers/native-addon-toolchain/actions/runs/38652074104) ont réussi. Deux reconstructions centrales indépendantes produisent les mêmes quatre fichiers, octet pour octet. Les empreintes et tailles annoncées par l’index publié correspondent à ces fichiers. Deux constructions locales propres sont également identiques entre elles ; leur EXE diffère du central, issu d’une autre chaîne de compilation. Le test central utilise bien l’EXE central, pas le résultat local.

La release est active et préférée par son auteur. Elle apparaît dans le catalogue étendu (`includeUnreviewed`), avec attestation de reconstruction native. Elle n’apparaît pas encore dans le catalogue réservé aux releases examinées : `staffEvidenceReviewed=false`, signal communautaire insuffisant. Aucune publication en production ni validation par l’équipe n’est revendiquée.

Preuves : [builds](../evidence/release-0.1.0-builds.json), [publication et fichiers](../evidence/release-0.1.0-publication.json).

## Corrections réalisées

- **Construction autonome** : en-tête JSON et notice MIT vendorisés avec empreintes verrouillées, sélection explicite de Visual Studio 2022/2026 et SHA-256 par .NET dans le worker. Aucun accès aux sources du moteur n’est nécessaire.
- **Placement asynchrone** : même en mode immédiat, la mémorisation attend que Windows réalise le déplacement. L’unique état de placement existant suffit ; aucune file supplémentaire n’est introduite.
- **Changement de réglages pendant un geste** : le geste est annulé au lieu de terminer avec l’ancienne configuration.
- **Restauration** : une API qui refuse une restauration ne fait plus perdre prématurément la preuve de l’état possédé.
- **Palette cliquable** : création au premier plan sans activation, conservation des styles et réouvertures testées. L’ordre d’affichage seul ne corrigeait pas le problème ; le style initial topmost était nécessaire.

La [revue d’impact](impact-review.md) trace les appelants et registrations concernés, dont les callbacks Windows et les contrats de construction. Aucun code du moteur ou autre add-on n’a été déplacé dans A08.

## Essais observés

Host : Windows `10.0.26200.9550`, un écran 2560 × 1440 à 96 DPI. Les cibles sont des fenêtres Win32 contrôlées, distinctes des applications personnelles.

| Vérification | Résultat |
| --- | --- |
| CTest Windows/MSVC et GNU/WSL ; manifeste public | Passés ; 77 définitions, cinq profils |
| Suite Win32 sur l’EXE central | 23 groupes passés ; un probe COM limité séparément |
| Sidebar normale MyWallpaper | Slider 50 → 61 puis restauré ; cinq dispositions dans le menu ; application sans bouton Appliquer |
| Raccourcis et palette dans MyWallpaper | Placement, clavier, Échap et sélection à la souris passés |
| Gestes externes bas niveau | Alt-déplacement +70/+45 px, Alt-redimensionnement +80/+60 px passés |
| Répétition | 40 cycles de palette/placement ; 12 désactivations/réactivations |
| Arrêt par retrait du calque | États temporaires restaurés ; companion et gardien terminés |
| Stabilité du host | Mêmes instances de MyWallpaper et Explorer, toujours réactives ; aucune relance pendant l’essai intégré |

Les entrées clavier/souris sont envoyées par `SendInput`, avec contrôle de la fenêtre sous le pointeur, du premier plan et de la touche Alt. Deux premières tentatives intégrées ont échoué sur Alt-déplacement. La première montre un pointeur hors de la cible ; la seconde ne mesurait pas encore les préconditions du glissement. Après instrumentation et temporisation des entrées, les essais autonome et intégré passent **avec le même EXE**. La cause de la seconde tentative n’est pas établie ; ces échecs sont conservés dans la preuve. Aucune modification spéculative du moteur n’en a résulté.

L’exécution MyWallpaper emploie le contrôleur `process-v2` et le Job Object réels, avec une validation native humaine. Les fichiers sont ceux de la publication, mais chargés par un **calque DEV**. L’installation permanente d’un calque publié, sa synchronisation de réglages de compte et une matrice d’applications tierces ne sont pas démontrées par cet essai.

Preuves : [23 groupes Win32 centraux](../evidence/release-0.1.0-windows-validation.json), [intégration, contrôles et mesures](../evidence/release-0.1.0-integration.json).

## Mémoire et arrêt

Mesures cumulées du companion et du gardien, 19 échantillons sur **63,844 secondes**, dont dix au repos final :

| Ressource | Départ | Repos final | Pic |
| --- | ---: | ---: | ---: |
| Mémoire privée | 4,727 Mio | 4,852 Mio | 5,066 Mio |
| Mémoire résidente, working set | 44,820 Mio | 45,039 Mio | 45,133 Mio |
| Handles | 331 | 330 | — |
| Objets GDI / USER | 3 / 18 | 3 / 18 | — |
| Threads | 13 | 6 | — |

Rétention privée finale : **+0,125 Mio**. Le résultat est compatible avec un état borné sur ces cycles ; il ne démontre pas l’absence de fuite sur plusieurs heures. Le working set inclut des pages partageables et ne doit pas être confondu avec la mémoire privée. Après retrait du calque, les deux processus A08 ont quitté.

Les placements ordinaires restent volontairement en place. Premier plan forcé, cadre de plein écran et rangement sont des états temporaires restaurés à l’arrêt normal. EOF, mort du parent, arrêt du Job et récupération suivante ont aussi des scénarios distincts dans la suite Win32. Dans le Job actuel, le gardien ne survit pas à la mort de tout le groupe : le rangement garde donc une fenêtre minimisée accessible, et la prochaine activation reprend le journal. Aucune garantie absolue n’est ajoutée pour une fenêtre qui refuse les API.

## Limites et suites possibles

Restent hors qualification : applications tierces et élevées, entrées physiques utilisateur et pavé tactile, écrans à DPI différents ou débranchés, vrais changements de bureaux virtuels, coopération FancyZones et restrictions de focus. L’interface COM des bureaux virtuels existe, mais l’identifiant de la fenêtre non activante de la fixture a été refusé ; ce probe n’est pas compté parmi les 23 succès. Les hooks privés du flyout Snap d’Explorer et la connexion à un fournisseur A01 de zone de travail restent absents.

Ces limites ne requièrent pas de complexifier le moteur pour publier cette première version en développement. Toute extension devra être qualifiée sur sa cible réelle selon le ledger, sans transformer les preuves de fixture en promesse de compatibilité générale.
