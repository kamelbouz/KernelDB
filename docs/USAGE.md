# KernelDB — Guide d'utilisation

État documentaire : 20 octobre 2026. La version déclarée par CMake et le shell
reste **1.7.0** ; les évolutions J4 à J5.5 sont décrites comme travaux non publiés.
Cette mise à jour repose sur les sources locales et ne constitue pas un nouvel audit.

## Construire et démarrer

Depuis la racine du dépôt :

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DKERNELDB_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake 3.15 minimum et compilateur C++17 sont requis. Ninja peut être choisi
avec `-G Ninja`. Le moteur utilise la bibliothèque standard ; les tests
téléchargent Catch2 3.5.2 via FetchContent si celui-ci n'est pas déjà disponible.
L'option `KERNELDB_BUILD_BENCH=ON` construit `bench_storage`.

Avec Ninja ou Make : `./build/kerneldb` ou `./build/kerneldb.exe` sous Windows.
Le chemin dépend de la configuration avec Visual Studio.
Un argument positionnel choisit la racine des bases : `./build/kerneldb ./mes_bases`.
Sinon KernelDB utilise `KERNELDB_DATA_DIR`, puis `.kerneldb` dans le profil utilisateur.
Le shell attend une commande SQL par ligne : aplatir les exemples multilignes
de ce guide lors d'une saisie interactive.

## Bases et tables

```sql
CREATE DATABASE demo WITH PAGE_SIZE 8192;
USE demo;
SHOW DATABASES;
CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR(50) NOT NULL, email VARCHAR(100) UNIQUE, age INTEGER);
INSERT INTO users VALUES (1, 'Alice', 'alice@example.com', 30);
INSERT INTO users VALUES (2, 'Bob', NULL, 24);
SELECT * FROM users;
UPDATE users SET age = 31 WHERE id = 1;
DELETE FROM users WHERE id = 2;
ALTER TABLE users ADD COLUMN active BOOLEAN;
```

`ALTER TABLE ADD COLUMN` complète les lignes existantes avec NULL. L'ajout de
PK ou UNIQUE via cette commande est refusé. Un ajout NOT NULL doit respecter
les lignes existantes. `DROP TABLE users` supprime la table ; `DROP DATABASE demo`
supprime la base et son contenu. Ces commandes sont destructives.
Les tailles proposées à la création d'une base sont 4096, 8192 (défaut),
16384, 32768 et 65536 octets.

Via `Session` et le shell, `SELECT * FROM demo.users` peut cibler une autre base.
Les références qualifiées fonctionnent aussi pour INSERT, UPDATE, DELETE,
DROP TABLE et ALTER TABLE. Cela ne constitue pas une jointure entre bases.

## Types et NULL

| Type | Exemple | Précision |
|---|---|---|
| `INTEGER` (`INT`) | `42`, `-7` | Entier signé 64 bits |
| `DOUBLE` | `2.5`, `1e3` | Flottant, pas un décimal exact |
| `BOOLEAN` (`BOOL`) | `true`, `false` | Booléen |
| `VARCHAR(n)` | `'Alice'` | Limite sur la taille de la chaîne ; ne pas supposer un comptage Unicode |
| NULL | `NULL` | Absence de valeur |

Employer `IS NULL` / `IS NOT NULL` : `email = NULL` ne sélectionne pas les
lignes NULL. `COUNT(*)` compte les lignes, `COUNT(email)` les valeurs non NULL.
Les autres agrégats ignorent les valeurs NULL.

## Contraintes simples et composites

```sql
CREATE TABLE memberships (tenant INTEGER, id INTEGER, email VARCHAR(100), PRIMARY KEY (tenant, id), CONSTRAINT uq_member_email UNIQUE (tenant, email));
INSERT INTO memberships VALUES (10, 1, 'a@example.com');
INSERT INTO memberships VALUES (10, 2, NULL);
INSERT INTO memberships VALUES (10, 3, NULL);
SELECT * FROM memberships WHERE tenant = 10 AND id = 1;
SELECT * FROM memberships WHERE email = 'a@example.com' AND tenant = 10;
```

Une table possède au plus une PK, éventuellement composite ; chaque composante
est non NULL. Plusieurs contraintes UNIQUE sont possibles. Dans l'implémentation
actuelle, une clé UNIQUE avec **au moins une composante NULL** n'est pas indexée
et n'est pas soumise au contrôle d'unicité de cette contrainte. Les index sont
créés automatiquement pour la PK et les UNIQUE et maintenus lors des mutations.

## Sélection et agrégation

```sql
SELECT name FROM users WHERE age BETWEEN 18 AND 40 ORDER BY name LIMIT 10;
SELECT DISTINCT age FROM users;
SELECT * FROM users WHERE name LIKE 'A%' AND email IS NOT NULL;
SELECT * FROM users WHERE age IN (24, 30, 31);
SELECT COUNT(*), MIN(age), MAX(age), SUM(age), AVG(age) FROM users;
SELECT age, COUNT(*) FROM users GROUP BY age HAVING age >= 18 ORDER BY age;
```

