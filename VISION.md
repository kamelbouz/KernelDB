# Vision de KernelDB

Mise à jour : 20 octobre 2026.

## Mission

Construire progressivement un moteur relationnel embarqué utilisable, dont
le fonctionnement peut être compris et expliqué à partir du code C++17.
KernelDB associe apprentissage des SGBD et ambition de produit : la lisibilité,
l'exactitude des résultats et la récupération des données guident son évolution.

Les travaux sur les premiers systèmes relationnels, notamment System R,
constituent une source d'inspiration. KernelDB suit sa propre architecture ;
il ne revendique pas une reproduction fidèle de System R.

## Situation actuelle

Le moteur dispose d'un langage SQL partiel, de pages à slots, d'un cache de pages,
de RowId, de contraintes simples et composites et d'index B+ tree persistants.
Les SELECT par égalité de clé complète peuvent éviter le scan de table.
Un journal d'annulation et un verrou exclusif existent ; leur présence ne suffit
pas à affirmer une fiabilité de production. Les limites sont suivies dans
[PROJET.md](PROJET.md) et [CHANGELOG.md](CHANGELOG.md).

## Trajectoire

1. **Faire évoluer le moteur embarqué** : consolider stockage, index, transactions
   et récupération, puis élargir les accès indexés et introduire des jointures simples.
2. **Préparer un projet communautaire** : compilation reproductible, architecture
   documentée, exemples, tests automatiques, limites explicites et tâches accessibles.
   Le projet peut être présenté avant sa maturité de production, en annonçant son état réel.
3. **Stabiliser une base utilisable** : API claire, politique de compatibilité des
   fichiers, mesures reproductibles et validations d'incidents avant une version stable.
4. **Envisager un SGBD client/serveur** : bâtir une couche autour du moteur lorsque
   ses contrats seront suffisamment solides. Sessions, protocole, concurrence,
   authentification, annulation et ressources devront faire l'objet d'une conception dédiée.

Le serveur est une perspective, sans échéance imposée. Un modèle à un seul écrivain
est une piste à étudier ; aucune architecture de concurrence n'est arrêtée.

## Principes de travail

- Avancer par étapes limitées, avec des tests de régression adaptés à chaque changement.
- Réserver les audits complets aux étapes de consolidation et de préparation à la publication.
- Distinguer fonctionnalité implémentée, vérification réalisée et garantie démontrée.
- Préserver la compréhension du code et expliquer les choix et leurs limites.
- Mesurer les performances avant d'annoncer des gains ou d'ajouter de la complexité.
- Garder le moteur sans dépendance tierce à l'exécution ; les outils de test restent distincts.

Une communauté pourra enrichir le produit. Pour l'accueillir, la priorité est un
dépôt facile à construire, tester et comprendre, avec une direction et des limites visibles.
