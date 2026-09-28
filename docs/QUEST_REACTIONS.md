# Réactions d'Antanagor aux quêtes

Réglages demandés le 20 septembre 2026.

| Catégorie | Acceptation | Remise avec récompense | Longueur visée |
|---|---:|---:|---|
| Normale | 25 % de base | 40 % de base | 1–2 phrases, 15–35 mots |
| Donjon ou élite | 50 % | 60 % | 2–3 phrases, 35–65 mots |
| Raid ou boss mondial | 75 % | 80 % | 4–5 phrases, 65–100 mots |

Les taux des quêtes spéciales sont des tirages indépendants pour chaque compagnon
éligible. La pénalité après une réplique ne les réduit pas. Les quêtes normales
gardent leur mécanisme antérieur de réduction ; les conversations ordinaires aussi.

La longueur est une consigne de génération, pas un nombre de mots garanti. Elle
s'applique à l'acceptation et à la remise. Le personnage conserve sa voix et sa
personnalité ; les détails doivent venir de la mission ou des souvenirs accessibles.
Les longues répliques sont envoyées en plusieurs messages courts sans couper les
caractères UTF-8. L'historique conserve la réponse entière.

La réaction reste soumise à la présence d'un compagnon éligible dans le groupe,
au suivi actuel du chef de groupe et au succès de la génération. La collecte du
quatrième pilier est indépendante de ces tirages.

## Identification des catégories

Les types WotLK élite et donjon utilisent le deuxième palier. Les types raid,
raid 10 et raid 25 utilisent le troisième. Un objectif de créature de rang
world boss hors marqueur de boss de donjon utilise aussi le troisième palier,
même si la quête est classée élite. Une quête explicitement de donjon reste au
deuxième palier : le rang technique de sa cible ne la transforme pas en raid.
Une quête personnalisée sans ces métadonnées, ou dont le seul objectif est un
objet provenant d'un boss, ne peut pas toujours être reconnue comme world boss ;
son type de quête dans la base doit refléter sa catégorie.

Le boss mondial est assimilé au raid également à l'acceptation (75 %).

## Configuration du serveur

Dans le fichier de configuration chargé par `worldserver`, par exemple `configs/modules/playerbots_characters.conf` :

```ini
PBC.ReplyChanceQuestTakenElite = 50
PBC.ReplyChanceQuestCompletedElite = 60
PBC.ReplyChanceQuestTakenRaid = 75
PBC.ReplyChanceQuestCompletedRaid = 80
```

Les paramètres normaux `PBC.ReplyChanceQuestTaken` et
`PBC.ReplyChanceQuestCompleted` sont conservés. Les taux sont bornés à 100 %.
Un rechargement `.chars reload` au repos permet de relire les paramètres.

Pour tester la différence de longueur, accepter puis rendre plusieurs quêtes des
catégories concernées. Une absence de réaction isolée est normale : aucun taux
n'atteint 100 %. Observer les réactions réellement déclenchées.
