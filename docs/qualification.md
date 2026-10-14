# Qualification de l’implémentation locale

La [qualification de publication 0.1.0](release-qualification-0.1.0.md) complète ce socle : binaire central exact, exécution dans MyWallpaper via un calque DEV, arrêt, mesures mémoire et disponibilité dans le catalogue de développement. Elle distingue les gestes simulés sur fixture des usages physiques et applications tierces encore non qualifiés.

Les résultats observés sont dans [evidence/windows-validation.json](../evidence/windows-validation.json), liés au SHA-256 du binaire et du ledger courant. Ce rapport décrit une fixture Win32 externe privée. Il ne certifie pas Explorer, Chromium, une application élevée ou toutes les versions de Windows.

Le build MSVC x64 et CTest couvrent profils, alpha-last, plages, settings invalides, zones, marges impaires, origines négatives, limites de grille, transfert géométrique, centrage et framing mono/multi-chunk. Le validateur public MyWallpaper accepte le manifeste et les valeurs des cinq presets. Aucune action ou section n’est soustraite au compteur de 100.

Résultat local du 15 octobre 2026 : **23 groupes d’assertions Win32 passés**, un probe de capacité enregistré séparément ; Windows `10.0.26200.9550`, un écran observé de 2560 × 1440 à 96 DPI. Les tests portables passent aussi sous GNU 15.2.0 dans WSL. Les scénarios Windows ne démontrent pas une compatibilité multi-build ou multi-écran. Les nouvelles assertions vérifient trois réouvertures avec de vrais clics sur la palette et la mémorisation après application d’un déplacement asynchrone sur une fenêtre lente.

Le ledger a changé pendant le travail concurrent. L’empreinte des essais est conservée dans leur preuve ; les DEC-0012/0013/0099/0157/0160 ont été relues ensuite et leurs contraintes applicables restent respectées. [evidence/ledger-recheck.json](../evidence/ledger-recheck.json) lie cette relecture au nouveau snapshot, sans remplacer le ledger ni falsifier l’empreinte du test.

Les tests Windows vérifient les coordonnées visibles DWM, la zone de travail, l’animation et les états des vraies fenêtres de la fixture via les API du host de test. Les données précisent le DPI et les écrans réellement observés. Les rendus PNG emploient le vrai renderer natif sans afficher un overlay plein écran dans la session utilisateur.

Le probe de bureaux virtuels constate la disponibilité de l’interface COM, mais Windows refuse l’identifiant de la fenêtre non activante de cette fixture. Ce résultat est enregistré comme indisponible pour la fixture, et non comme un succès de mémorisation par bureau. Le masquage hors Job Object n’a pas été observé : le contexte de test utilise le fallback minimisé.

Le build est désormais autonome : en-tête JSON MIT et notice vendorisés avec vérification de leurs empreintes, sélection explicite des générateurs Visual Studio 2022/2026, et calcul SHA-256 par .NET disponible dans le worker central. Aucun fichier du moteur privé n’est consommé. La provenance Git et les artefacts centraux sont vérifiés lors de la qualification de publication, distinctement des essais locaux.

L’arrêt normal, EOF, la désactivation et la mort forcée du parent sont distingués de la mort de tout le Job Object. Sous arrêt groupé, le mode tray laisse une fenêtre minimisée visible. Le test ferme ce groupe puis vérifie l’accessibilité et la reprise du journal à l’activation suivante. La proposition de lease de récupération expose le besoin d’un lifecycle détenu par le host, sans attribuer ce comportement au core actuel.

Restent à qualifier sur les hosts concernés :

- entrées physiques Alt/glisser, clic et clavier dans des applications utilisateur, gestes configurés d’un pavé tactile ; les entrées simulées sur fixture et la palette passent désormais dans MyWallpaper ;
- Win32, Chromium, applications UWP, applications élevées, fenêtres à taille minimale particulière et applications occupées ;
- écrans à DPI différents, écrans négatifs physiques, rotation, débranchement et retour d’affichage ;
- passage entre plusieurs bureaux virtuels réels, applications qui replacent leur fenêtre après ouverture et plusieurs documents de même classe ;
- restitution du focus dans les restrictions d’activation Windows et coopération FancyZones avec un vrai déplacement ;
- fournisseur de zone de travail A01 réellement connecté ;
- ciblage de symboles privés d’Explorer si un travail futur demande de modifier son flyout Snap.

Le défaut libre, les exclusions et le respect des plein écrans évitent une réorganisation automatique des applications déjà ouvertes à l’activation. Ce mandat de développement ne modifie aucune décision de lancement.
