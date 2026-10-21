# KernelDB

KernelDB est un moteur relationnel embarqué expérimental en C++17, accompagné
d'un shell SQL multi-base. Le projet vise un moteur utilisable, compréhensible
et progressivement ouvert à une communauté de contributeurs.

État documentaire : 20 octobre 2026. La version déclarée par CMake et le shell
reste **1.7.0** ; les évolutions J4 à J5.5 sont décrites comme travaux non publiés.
Cette mise à jour repose sur les sources locales et ne constitue pas un nouvel audit.

## Fonctionnalités disponibles

- SQL : `CREATE TABLE`, `DROP TABLE`, `ALTER TABLE ADD COLUMN`, `INSERT INTO ... VALUES`,
  `SELECT`, `UPDATE`, `DELETE`.
- Sélection : `WHERE`, `AND`, `OR`, parenthèses, comparaisons, `LIKE`, `IN`,
  `BETWEEN`, leurs variantes négatives, `IS NULL`, `IS NOT NULL`.
- Résultats : `DISTINCT`, `ORDER BY`, `LIMIT`, `COUNT`, `MIN`, `MAX`, `SUM`, `AVG`,
  `GROUP BY`, `HAVING` sur les colonnes de regroupement.
- Types : `INTEGER` 64 bits, `DOUBLE`, `BOOLEAN`, `VARCHAR(n)` et NULL.
- Contraintes : `NOT NULL`, PK et UNIQUE simples ou composites, UNIQUE nommées.
- Requêtes préparées : API C++ et `PREPARE` / `EXECUTE ... USING` / `DEALLOCATE`.
- Transactions explicites, journal d'annulation persistant, récupération à l'ouverture,
  verrou exclusif de base ; les garanties après incident restent à consolider.
- Stockage SQL en pages à slots, `RowId`, BufferPool LRU intégré, index B+ tree
  persistants pour les contraintes et recherches SELECT par égalité de clé complète.
- Multi-base : `CREATE DATABASE`, `DROP DATABASE`, `USE`, `SHOW DATABASES`,
  référence qualifiée `base.table` via `Session`.

## Construire et tester

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DKERNELDB_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake 3.15 minimum et compilateur C++17 sont requis. Ninja peut être choisi
avec `-G Ninja`. Le moteur utilise la bibliothèque standard ; les tests
téléchargent Catch2 3.5.2 via FetchContent si celui-ci n'est pas déjà disponible.
L'option `KERNELDB_BUILD_BENCH=ON` construit `bench_storage`.

Avec Ninja ou Make, lancer `./build/kerneldb` (Windows : `./build/kerneldb.exe`).
Avec un générateur multi-configuration, le binaire peut être sous `build/Release`.

## Premier essai

Dans le shell, saisir chaque commande SQL sur une seule ligne :

```sql
CREATE DATABASE demo;
USE demo;
CREATE TABLE users (id INTEGER PRIMARY KEY, email VARCHAR(100) UNIQUE, name VARCHAR(50));
INSERT INTO users VALUES (1, 'alice@example.com', 'Alice');
SELECT * FROM users WHERE id = 1;
SELECT name FROM users WHERE email = 'alice@example.com';
```

Les commandes `.tables`, `.schema users`, `.indexes users`, `.databases`,
`.current`, `.help` et `.quit` complètent le shell.
Le répertoire racine est choisi par l'argument du programme, sinon
`KERNELDB_DATA_DIR`, sinon le dossier `.kerneldb` du profil utilisateur.

## Accès indexés : portée actuelle

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

## Architecture

`Session` route les commandes vers une `Database`. Le tokenizer et le parser
produisent un AST ; `Database` valide et exécute les instructions avec le
catalogue, le stockage et l'IndexManager. Le stockage lit les pages à travers
le BufferPool. Les index associent une clé encodée à un `RowId` `(page_id, slot_id)`.
Une mise à jour peut relocaliser une ligne et doit alors actualiser ses index.

## Limites et maturité

- Pas de jointures, sous-requêtes, `UNION`, clés étrangères ou contraintes `CHECK`.
- Pas de `CREATE INDEX` SQL opérationnel ni d'optimiseur à coûts.
- Pas de serveur réseau, d'authentification ni de transactions concurrentes.
- DDL `CREATE/DROP/ALTER TABLE` interdit dans une transaction explicite.
- Résultats matérialisés en mémoire ; les recherches non éligibles parcourent la table.
- Le format disque évolue : conserver des sauvegardes et ne pas supposer de migration automatique.
- La fiabilité après incident reste un chantier : voir les défauts historiques
  D01–D03 et E01–E02 dans [CHANGELOG.md](CHANGELOG.md). Leur résolution nécessite
  une vérification dédiée ; les tests courants ne prouvent pas la résistance à une coupure électrique.

## Documentation et contribution

- [Guide d'utilisation](docs/USAGE.md)
- [API C++](docs/API.md)
- [État et feuille de route](PROJET.md)
- [Vision](VISION.md)
- [Historique et défauts connus](CHANGELOG.md)

Pour contribuer : fournir un cas reproductible, indiquer le compilateur et le
système utilisés, puis proposer une modification ciblée avec les tests pertinents.
Les PDF précédemment générés peuvent décrire une version antérieure ; les guides
Markdown ci-dessus décrivent l'état présent.

Licence [MIT](LICENSE). Auteur : Kamel Bouzenad.
