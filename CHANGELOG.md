# Changelog

Toutes les modifications notables de KernelDB sont documentées dans ce fichier.

Format basé sur Keep a Changelog (https://keepachangelog.com/fr/1.1.0/),
versionnage SemVer (https://semver.org/lang/fr/).

---
## [Unreleased]

### Identité KernelDB
- Nom du projet, API C++, headers, exécutables, paquets et configuration : KernelDB.
- Projet initialisé sans historique antérieur ; documentation source conservée.


### Évolutions non publiées — état au 2026-10-20

La version déclarée reste 1.7.0. Cette section décrit les sources locales ;
elle n'annonce ni nouvelle release ni nouvel audit de fiabilité.

#### Ajouté
- Pages à slots pour les lignes SQL, adressage par RowId et BufferPool intégré.
- B+ tree persistant et IndexManager pour les contraintes PK/UNIQUE simples ou
  composites, y compris UNIQUE nommées ; maintenance lors des mutations.
- Commandes REPL `.indexes` et `.indexes <table>`.
- J5.5 : SELECT par égalité non NULL sur une clé PK/UNIQUE complète, sans OR,
  avec lecture ciblée par RowId et réévaluation du WHERE entier.
- Repli vers le scan pour les clés partielles, types non identiques ou OR ;
  clé absente sans scan, erreur explicite si le RowId indexé n'existe pas.
- Tests de stockage ciblé et d'intégration : clés composites, filtres résiduels,
  préparations, mutations, rollback, réouverture et absence de scan complet.

#### Corrigé
- Compteurs de clés des feuilles B+ tree lors des splits et fusions de suppression.
- Attentes des tests d'intégration : nom réel du fichier UNIQUE et réutilisation
  d'une ancienne valeur PK après UPDATE.

#### Documentation
- Actualisation de README, PROJET, VISION, API et USAGE au 20 octobre 2026 :
  architecture paginée, index intégrés, périmètre J5.5, API et limites réelles.
- Vision : consolidation du moteur embarqué, accueil de contributeurs, puis
  éventuelle couche client/serveur ; aucune échéance de stabilité annoncée.
- Dernière validation observée le 18 octobre : 12/12 suites réussies ; aucun
  nouvel essai de coupure électrique ni audit complet lors de cette mise à jour.


### Défauts connus (à corriger)

Cette section liste les défauts identifiés par l'audit du 13 octobre 2026
conservés comme points de suivi. Leur statut doit être confirmé par une
vérification dédiée ; cette mise à jour documentaire ne les clôture pas.

#### P0 — Blocages majeurs

- **D01** — `ROLLBACK` réussit malgré un journal obsolète.
  Si la suppression de `undo.log` échoue (handle Windows bloquant), le
  ROLLBACK réussit, mais le journal reste. Une écriture ultérieure peut
  disparaître après réouverture (ancien journal rejoué).
  *Correction prévue : propager l'erreur de `remove`, empêcher toute
  mutation tant que le journal n'est pas supprimé.*

- **D02** — `ALTER TABLE` non atomique entre données et catalogue.
  Si `catalog_.save` échoue après un `rewrite` réussi, données et
  catalogue divergent. À la réouverture, le schéma peut décrire 1 colonne
  alors que les données en ont 2.
  *Correction prévue : transaction interne de DDL ou protocole
  récupérable avec état de transition.*

- **D03** — Données non synchronisées avant suppression du journal.
  `Journal::commit` supprime `undo.log` sans synchroniser les données
  modifiées. Une coupure électrique entre la suppression du journal et
  l'écriture des données peut laisser un état incohérent.
  *Correction prévue : ordre durable complet — journal fsync → mutations
  → données fsync → journal supprimé.*

#### P1 — Défauts importants

- **E01** — Journal tronqué au milieu d'une ligne.
  La tolérance ajoutée en C06 couvre les frontières de champ, mais pas
  `deserialize_row`. Une troncature au milieu d'une ligne bloque
  l'ouverture.
  *Correction prévue : enregistrements bornés avec marqueur de
  complétude.*

- **E02** — Catalogue invalide encore accepté.
  Un catalogue avec un type inconnu (`type=99`) est accepté à
  l'ouverture. `schema_of` expose le type invalide.
  *Correction prévue : valider complètement le catalogue au chargement
  (types, flags, doublons, nombre de colonnes).*

---


## [1.7.0] — 2026-10-12

### Corrigé (8 défauts P0 du 5e audit)

#### Verrouillage (C01, C02)
- **C01** — Acquisition **atomique** du verrou (`CreateFileW`/`CREATE_NEW` sur Windows, `open`/`O_CREAT|O_EXCL` sur POSIX). La course de 8 threads concurrents donne désormais **un seul propriétaire**.
- **C02** — `DROP DATABASE` acquiert le verrou avant suppression. Refus si une autre instance détient la base.

#### Journal et COMMIT (C04, C05)
- **C04** — `Journal::commit()` propage l'erreur de `remove`. Un COMMIT ne peut plus être un faux succès si le journal n'est pas supprimable.
- **C05** — `sync_file()` portable : `FlushFileBuffers` sur Windows, `fsync` sur POSIX. Le journal est **réellement sur disque** avant la suite.

#### DDL atomique (C07, C08)
- **C07** — `ALTER TABLE` réécrit les données **avant** de mettre à jour le catalogue. Un échec de rewrite ne corrompt plus le schéma.
- **C08** — `rewrite` utilise `ReplaceFileW` (Windows) ou `rename` (POSIX) pour un remplacement **atomique**. Plus de fenêtre sans fichier final.

### Ajouté
- Module `DatabaseLock` — verrou exclusif atomique (mono-connexion)
- Module `Journal` — journal d'annulation persistant (`undo.log`)
- Récupération automatique au démarrage : annulation des transactions non commitées
- ~15 tests de non-régression (C01, C02, C04, C05, C07, C08, J3.1, J3.2)

### Modifié
- `StorageManager::rewrite` : remplacement atomique multiplateforme
- `Session::drop_database` : libère d'abord la base locale, puis acquiert le verrou
- `Database::capture_before` : écrit dans le journal persistant
- `Database::commit` / `rollback` : gèrent le journal

---
## [1.6.0] — 2026-10-08

### Corrigé (15 défauts : R01–R07, A01–A06, B01–B03)

#### Audit 2 — 6 défauts (R01–R07)
- **R01** — Accès hors limites dans ORDER BY (GROUP BY)
- **R04** — Dépassement signé int64 dans SUM
- **R05** — Conversion partielle des tailles (LIMIT, VARCHAR, PAGE_SIZE)
- **R05-bis** — Notation scientifique (1e10, 1.5e-3)
- **R06** — Seconde PRIMARY KEY via ALTER
- **R07** — Routage de PREPARE/EXECUTE vers la base propriétaire

#### Audit 3 — 6 défauts (A01–A06)
- **A01** — Comparaison int64/double : fractions négatives
- **A02** — UNIQUE vérifié sur les valeurs normalisées (avant écriture)
- **A03** — DEALLOCATE routé vers la base propriétaire
- **A04** — Noms de prepared statements uniques par session
- **A05** — Tailles négatives et signées refusées
- **A06** — Projection unifiée : ordre SELECT, DISTINCT, ORDER BY sur colonne non projetée

#### Audit 4 — 3 défauts (B01–B03)
- **B01** — Conversion double → int64 : refus des valeurs hors plage et non finies
- **B02** — 0.0 et -0.0 fusionnés dans DISTINCT et GROUP BY
- **B03** — INSERT et UPDATE appliquent le même contrat de conversion

### Ajouté
- Module `validation` centralisé (schémas, tailles, normalisation, contraintes)
- `VISION.md` — KernelDB = réimplémentation moderne de System R
- ~50 tests de non-régression couvrant les 28 défauts des 4 audits
- Refactoring `exprs` : liste ordonnée d'expressions SELECT (colonne ou agrégat)

### Modifié
- `compute_aggregate` : MIN/MAX/SUM préservent le type (int64 vs double)
- `execute_statement` : contrôle transactionnel centralisé
- `Session` : cycle prepared statements unifié (PREPARE/EXECUTE/DEALLOCATE)
- CI : suppression du générateur NSIS (ZIP uniquement)

---

## [1.5.0] — 2026-10-03

### Corrigé (13 défauts du rapport d'audit)

#### Transactions et validation SQL
- **F01** — Les requêtes préparées respectent désormais les règles transactionnelles
- **F03** — Le DDL (CREATE/DROP/ALTER TABLE) est refusé dans une transaction

#### Exactitude SQL
- **F04** — Comparaison exacte des INTEGER (sans conversion en double)
- **F05** — Clés DISTINCT/GROUP BY injectives (NULL ≠ 'NULL')
- **F06** — NOT IN avec NULL retourne UNKNOWN (exclu par WHERE)
- **F08** — L'ordre des colonnes suit SELECT, pas GROUP BY
- **F13** — MIN/MAX/SUM préservent la précision des grands entiers
- **F14** — LIMIT 0 retourne bien 0 ligne (y compris avec agrégats)
- **F15** — Nombres invalides (`7.1.2`, `123abc`) refusés
- **F18** — SELECT colonne + agrégat sans GROUP BY refusé

#### Validation et stockage
- **F07** — ALTER ADD NOT NULL refusé sur table non vide
- **F09** — En-tête de table tronqué refusé (plus interprété comme table vide)
- **F16** — Colonnes dupliquées refusées
- **F17** — Plusieurs PRIMARY KEY refusées
- **F10** — Bases ouvertes à la demande (cross-database après réouverture)

### Ajouté
- ~40 tests de non-régression couvrant les 13 défauts corrigés
- `BufferPool` : cache LRU avec pin/unpin, dirty tracking, write-back à l'éviction
- 19 tests Catch2 sur `BufferPool` (`tests/test_buffer_pool.cpp`)
- Statistiques du pool : hits, misses, evictions, reset

### Modifié
- `compute_aggregate` : MIN/MAX/SUM préservent le type (int64 vs double)
- `execute_statement` : contrôle transactionnel centralisé
- `Session::database` : ouverture à la demande

---

## [1.3.0] — 2026-09-29

### Ajouté
- Multi-base : CREATE DATABASE, DROP DATABASE, USE, SHOW DATABASES
- Cross-database queries : SELECT * FROM base1.users
  (SELECT, INSERT, UPDATE, DELETE, DROP TABLE, ALTER TABLE)
- Stockage paginé configurable : WITH PAGE_SIZE 4096|8192|16384|32768|65536
- Répertoire des bases configurable via KERNELDB_DATA_DIR (défaut $HOME/.kerneldb/)
- Prompt avec base active : kerneldb (base1)>
- Classe Session (API C++ pour gérer plusieurs bases)
- 18 tests Catch2 sur Session (tests/test_session.cpp)

### Modifié
- main.cpp utilise désormais Session au lieu de la gestion mono-base
- README.md : sections Multi-base + Cross-database, feuille de route mise à jour
- docs/USAGE.md : version 1.3.0, section Multi-database, TOC complétée
- docs/KernelDB_UserGuide.pdf régénéré

---

## [1.2.0]

### Ajouté
- Transactions ACID : BEGIN / COMMIT / ROLLBACK
- Contraintes : NOT NULL, UNIQUE, PRIMARY KEY

---

## [1.1.0]

### Ajouté
- Requêtes préparées : API C++ + syntaxe SQL

---

## [1.0.0]

### Ajouté
- Opérateurs IN, BETWEEN, IS NULL
- AND / OR
- GROUP BY, HAVING
- ALTER TABLE

---

## [0.3.0]

### Ajouté
- Agrégats : COUNT, MIN, MAX, SUM, AVG
- LIKE

---

## [0.2.0]

### Ajouté
- UPDATE, DELETE
- ORDER BY, LIMIT, DISTINCT

---

## [0.1.0]

### Ajouté
- CRUD basique
- Persistance sur disque
- Parser + tokenizer
- Tests initiaux

---

[1.7.0]: https://github.com/kamelbouz/kerneldb/releases/tag/v1.7.0
[1.6.0]: https://github.com/kamelbouz/kerneldb/releases/tag/v1.6.0
[1.5.0]: https://github.com/kamelbouz/kerneldb/releases/tag/v1.5.0
[1.3.0]: https://github.com/kamelbouz/kerneldb/releases/tag/v1.3.0
