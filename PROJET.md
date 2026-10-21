# KernelDB — État du projet et continuité

État documentaire : 20 octobre 2026. La version déclarée par CMake et le shell
reste **1.7.0** ; les évolutions J4 à J5.5 sont décrites comme travaux non publiés.
Cette mise à jour repose sur les sources locales et ne constitue pas un nouvel audit.

## Objectif et contexte

Moteur SQL embarqué C++17, puis projet communautaire et, à plus long terme,
socle possible d'un SGBD client/serveur. Voir [VISION.md](VISION.md).
Dépôt : `kamelbouz/kerneldb`. Répertoire de développement : `C:\github\kerneldb`.
Construction CMake, tests Catch2, cible bibliothèque `kerneldb_core`
(alias/export `kerneldb::core`), exécutable `kerneldb`.

## Architecture actuelle

| Module | Responsabilité |
|---|---|
| `Session` | Répertoire des bases, base active, routage des commandes |
| `Tokenizer`, `Parser`, `types` | Analyse SQL et AST basé sur `std::variant` |
| `Database` | Validation, exécution SQL, transactions et résultats |
| `Catalog`, `validation` | Métadonnées et contrats de validation |
| `StorageManager` | Sérialisation, scan, insertions et mutations par RowId |
| `Page`, `PageManager` | Format de pages à slots et gestion de pages |
| `BufferPool` | Cache LRU de pages, pin/unpin et suivi des modifications |
| `BPlusTree`, `IndexManager` | Index persistants de contraintes et recherche exacte |
| `DatabaseLock`, `Journal` | Verrou exclusif et journal d'annulation persistant |

La clé du cache inclut la table et le numéro de page. Le format des tables
est paginé (`MDBP`, version d'en-tête 1), distinct de la version du produit.
Les tailles proposées sont 4096, 8192, 16384, 32768 et 65536 octets.
Le choix d'une taille ne garantit pas que toute ligne ou clé y tient : les
limites des slots et des nœuds doivent être prises en compte.

## Jalons

| Jalon | État constaté dans les sources |
|---|---|
| SQL, agrégats, GROUP BY, préparations, multi-base | Implémentés |
| Verrou exclusif, journal persistant, récupération | Implémentés ; robustesse à consolider |
| J4 — Pages SQL et BufferPool intégré | Implémentés |
| J4-bis — Adressage des lignes par RowId | Implémenté ; relocalisation possible sur UPDATE |
| J5.2–J5.3c — B+ tree et suppression | Implémentés avec tests |
| J5.4 — Intégration PK/UNIQUE simples et composites | Implémentée avec maintenance des index |
| J5.5 — SELECT par égalité de clé complète | Implémenté avec tests d'intégration |
| Accès par plages, index généraux, choix de plans | À concevoir |
| Jointures et sous-requêtes | À développer |
| Validation pour publication stable | À réaliser |
| Serveur réseau | Perspective ultérieure |

## Règles de J5.5

Une égalité sur toutes les colonnes d'une `PRIMARY KEY` ou d'une contrainte
`UNIQUE` peut utiliser le B+ tree, puis lire directement la ligne par son `RowId`.
Les égalités peuvent être réunies par `AND`, dans un ordre différent de celui
des colonnes de la clé. La PK est essayée avant les contraintes UNIQUE.

Le chemin indexé exige des valeurs non NULL de même type que les colonnes
(et des doubles finis). Une clé composite partielle, un `OR` dans le filtre,
ou une conversion numérique nécessaire provoque un repli vers le scan.
Les plages, `IN` et `LIKE` ne constituent pas des accès indexés à ce stade ;
ils peuvent rester des filtres supplémentaires avec une égalité de clé complète.
Le filtre WHERE entier est réévalué après lecture. Une clé absente donne un
résultat vide sans scan ; un index pointant vers une ligne absente déclenche une erreur.

## Contraintes et persistance

La PK est unique et non NULL, sur une ou plusieurs colonnes. Les UNIQUE peuvent
être nommées et composites. L'implémentation ignore une ligne dans un index UNIQUE
dès qu'au moins une composante de la clé est NULL ; plusieurs telles lignes sont admises.
Cette règle provient des chemins `row_any_null` du code, malgré d'anciens commentaires
d'en-tête qui évoquent seulement les clés entièrement NULL.

Les index sont stockés dans `<table>.idx` pour la PK et
`<table>.uq_<contrainte>.idx` pour UNIQUE. Une contrainte anonyme porte le nom
généré `uq_<table>_<i>` ; par exemple `u.uq_uq_u_0.idx`.
Ne pas éditer les fichiers de données ou d'index manuellement.

## Validation disponible

Douze exécutables de tests sont déclarés dans `tests/CMakeLists.txt` : tokenizer,
parser, storage, database, session, buffer_pool, validation, page, page_manager,
bplus_tree, index_manager et index_integration.
Dernière exécution observée lors de J5.5, le 18 octobre 2026 : **12 suites sur 12
réussies** ; intégration des index : 16 cas / 102 assertions, stockage : 20 cas /
54 assertions. Ces chiffres sont historiques, sans nouvelle exécution le 20 octobre.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DKERNELDB_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake 3.15 minimum et compilateur C++17 sont requis. Ninja peut être choisi
avec `-G Ninja`. Le moteur utilise la bibliothèque standard ; les tests
téléchargent Catch2 3.5.2 via FetchContent si celui-ci n'est pas déjà disponible.
L'option `KERNELDB_BUILD_BENCH=ON` construit `bench_storage`.

## Limites et défauts à suivre

- Pas de jointures, sous-requêtes, `UNION`, clés étrangères ou contraintes `CHECK`.
- Pas de `CREATE INDEX` SQL opérationnel ni d'optimiseur à coûts.
- Pas de serveur réseau, d'authentification ni de transactions concurrentes.
- DDL `CREATE/DROP/ALTER TABLE` interdit dans une transaction explicite.
- Résultats matérialisés en mémoire ; les recherches non éligibles parcourent la table.
- Le format disque évolue : conserver des sauvegardes et ne pas supposer de migration automatique.
- La fiabilité après incident reste un chantier : voir les défauts historiques
  D01–D03 et E01–E02 dans [CHANGELOG.md](CHANGELOG.md). Leur résolution nécessite
  une vérification dédiée ; les tests courants ne prouvent pas la résistance à une coupure électrique.

Les défauts historiques suivis concernent la suppression du journal au rollback
(D01), l'atomicité données/catalogue d'ALTER (D02), l'ordre de synchronisation
durable (D03), le journal tronqué (E01) et la validation du catalogue (E02).
Ne pas les fermer sur la seule base de cette mise à jour documentaire.

## Prochaines étapes proposées

1. Mesurer les recherches PK/UNIQUE face aux scans sur des volumes reproductibles.
2. Choisir un prochain jalon limité : accès indexés supplémentaires ou jointure simple.
3. Accompagner chaque jalon de tests ciblés et de documentation à jour.
4. Prévoir une phase de consolidation des erreurs d'E/S et de récupération avant
   toute annonce de stabilité ; traiter les défauts connus et vérifier leurs régressions.
5. Préparer les contributions : instructions simples, exemples, tâches délimitées et CI.

Pour reprendre après une pause, lire ce fichier puis vérifier les modifications
locales, le dernier commit et les tests. Une case de jalon décrit une réalisation,
pas une preuve générale d'absence de défauts. Aucun numéro de future version ni
calendrier de publication n'est fixé par cette feuille de route.
