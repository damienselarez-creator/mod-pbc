# Sources, statut des textes et intégration de Winifred

Winifred (GUID 3), Réprouvée démoniste, est destinée à accompagner Scrootch (GUID 2), guerrier tauren. Identités lues dans la base locale le 29 septembre 2026. Le niveau relevé était respectivement 4 et 6 ; le niveau et les sorts doivent toujours être pris dans le contexte actuel, pas figés dans le lore.

## Les textes avant les fragments

Quatre textes sources ont été rédigés avant leur extraction finale. Les premiers petits JSON de travail ont été remplacés. Les sources Markdown, les ODT éditables et les PDF se trouvent dans ce dossier. Les JSON PBC sont dans `/opt/azerothcore/modules/mod-pbc/knowledge/`. Les extraits conservent intégralement tous les paragraphes des textes longs, sans résumé substitué au texte. La notice éditoriale hors récit n’est pas comptée dans les pages du corps.

| Socle | Pages A4 réelles | Mots du corps | Fragments |
|---|---:|---:|---:|
| Société réprouvée | 21 | 7 443 | 35 |
| Démonistes Affliction réprouvés | 20 | 7 208 | 28 |
| Biographie de Winifred | 22 | 7 597 | 30 |
| Personnalité et psychologie | 21 | 7 468 | 25 |

Total : 84 pages, 29 716 mots de corps, 118 fragments originaux. Le corpus assemblé ajoute 22 fragments historiques préexistants, soit 140 fragments. Présentation : A4, Liberation Serif 12 pt, interligne 1,5, marges 2,5 cm. Aucun saut de page ajouté par chapitre. Les titres et pieds de page utilisent les styles ordinaires du document.

Chaque fragment contient un identifiant, le texte intégral extrait, une section et une plage de paragraphes source, ainsi que le SHA-256 du fichier source. Les fichiers Markdown sont la source éditoriale ; modifier un JSON seul créerait une divergence. Régénérer avec `python3 chunk_sources.py --guid 3`, puis `python3 render.py` et exporter les ODT en PDF avec LibreOffice si le texte change.

## Repères du lore

La borne est celle du corpus déjà fourni : début de WotLK, avant le Portail du Courroux. Les récits ne donnent aucune connaissance anticipée du coup d’État de Fossoyeuse, des Val’kyr au service de Sylvanas, du Cataclysme, de la direction ultérieure des Réprouvés ou de l’Ombreterre. Ils ne reprennent pas les ajouts de Season of Discovery ni les ressources modernes du démoniste.

