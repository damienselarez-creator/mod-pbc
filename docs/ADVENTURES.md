# Tester la mémoire des aventures partagées

Version : première intégration, 20 septembre 2026.
L'état de déploiement est consigné dans `memoire/PROJET.md` ; ce guide seul ne
certifie pas que le binaire a été chargé.

## Une partie de test

1. Connecte ton personnage et place Antanagor dans ton groupe.
2. Sélectionne le compagnon et tape `.adventure begin`.
   Tu peux aussi taper `.adventure begin NomExactDuCompagnon`.
   La sélection évite l'ambiguïté entre Antanagor et Anthanagor.
3. Accepte et rends quelques quêtes. Échange avec lui pendant le parcours.
   Reste à proximité pour que les événements soient identifiés comme partagés.
4. Tape `.adventure status` : `active: true` et `events` doivent refléter la collecte.
5. À la fin, tape `.adventure end`. La session est fermée immédiatement ;
   la synthèse se fait en arrière-plan. Tu peux continuer à jouer.
6. Consulte `.adventure status` jusqu'à `pending: 0` et « Synthèse terminée ».
7. Tape `.adventure recall` ou `.adventure recall un_nom_ou_un_sujet` pour examiner
   le contexte des souvenirs. Demande ensuite au compagnon ce qu'il retient de
   l'aventure, par message de groupe ou en chuchotement.

## Session suivante et problèmes

- `.adventure begin` ouvre une nouvelle session avec le même compagnon.
- Une déconnexion ne clôture pas la session : la collecte reprend à la reconnexion.
- Un redémarrage préserve les sessions et souvenirs déjà journalisés.
- `.adventure retry` reprend une synthèse échouée sans recréer les lots déjà validés.
- `.adventure export` produit le fichier JSON courant, même hors synthèse.
- Ne pas utiliser `.chars condense` pour fabriquer les souvenirs du quatrième pilier.
  La condensation concurrente est désactivée pour les personnages appariés.
- Les anciennes mémoires SQL sont conservées, mais ne servent plus de bloc de
  souvenirs dans les réponses des personnages passés au nouveau système.

## Ce qu'il faut observer

- Les tâches banales ne produisent pas toutes une fiche individuelle.
- Une intrigue poursuivie sur deux sessions peut enrichir un épisode existant.
- Le compagnon distingue tes actions de sa participation et garde sa personnalité.
- Une remarque réellement échangée peut compter davantage qu'une livraison.
- Il n'invente ni sauvetage, ni promesse, ni révélation future.
- Le champ `regard` est une interprétation générée après la partie, pas une
  phrase qu'il aurait nécessairement prononcée pendant le jeu.

## Conservation et limites de cette première version

Dossier par défaut : `data/pbc-adventures` relativement au répertoire de travail du serveur. Sous Linux en production, utiliser de préférence un chemin absolu persistant, par exemple `/var/lib/azerothcore/pbc-adventures`, via `PBC.AdventurePath`. `player-<GUID>.json` contient les chunks.
Le sous-dossier `journal` conserve les événements et toutes les versions validées,
avec contrôle d'intégrité. Ne pas effacer ses fichiers `.pending` : dans cette
fonctionnalité, ils constituent les archives permanentes, et pas une file à purger.

La synthèse utilise la connexion IA `condensation` déjà configurée dans `mod-pbc`.
Une session longue est traitée en plusieurs lots ; un échec laisse les sources disponibles.
Les événements classés de routine restent dans le journal, sans obligation de chunk.

Première version : clôture manuelle ; un couple joueur/compagnon stable ; quêtes
acceptées/rendues/abandonnées, changements de zone partagés, mort du joueur,
boss crédité au joueur et dialogues enregistrés par `mod-pbc`. Les coups et soins
individuels, les dialogues de gossip complets et l'instant exact de validation
des objectifs ne sont pas instrumentés. Aucune action précise n'est déduite de
la seule présence du compagnon. Le PNJ réel n'est pas identifié par tous les hooks.

La pertinence des souvenirs est choisie par mots/thèmes et récence, avec un contexte
borné. La qualité narrative de la fusion entre épisodes doit être appréciée en jeu.
La validation automatique contrôle les références et le format ; elle ne prouve
pas à elle seule l'exactitude sémantique de chaque résumé produit par l'IA.

Cette intégration concerne le quatrième pilier. Elle ne charge pas automatiquement
la biographie complète du troisième pilier, dont le lecteur reste à adapter.