WHERE accepte les comparaisons `=`, `!=`, `<`, `<=`, `>`, `>=`, les opérateurs
LIKE, IN, BETWEEN et leurs formes NOT, AND/OR et les parenthèses.
LIKE emploie `%` pour une séquence et `_` pour un caractère.
Les colonnes projetées avec GROUP BY doivent être regroupées ou agrégées.
HAVING porte sur les colonnes regroupées ; les expressions d'agrégats dans
HAVING et HAVING sans GROUP BY ne sont pas pris en charge.

## Quand SELECT utilise un index

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

Par exemple `id = 1` est éligible pour une PK INTEGER ; `id = 1.0` se replie
vers le scan afin de préserver les règles de comparaison. `tenant = 10` seul
n'utilise pas la PK composite de l'exemple. Aucune commande EXPLAIN n'est fournie.

## Requêtes préparées

```sql
PREPARE find_user AS SELECT * FROM users WHERE id = ?;
EXECUTE find_user USING (1);
DEALLOCATE find_user;
```

L'API C++ permet aussi `db.prepare`, `bind_int`, `bind_string` et les autres
méthodes de liaison, puis `stmt.execute(db)` ; voir [API.md](API.md).
Les paramètres représentent des valeurs. Lier une valeur évite de construire
la requête en concaténant une saisie utilisateur.

## Transactions

```sql
BEGIN;
UPDATE users SET age = 32 WHERE id = 1;
ROLLBACK;
BEGIN;
UPDATE users SET age = 33 WHERE id = 1;
COMMIT;
```

Pas de transactions imbriquées ni de savepoints. Après une erreur en transaction,
ROLLBACK est requis lorsque celle-ci est Aborted. CREATE/DROP/ALTER TABLE sont
interdits dans une transaction explicite. La base est protégée par un verrou
exclusif ; cela ne fournit pas de sessions concurrentes.

Un journal d'annulation persistant permet la récupération à l'ouverture.
Les limites connues de suppression du journal, de synchronisation et de
récupération sont consignées dans [CHANGELOG.md](../CHANGELOG.md) ; ce mécanisme
ne doit pas être présenté comme une garantie complète contre les coupures électriques.

## Commandes du shell

| Commande | Effet |
|---|---|
| `.help` | Aide |
| `.databases`, `.current` | Bases disponibles et base active |
| `.tables` | Tables de la base active |
| `.schema`, `.schema users` | Tous les schémas ou un schéma |
| `.indexes`, `.indexes users` | Index de toutes les tables ou d'une table |
| `.users` | Contenu de la table users |
| `.quit`, `.exit` | Quitter |

Ces commandes appartiennent au REPL, pas à `Database::execute`.

## Fichiers persistants

- `config.meta` : options de base gérées par Session.
- `catalog.meta` : schémas, contraintes et métadonnées.
- `<table>.tbl` : données paginées, signature `MDBP`, version d'en-tête 1,
  en-tête de fichier de 24 octets, pages avec annuaire de slots.
- `<table>.idx` : index PK ; `<table>.uq_<contrainte>.idx` : index UNIQUE.
- `undo.log` : journal d'annulation lorsqu'il est présent.

Pour une contrainte anonyme, le nom généré peut produire un fichier tel que
`u.uq_uq_u_0.idx` : le préfixe du fichier s'ajoute au nom de contrainte.
Le cache LRU est intégré au stockage SQL. Les RowId adressent une page et un
slot ; une mise à jour peut déplacer une ligne. Ne pas les utiliser comme clés métier.
Les anciens guides décrivant des lignes séquentielles `KERNELDB02` ne décrivent
plus ce stockage. Aucune migration automatique depuis ces formats n'est promise.
Ne pas modifier ni copier les fichiers d'une base en cours d'utilisation pour
en faire une sauvegarde supposée cohérente.

## Limites actuelles

- Pas de jointures, sous-requêtes, `UNION`, clés étrangères ou contraintes `CHECK`.
- Pas de `CREATE INDEX` SQL opérationnel ni d'optimiseur à coûts.
- Pas de serveur réseau, d'authentification ni de transactions concurrentes.
- DDL `CREATE/DROP/ALTER TABLE` interdit dans une transaction explicite.
- Résultats matérialisés en mémoire ; les recherches non éligibles parcourent la table.
- Le format disque évolue : conserver des sauvegardes et ne pas supposer de migration automatique.
- La fiabilité après incident reste un chantier : voir les défauts historiques
  D01–D03 et E01–E02 dans [CHANGELOG.md](../CHANGELOG.md). Leur résolution nécessite
  une vérification dédiée ; les tests courants ne prouvent pas la résistance à une coupure électrique.

## Documentation et licence

[Architecture et jalons](../PROJET.md), [vision](../VISION.md),
[API C++](API.md). Licence [MIT](../LICENSE).