- Histoire collective : [histoire-reprouves.json](/opt/azerothcore/modules/mod-pbc/knowledge/histoire-reprouves.json), 27 fragments, et le manuscrit [histoire-reprouves-reference.docx](/opt/azerothcore/modules/mod-pbc/knowledge/histoire-reprouves-reference.docx). Ses références sont celles du manuscrit fourni ; il ne s’agit pas d’un nouvel audit exhaustif de chaque source originale. Les 22 fragments de la politique historique initiale sont repris sans réécriture des faits. Les cinq exclusions historiques héritées restent des exclusions d’identifiants, pas une garantie que leurs thèmes seraient absents de tout autre texte.
- Société et institutions : [socioculture-pretres-reprouves.json](/opt/azerothcore/modules/mod-pbc/knowledge/socioculture-pretres-reprouves.json), pour les exemples de soins, d’attachements, de fonctions, de violence et de coopération. Le nouveau socle général ne convertit pas les pratiques sacerdotales en coutumes universelles. Les développements sur la Horde, Tranquillien, Hautebrande, Putress et Acherus s’appuient sur les fragments historiques correspondants.
- Principes du démoniste : [manuel original de World of Warcraft, pages imprimées 86–87](https://assets.blz-contentstack.com/v3/assets/blt3452e3b114fab0cd/blt2e9295db02a222fc/6025bcbb6968b53d529edb2a/media_manual_classic_enUS.pdf). Invocations, affaiblissements, effets persistants, fragments d’âme et outils de soutien. Les chiffres, coûts et formulations de mécaniques anciennes ne sont pas transposés comme spécifications WotLK.
- Transmission à Fossoyeuse : [Halgar’s Summons, quête 1478](https://classicdb.ch/?quest=1478), reproduction du texte de quête consultée. Elle établit l’orientation vers Carendin Halgar, pas l’existence d’une confrérie officielle de l’Affliction.
- Vérification de période : [Carendin Halgar](https://warcraft.wiki.gg/wiki/Carendin_Halgar), en distinguant les éléments antérieurs à Cataclysm des ajouts ultérieurs ; [The Binding, quête 1471, version WotLK](https://www.wowhead.com/wotlk/quest%3D1471/the-binding). Ces références secondaires aident à situer les quêtes ; les scènes inventées ne leur sont pas attribuées.
- Capacités effectivement implémentées : [AfflictionWarlockStrategy.cpp](/opt/azerothcore/modules/mod-playerbots/src/Ai/Class/Warlock/Strategy/AfflictionWarlockStrategy.cpp) et [GenericWarlockStrategy.cpp](/opt/azerothcore/modules/mod-playerbots/src/Ai/Class/Warlock/Strategy/GenericWarlockStrategy.cpp). Ce code confirme la stratégie de combat `affli` (le libellé de spécialisation est `afflic`) et les moyens reconnus par cette implémentation ; il ne constitue pas une preuve de coutume sociale canonique et ne donne pas automatiquement ces sorts à Winifred.

## Création originale, clairement distinguée

Les scènes anonymes des deux socles collectifs sont des illustrations littéraires possibles. Elles ne sont ni des événements canoniques supplémentaires, ni des souvenirs attribués à Winifred. Le récit explicite la diversité des attitudes plutôt que de créer une doctrine raciale obligatoire.

La famille Darkmoor, Edmund, Maera Vossel, Harlan et les épisodes privés sont des créations originales pour ce compagnon. Darkmoor ne revendique ni domaine ni titre. Winifred n’assiste pas fictivement à toutes les scènes historiques importantes ; le sort inconnu de sa famille et les lacunes de sa mémoire restent inconnus. Son passé écrit ne lui donne ni rang prestigieux ni sorts non appris.

La psychologie suit les demandes de l’auteur : loyauté, intelligence, sensibilité sous le cynisme ; humour noir pince-sans-rire pour supporter le traumatisme réprouvé ; rancœur et rage dirigées contre les responsables ; cruauté punitive possible envers les ennemis, refus de faire payer les innocents. Elle n’est ni sereinement guérie ni secrètement inoffensive. La confiance envers Scrootch évoluera avec les événements réellement joués.

## Évolution du lecteur PBC

Le lecteur accepte les formats historiques existants et ajoute `pbc.corpus.personnage` avec `pilier: personnage`. Le format mixte `pbc.corpus.documentaire` peut réunir les quatre piliers. Les champs existants de configuration `PBC.History*` sont conservés.

Les fragments `biographie` emploient `biographie_originale_non_quete_jouee`. Les fragments `psychologie` emploient `portrait_psychologique_scenes_illustratives_non_vecues`. Ils portent `personnage`, `texte_diegetique` et exactement un GUID autorisé dans `character_guids`. Les fragments historiques et socioculturels gardent leur ancien contrat. Un fragment personnel sans propriétaire unique est rejeté, même s’il n’est pas dans la liste d’accès sélectionnée.

Les quatre types partagent la sélection lexicale bornée, la publication atomique et les budgets existants. L’en-tête du contexte distingue le passé personnel écrit, les dispositions psychologiques, les scènes illustratives et le savoir collectif. Aucun fragment n’est directement inséré dans les tables de mémoire, de relations ou de quêtes. Les paroles réellement prononcées pourront ensuite appartenir à l’historique normal de conversation, comme auparavant.

Le corpus prêt est `winifred-documentaire.json`. Tous ses fragments sont réservés au GUID 3. Cette restriction ne doit pas être remplacée par le GUID du joueur Scrootch. Le manifeste `piliers-winifred.json` décrit l’association au compagnon principal ; il ne modifie pas les groupes ou talents de playerbots.

## État pendant la session de jeu

La fiche `characters/Winifred.card.txt` est préparée dans le répertoire local chargé par PBC. Les cartes sont ignorées par Git selon la convention existante du module ; une copie se trouve aussi dans ce dossier. Aucun rechargement à chaud n’a été déclenché et aucune mémoire existante n’a été modifiée.

Le serveur en cours utilise encore son binaire antérieur. Aucun redémarrage, remplacement du binaire, changement de talents ou de configuration active n’a été effectué. La configuration prête est conservée dans `deploiement-pbc.conf.pending` ; elle ne doit être appliquée qu’avec le nouveau lecteur intégré au serveur. Une compilation du serveur puis une fenêtre de redémarrage seront nécessaires pour rendre cette évolution active.

La compilation autorisée et exécutée porte uniquement sur les tests autonomes du lecteur. Les suites `historical_retrieval` et `winifred_retrieval` passent : compatibilité, nature des fragments, isolation, accès, budgets, rechargements rejetés atomiquement, concurrence et récupération de chacun des 140 fragments. Les tests n’établissent pas encore le comportement des réponses du modèle dans une session réelle.
